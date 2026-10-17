"""USB block write/read soak at one clock, same patterns as tcp_block.py (to compare error rates).
usage: usb_wsoak.py CLOCK_HZ SECONDS"""
import struct, sys, time, collections
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
sys.path.insert(0, r'D:/Codex/tmp/rml')
from winusb_dap import UsbClient
from swdtest_xr import Dap

clock = int(sys.argv[1]); secs = float(sys.argv[2])
W, WW = 255, 254
RD_BASE, WR_BASE = 0x1FFF0000, 0x20002000
ERR = collections.Counter()
le = lambda v: struct.pack('<I', v)

with UsbClient() as c:
    c.pad = False
    d = Dap(c)
    d.connect(clock); d.bringup()
    d.transfer([(0x05, 0xE000EDF0), (0x0D, 0xA05F0003)])
    for _ in range(50):
        if d.read_word(0xE000EDF0) & 0x20000:
            break
        time.sleep(0.01)
    v = d.read_word(0xE0042008)
    d.transfer([(0x05, 0xE0042008), (0x0D, v | 0x1800)])
    truth = [d.read_word(RD_BASE + 4 * i) for i in range(W)]
    tar_rd = bytes([0x05, 0x00, 0x01, 0x05]) + le(RD_BASE)
    blk_rd = bytes([0x06, 0x00]) + struct.pack('<H', W) + bytes([0x0F])
    tar_wr = bytes([0x05, 0x00, 0x01, 0x05]) + le(WR_BASE)
    pattern = [(0xA5A50000 + i * 0x10001) & 0xFFFFFFFF for i in range(WW)]
    blk_wr = bytes([0x06, 0x00]) + struct.pack('<H', WW) + bytes([0x0D]) + b''.join(le(x) for x in pattern)
    blk_back = bytes([0x06, 0x00]) + struct.pack('<H', WW) + bytes([0x0F])
    n = 0; bad = 0; wr_blocks = 0; rd_blocks = 0
    t0 = time.time()
    while time.time() - t0 < secs:
        res = c.exchange_many([tar_rd, blk_rd, tar_rd, blk_rd, tar_rd, blk_rd, tar_rd, blk_rd])
        for i in range(4):
            r = res[2 * i + 1]; cnt = r[1] | (r[2] << 8)
            vals = [struct.unpack_from('<I', r, 4 + 4 * j)[0] for j in range(cnt)]
            rd_blocks += 1
            if r[3] != 1 or cnt != W or vals != truth:
                bad += 1; ERR[('read', 'ack=%d' % r[3], 'cnt=%d' % cnt)] += 1
        res = c.exchange_many([tar_wr, blk_wr, tar_wr, blk_wr])
        for i in range(2):
            r = res[2 * i + 1]; cnt = r[1] | (r[2] << 8)
            wr_blocks += 1
            if r[3] != 1 or cnt != WW:
                bad += 1; ERR[('write', 'ack=%d' % r[3], 'cnt=%d' % cnt)] += 1
        res = c.exchange_many([tar_wr, blk_back])
        r = res[1]
        vals = [struct.unpack_from('<I', r, 4 + 4 * j)[0] for j in range(r[1] | (r[2] << 8))]
        if vals != pattern:
            bad += 1; ERR[('readback_mismatch',)] += 1
        n += 1
    print(f'USB {clock/1e6:.0f} MHz soak {secs:.0f}s: {rd_blocks} read blocks, {wr_blocks} write blocks, bad={bad} {dict(ERR)}')
    d.transfer([(0x05, 0xE000EDF0), (0x0D, 0xA05F0000)])
    c.exchange(b'\x03')
