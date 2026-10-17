"""Sequential block-read throughput (DAP_TransferBlock) over native USB.

Core halted + IWDG/WWDG frozen; reads 255 words (1020 B) per command from system memory at
0x1FFF0000 (1 KiB aligned, so the MEM-AP auto-increment does not wrap); 3 commands in flight.
usage: xrblock_usb.py CLOCK_HZ [CLOCK_HZ ...]
"""
import struct
import sys
import time

sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
sys.path.insert(0, r'D:/Codex/tmp/rml')
from winusb_dap import UsbClient
from swdtest_xr import Dap

WORDS = 255
import os
NPAIR = int(os.environ.get('NPAIR', '2'))   # (TAR write, block read) pairs in flight
BASE = 0x1FFF0000


def run(c, clock, rounds=60):
    d = Dap(c)
    d.connect(clock)
    d.bringup()
    d.transfer([(0x05, 0xE000EDF0), (0x0D, 0xA05F0003)])
    for _ in range(50):
        if d.read_word(0xE000EDF0) & 0x20000:
            break
        time.sleep(0.01)
    v = d.read_word(0xE0042008)
    d.transfer([(0x05, 0xE0042008), (0x0D, v | 0x1800)])
    truth = [d.read_word(BASE + 4 * i) for i in range(WORDS)]
    d.transfer([(0x05, BASE)])                                   # TAR (CSW has auto-increment on)
    cmd = bytes([0x06, 0x00]) + struct.pack('<H', WORDS) + bytes([0x0F])   # AP DRW read x255
    # reset TAR before every command: send TAR write as its own tiny command in the same batch
    tar = bytes([0x05, 0x00, 0x01, 0x05]) + struct.pack('<I', BASE)   # DAP_Transfer: AP write TAR
    errs = 0
    best = 0.0
    for _ in range(3):
        t0 = time.perf_counter()
        words = 0
        for _r in range(rounds):
            reqs = []
            for _k in range(NPAIR):
                reqs += [tar, cmd]
            resp = c.exchange_many(reqs)
            for i in range(NPAIR):
                r = resp[2 * i + 1]
                cnt = r[1] | (r[2] << 8)
                ack = r[3]
                vals = [struct.unpack_from('<I', r, 4 + 4 * j)[0] for j in range(cnt)]
                if ack != 1 or cnt != WORDS or vals != truth:
                    errs += 1
                words += cnt
        best = max(best, words / (time.perf_counter() - t0))
    d.transfer([(0x05, 0xE000EDF0), (0x0D, 0xA05F0000)])
    c.exchange(b'\x03')
    print('block read: clock_req=%9d errors=%3d best=%8.0f words/s = %7.1f KiB/s' % (
        clock, errs, best, best * 4 / 1024), flush=True)


def main():
    clocks = [int(x) for x in sys.argv[1:]] or [4000000, 10000000]
    for clk in clocks:
        with UsbClient() as c:
            c.pad = os.environ.get('PAD', '1') == '1'
            run(c, clk)
        time.sleep(0.3)


if __name__ == '__main__':
    main()
