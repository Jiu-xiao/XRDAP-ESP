"""A client that vanishes without FIN (its RST is dropped by a temporary iptables rule towards the probe),
then a new client tries to get served. Runs on the lab Pi.   usage: tcp_dead_client.py HOST"""
import socket, struct, subprocess, sys, time
sys.path.insert(0, '/tmp')
import swdtcp as T

host = sys.argv[1]
RULE = ['OUTPUT', '-d', host, '-p', 'tcp', '--dport', '3241', '-j', 'DROP']


def ipt(op):
    # blackhole route to the probe instead of an iptables rule (iptables is not installed on the Pi)
    cmd = ['sudo', '-n', 'ip', 'route', 'add' if op == '-I' else 'del', 'blackhole', host + '/32']
    return subprocess.run(cmd, capture_output=True, text=True)


def try_new_client(limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            s = socket.create_connection((host, 3241), timeout=3)
            s.settimeout(3)
            s.sendall(b'DAP\0' + struct.pack('<H', 2) + b'\x01\x00' + b'\x00\xff')
            got = s.recv(64)
            s.close()
            if got[:4] == b'DAP\0':
                return time.time() - t0
        except Exception:
            time.sleep(1)
    return None


c = T.Tcp(host)
d = T.Dap(c)
d.connect(4000000)
d.bringup()
c.s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))   # close -> single RST
try:
    r = ipt('-I')
    print('iptables insert:', r.returncode, r.stderr.strip()[:60])
    c.s.close()                    # RST is dropped, the probe never sees it
    time.sleep(1)
finally:
    r = ipt('-D')
    print('iptables delete:', r.returncode, r.stderr.strip()[:60])
t = try_new_client(60)
print('new client served after', f'{t:.1f}s' if t is not None else 'NEVER (60 s): the server is stuck on the dead connection')
