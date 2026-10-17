"""Block read through the XR probe with overlapped USB I/O; TAR write + block read pairs like OpenOCD."""
import struct
import sys
import time
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
sys.path.insert(0, r'D:/Codex/tmp/rml')
from winusb_dap import UsbClient
from swdtest_xr import Dap
from xrusb_pipe import Pipe

BASE = 0x1FFF0000
with UsbClient() as c:
    c.pad = False
    p = Pipe(c)
    for clock in [int(x) for x in sys.argv[1:]] or [10000000]:
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
        tar = bytes([0x05, 0x00, 0x01, 0x05]) + struct.pack('<I', BASE)
        blk = bytes([0x06, 0x00]) + struct.pack('<H', 255) + bytes([0x0F])
        rt = c.exchange(tar)
        rb = c.exchange(blk)
        print('TAR response', len(rt), rt.hex(' '), '| block response', len(rb), rb[:6].hex(' '))
        for depth in (2, 4, 6, 8):
            best = None
            for _ in range(3):
                dt, tot = p.run_list([tar, blk], [len(rt), len(rb)], depth, 200)
                if best is None or dt < best[0]:
                    best = (dt, tot)
            dt, tot = best
            print('SWCLK req %8d Hz, TAR+block pairs, %d commands in flight: %7.1f KiB/s data, %5.2f ms per block' % (
                clock, depth, 200 * 1020 / 1024.0 / dt, dt / 200 * 1000), flush=True)
        c.exchange(b'\x03')
