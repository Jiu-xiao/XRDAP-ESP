import sys, time
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools'); sys.path.insert(0, r'D:/Codex/tmp/rml')
from winusb_dap import UsbClient
from xrusb_pipe import Pipe
with UsbClient() as c:
    c.pad = False
    p = Pipe(c)
    n = 169
    req = bytes([0x7F, n]) + bytes([0x04, 0x00, 0x64, 0x00, 0x00, 0x00]) * n
    r0 = c.exchange(req)
    print('OUT-heavy command (no SWD): request %d B, response %d B' % (len(req), len(r0)))
    for depth in (1, 2, 3, 4):
        best = None
        for _ in range(3):
            dt, tot = p.run(req, len(r0), depth, 400)
            if best is None or dt < best:
                best = dt
        print('  depth %d: OUT %6.1f KiB/s, %5.2f ms per command' % (depth, 400 * len(req) / 1024.0 / best, best / 400 * 1000), flush=True)
