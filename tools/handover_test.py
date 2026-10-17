"""USB <-> TCP hand-over test, runs on the PC. The Pi opens a TCP session (USB must disappear), closes it
(USB must come back and work).  usage: handover_test.py [rounds]"""
import subprocess, sys, time
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
sys.path.insert(0, r'D:/Codex/tmp/rml')
from winusb_dap import UsbClient
from swdtest_xr import Dap

rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 10
PI_HOLD = ("python3 - <<'EOF'\nimport sys,time\nsys.path.insert(0,'/tmp')\nimport swdtcp as T\n"
           "c=T.Tcp('10.43.0.168'); d=T.Dap(c); d.connect(4000000); idc=d.bringup(); print('tcp idcode %08x'%idc, flush=True)\n"
           "time.sleep(SECS)\nc.close()\nEOF")


def usb_present():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match 'VID_303A&PID_4A01&MI_00' -and $_.Present -ne $false } | Measure-Object).Count"],
                         capture_output=True, text=True).stdout.strip()
    return out == '1'


def usb_works():
    for _ in range(40):
        try:
            with UsbClient() as c:
                c.pad = False
                d = Dap(c)
                d.connect(4000000)
                return d.bringup() == 0x2ba01477
        except Exception:
            time.sleep(0.25)
    return False


ok = 0
for i in range(rounds):
    t0 = time.time()
    before = usb_works()
    hold = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', 'lab3b', PI_HOLD.replace('SECS', '6')], stdout=subprocess.PIPE, text=True)
    line = hold.stdout.readline().strip()
    time.sleep(2.0)
    gone = not usb_present()           # USB must be deinitialised while the TCP client is connected
    hold.wait()
    time.sleep(1.5)
    back = usb_works()
    good = before and 'tcp idcode 2ba01477' in line and gone and back
    ok += good
    print(f'round {i}: usb_before={before} {line} usb_gone_during_tcp={gone} usb_back_and_works={back} -> {"OK" if good else "FAIL"} ({time.time()-t0:.1f}s)', flush=True)
print(f'hand-over: {ok}/{rounds}')
