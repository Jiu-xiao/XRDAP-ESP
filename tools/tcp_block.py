"""Block read / block write throughput and integrity over CMSIS-DAP TCP. Runs on the lab Pi (host 10.43.0.1).
usage: tcp_block.py HOST CLOCK_HZ [soak_seconds]"""
import struct, sys, time
sys.path.insert(0, '/tmp')
import swdtcp as T

host, clock = sys.argv[1], int(sys.argv[2])
soak = float(sys.argv[3]) if len(sys.argv) > 3 else 0
W = 255
RD_BASE = 0x1FFF0000          # system memory, 1 KiB aligned
WR_BASE = 0x20002000          # RAM scratch (not used by the target image)

import collections
ERR = collections.Counter()
c = T.Tcp(host)
d = T.Dap(c)
d.connect(clock)
d.bringup()
d.transfer([(0x05, 0xE000EDF0), (0x0D, 0xA05F0003)])               # halt
for _ in range(50):
    if d.read_word(0xE000EDF0) & 0x20000:
        break
    time.sleep(0.01)
v = d.read_word(0xE0042008)
d.transfer([(0x05, 0xE0042008), (0x0D, v | 0x1800)])               # freeze IWDG/WWDG
truth = [d.read_word(RD_BASE + 4 * i) for i in range(W)]

tar_rd = bytes([0x05, 0x00, 0x01, 0x05]) + T.le(RD_BASE)
blk_rd = bytes([0x06, 0x00]) + struct.pack('<H', W) + bytes([0x0F])
tar_wr = bytes([0x05, 0x00, 0x01, 0x05]) + T.le(WR_BASE)
WW = 254                      # 5-byte header + 254 words = 1021 bytes (255 words would exceed the 1024-byte DAP packet)
pattern = [(0xA5A50000 + i * 0x10001) & 0xFFFFFFFF for i in range(WW)]
blk_wr = bytes([0x06, 0x00]) + struct.pack('<H', WW) + bytes([0x0D]) + b''.join(T.le(x) for x in pattern)
blk_rd_back = bytes([0x06, 0x00]) + struct.pack('<H', WW) + bytes([0x0F])
tar_back = tar_wr


def read_round(depth):
    reqs = []
    for _ in range(depth):
        reqs += [tar_rd, blk_rd]
    res = c.send_many(reqs)
    bad = 0
    for i in range(depth):
        r = res[2 * i + 1]
        cnt = r[1] | (r[2] << 8)
        vals = [struct.unpack_from('<I', r, 4 + 4 * j)[0] for j in range(cnt)]
        if r[3] != 1 or cnt != W or vals != truth:
            bad += 1
            ERR[('read', 'ack=%d' % r[3], 'cnt=%d' % cnt, 'data_ok=%s' % (vals == truth))] += 1
    return bad


def write_round(depth):
    reqs = []
    for _ in range(depth):
        reqs += [tar_wr, blk_wr]
    res = c.send_many(reqs)
    bad = 0
    for i in range(depth):
        r = res[2 * i + 1]
        if r[3] != 1 or (r[1] | (r[2] << 8)) != WW:
            bad += 1
            ERR[('write', 'ack=%d' % r[3], 'cnt=%d' % (r[1] | (r[2] << 8)))] += 1
    return bad


def verify_written():
    res = c.send_many([tar_back, blk_rd_back])
    r = res[1]
    vals = [struct.unpack_from('<I', r, 4 + 4 * j)[0] for j in range(r[1] | (r[2] << 8))]
    return vals == pattern


for depth in (1, 2, 4):
    t0 = time.time(); n = 0; bad = 0
    while time.time() - t0 < 3:
        bad += read_round(depth); n += depth
    dt = time.time() - t0
    print(f'{clock/1e6:4.1f} MHz block read  depth {depth}: {n*W*4/1024/dt:7.1f} KiB/s  bad={bad}', flush=True)
for depth in (1, 2, 4):
    t0 = time.time(); n = 0; bad = 0
    while time.time() - t0 < 3:
        bad += write_round(depth); n += depth
    dt = time.time() - t0
    ok = verify_written()
    print(f'{clock/1e6:4.1f} MHz block write depth {depth}: {n*WW*4/1024/dt:7.1f} KiB/s  bad={bad}  readback_ok={ok}', flush=True)
if soak:
    t0 = time.time(); n = 0; bad = 0
    while time.time() - t0 < soak:
        bad += read_round(4); bad += write_round(2); n += 6
        if not verify_written():
            bad += 1
            ERR[('verify_written_mismatch',)] += 1
    print(f'soak {soak:.0f}s: {n} block ops, bad={bad}', flush=True)
d.transfer([(0x05, 0xE000EDF0), (0x0D, 0xA05F0000)])
c.exchange(b'\x03')
print('error kinds:', dict(ERR))
