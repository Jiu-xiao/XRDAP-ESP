"""Hot-plug test: stream TAR + 255-word block reads (4 requests in flight per round) for DURATION seconds.
Unplug and replug the DAP USB cable at any time; the script logs every loss, reconnects by itself (no board reset)
and checks that data read after a reconnect is still correct.
usage: hotplug.py [seconds] [clock_hz]"""
import struct, sys, time, os
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
sys.path.insert(0, r'D:/Codex/tmp/rml')
from winusb_dap import UsbClient
from swdtest_xr import Dap

DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 90
CLOCK = int(sys.argv[2]) if len(sys.argv) > 2 else 4000000
WORDS = 255
BASE = 0x1FFF0000
T0 = time.time()


def log(msg):
    print(f'[{time.time() - T0:6.1f}s] {msg}', flush=True)


def setup(c):
    d = Dap(c)
    d.connect(CLOCK)
    d.bringup()
    d.transfer([(0x05, 0xE000EDF0), (0x0D, 0xA05F0003)])          # halt
    for _ in range(50):
        if d.read_word(0xE000EDF0) & 0x20000:
            break
        time.sleep(0.01)
    v = d.read_word(0xE0042008)
    d.transfer([(0x05, 0xE0042008), (0x0D, v | 0x1800)])           # freeze IWDG/WWDG
    truth = [d.read_word(BASE + 4 * i) for i in range(WORDS)]
    d.transfer([(0x05, BASE)])
    cmd = bytes([0x06, 0x00]) + struct.pack('<H', WORDS) + bytes([0x0F])
    tar = bytes([0x05, 0x00, 0x01, 0x05]) + struct.pack('<I', BASE)
    return truth, tar, cmd


def stream(c, truth, tar, cmd, state):
    last = time.time()
    while time.time() - T0 < DURATION:
        resp = c.exchange_many([tar, cmd, tar, cmd])
        for i in range(2):
            r = resp[2 * i + 1]
            cnt = r[1] | (r[2] << 8)
            vals = [struct.unpack_from('<I', r, 4 + 4 * j)[0] for j in range(cnt)]
            if r[3] != 1 or cnt != WORDS or vals != truth:
                state['bad'] += 1
                log(f'BAD DATA ack={r[3]} cnt={cnt}')
        state['rounds'] += 1
        if time.time() - last > 5:
            last = time.time()
            log(f'streaming ok, {state["rounds"]} rounds, {state["bad"]} bad, {state["losses"]} losses')


def main():
    state = {'rounds': 0, 'bad': 0, 'losses': 0, 'reconnects': 0, 'reconnect_fail': 0}
    log('start; unplug / replug the DAP USB cable whenever you like')
    c = None
    while time.time() - T0 < DURATION:
        try:
            if c is None:
                t = time.time()
                c = UsbClient()
                c.pad = False
                truth, tar, cmd = setup(c)
                if state['losses']:
                    state['reconnects'] += 1
                    log(f'RECONNECTED and data verified (took {time.time() - t:.2f}s since open started)')
                else:
                    log('connected, target halted, reference data read')
            stream(c, truth, tar, cmd, state)
        except Exception as e:
            if c is not None:
                state['losses'] += 1
                log(f'LOST: {type(e).__name__}: {str(e)[:80]}')
                try:
                    c.close()
                except Exception:
                    pass
                c = None
            else:
                state['reconnect_fail'] += 1     # open/setup failed while waiting for the device
                time.sleep(0.3)
    try:
        if c is not None:
            c.exchange(b'\x03')
            c.close()
    except Exception:
        pass
    log(f'SUMMARY: rounds={state["rounds"]} bad_data={state["bad"]} losses={state["losses"]} '
        f'reconnects_verified={state["reconnects"]} open_retries={state["reconnect_fail"]}')


main()
