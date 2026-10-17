"""TCP transport robustness: reconnect loop, client dying mid-command, second client while one is connected,
idle connection, then a clean session. Runs on the lab Pi.  usage: tcp_robust.py HOST"""
import socket, struct, sys, time, os, signal
sys.path.insert(0, '/tmp')
import swdtcp as T

host = sys.argv[1]
CLOCK = 4000000


def session_ok(tag):
    c = T.Tcp(host)
    d = T.Dap(c)
    d.connect(CLOCK)
    idc = d.bringup()
    w = d.read_word(0x08000000)
    c.close()
    return idc == 0x2ba01477 and w == 0x20008000 or print(f'  {tag}: idcode={idc:08x} word0={w:08x}') or idc == 0x2ba01477


# 1. connect / use / close loop
ok = 0
t0 = time.time()
for i in range(60):
    try:
        ok += bool(session_ok('loop'))
    except Exception as e:
        print('  loop', i, 'failed:', repr(e)[:80])
print(f'1. 60 connect/attach/read/close cycles: {ok}/60 ok in {time.time()-t0:.1f}s')

# 2. client sends half a frame then dies (RST), repeatedly, then a normal session must still work
for i in range(10):
    s = socket.create_connection((host, 3241), timeout=3)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))   # close sends RST
    s.sendall(b'DAP\0' + struct.pack('<H', 600) + b'\x01\x00' + b'\x05' * 100)    # header says 600 bytes, only 100 sent
    time.sleep(0.05)
    s.close()
print('2. after 10 half-frame RST clients:', 'ok' if session_ok('after-rst') else 'FAILED')

# 3. a command in flight, client closes before reading the response
for i in range(10):
    c = T.Tcp(host)
    d = T.Dap(c)
    d.connect(CLOCK)
    blk = bytes([0x06, 0x00]) + struct.pack('<H', 255) + bytes([0x0F])
    c.s.sendall(b'DAP\0' + struct.pack('<H', len(blk)) + b'\x01\x00' + blk)
    c.s.close()
print('3. after 10 closes with a response in flight:', 'ok' if session_ok('after-inflight') else 'FAILED')

# 4. second client while the first is connected: it must queue and be served after the first closes
a = T.Tcp(host)
da = T.Dap(a)
da.connect(CLOCK)
b = socket.create_connection((host, 3241), timeout=3)
b.sendall(b'DAP\0' + struct.pack('<H', 2) + b'\x01\x00' + b'\x00\xff')
b.settimeout(2)
try:
    got = b.recv(64)
    print('4. second client answered while the first is connected (unexpected):', got[:8].hex())
except socket.timeout:
    print('4. second client waits while the first is connected (expected)')
a.close()
b.settimeout(5)
try:
    got = b.recv(64)
    print('   second client answered after the first closed:', 'ok' if got[:4] == b'DAP\0' else got[:8].hex())
except socket.timeout:
    print('   second client got no answer after the first closed: FAILED')
b.close()

# 5. idle connection for 60 s, then use it
c = T.Tcp(host)
d = T.Dap(c)
d.connect(CLOCK)
d.bringup()
time.sleep(60)
try:
    w = d.read_word(0x08000000)
    print(f'5. connection idle 60 s then read word0={w:08x}:', 'ok' if w == 0x20008000 else 'unexpected')
except Exception as e:
    print('5. idle connection broke:', repr(e)[:80])
c.close()
print('6. final session:', 'ok' if session_ok('final') else 'FAILED')
