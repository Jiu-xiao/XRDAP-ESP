import sys, time, os
from pyocd.core.helpers import ConnectHelper
hz = int(sys.argv[1])
with ConnectHelper.session_with_chosen_probe(target_override='cortex_m', options={'frequency': hz, 'connect_mode': 'halt'}) as s:
    t = s.target
    t.halt()
    t0 = time.time(); data = t.read_memory_block8(0x08000000, 0x20000); dt = time.time() - t0
    ref = open(r'D:/Codex/tmp/xrdap-s3/flashtest/dump_hi.bin', 'rb').read()
    print(f'{hz/1e6:.0f} MHz read 128 KiB: {128/dt:.0f} KiB/s, identical to image: {bytes(data)==ref[:len(data)]}')
    buf = bytes((i * 7 + 3) & 0xFF for i in range(0x4000))
    t0 = time.time(); t.write_memory_block8(0x20000000, buf); dt = time.time() - t0
    back = bytes(t.read_memory_block8(0x20000000, 0x4000))
    print(f'{hz/1e6:.0f} MHz write 16 KiB: {16/dt:.0f} KiB/s, readback ok: {back==buf}')
