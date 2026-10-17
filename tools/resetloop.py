"""Reset the board N times; after each reset open a WinUSB session with NO warm-up and count successes."""
import sys, time, subprocess
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
from winusb_dap import UsbClient
PY = r'C:\Users\a2592\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe'
n = int(sys.argv[1]); wait = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0
ok = 0; fails = []
for i in range(n):
    subprocess.run([PY, '-m', 'esptool', '--chip', 'esp32s3', '--port', 'COM108', '--before', 'default_reset',
                    '--after', 'hard_reset', 'read_mac'], capture_output=True)
    time.sleep(wait)
    t = time.time()
    try:
        c = UsbClient()
        r = c.exchange(bytes([0x00, 0x01]))   # DAP_Info vendor name
        c.close()
        ok += 1
        print(i, 'OK', f'{time.time()-t:.2f}s', r[:8].hex())
    except Exception as e:
        fails.append(i)
        print(i, 'FAIL', repr(e)[:100], f'{time.time()-t:.2f}s')
        try:
            c.close()
        except Exception:
            pass
print(f'first-session first-command: {ok}/{n} OK, fails at {fails}')
