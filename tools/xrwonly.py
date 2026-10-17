import struct, sys, time
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools'); sys.path.insert(0, r'D:/Codex/tmp/rml')
from winusb_dap import UsbClient
from swdtest_xr import Dap
from xrusb_pipe import Pipe
with UsbClient() as c:
    c.pad = False
    p = Pipe(c)
    d = Dap(c); d.connect(12000000); d.bringup()
    tar = bytes([0x05, 0, 1, 0x05]) + struct.pack('<I', 0x20003000)
    wblk = bytes([0x06, 0, 254, 0, 0x0D]) + bytes(4 * 254)
    rt = c.exchange(tar)
    rw = c.exchange(wblk)
    for name, reqs, lens in (('TAR+block write pairs', [tar, wblk], [len(rt), len(rw)]), ('block writes only (no TAR between)', [wblk], [len(rw)])):
        for depth in (2, 4):
            best = None
            for _ in range(3):
                dt, tot = p.run_list(reqs, lens, depth, 200)
                if best is None or dt < best: best = dt
            nblk = 200 * sum(1 for r in reqs if len(r) > 100)
            print('  %-36s depth %d: %6.1f KiB/s, %5.2f ms per write block' % (name, depth, nblk * 254 * 4 / 1024.0 / best, best / nblk * 1000), flush=True)
    c.exchange(b'\x03')
