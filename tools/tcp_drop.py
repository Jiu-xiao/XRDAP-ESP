"""Wi-Fi drop test, runs on the lab Pi. The probe station is deauthenticated with `iw station del` (only the probe).
usage: tcp_drop.py HOST MAC"""
import subprocess, sys, time, socket, struct
sys.path.insert(0, '/tmp')
import swdtcp as T

host, mac = sys.argv[1], sys.argv[2]
CLOCK = 4000000


def deauth():
    return subprocess.run(['sudo', '-n', 'iw', 'dev', 'wlan0', 'station', 'del', mac], capture_output=True, text=True)


def wait_ping(limit=40):
    t = time.time()
    while time.time() - t < limit:
        if subprocess.run(['ping', '-c1', '-W1', host], capture_output=True).returncode == 0:
            return time.time() - t
    return None


def attach_ok():
    c = T.Tcp(host)
    d = T.Dap(c)
    d.connect(CLOCK)
    idc = d.bringup()
    c.close()
    return idc == 0x2ba01477


# A: idle, no client: drop and see how long the probe takes to come back and serve
r = deauth()
print('A. deauth idle probe:', r.returncode, (r.stderr or '').strip()[:80])
t = wait_ping()
print('   ping back after', None if t is None else f'{t:.1f}s')
time.sleep(1)
try:
    print('   attach after reconnect:', 'ok' if attach_ok() else 'FAILED')
except Exception as e:
    print('   attach after reconnect FAILED:', repr(e)[:80])

# B: client connected and streaming, drop, client keeps its socket for 5 s then closes (FIN lost), new client after reconnect
c = T.Tcp(host)
d = T.Dap(c)
d.connect(CLOCK)
d.bringup()
deauth()
time.sleep(5)
try:
    c.s.close()
except Exception:
    pass
t = wait_ping()
print('B. drop with an open client, client closed while the link is down; ping back after', None if t is None else f'{t:.1f}s')
t0 = time.time()
ok = False
while time.time() - t0 < 45:
    try:
        s = socket.create_connection((host, 3241), timeout=3)
        s.settimeout(3)
        s.sendall(b'DAP\0' + struct.pack('<H', 2) + b'\x01\x00' + b'\x00\xff')
        got = s.recv(64)
        s.close()
        if got[:4] == b'DAP\0':
            ok = True
            break
    except Exception:
        time.sleep(1)
print('   new client served after', f'{time.time()-t0:.1f}s' if ok else 'NEVER (45 s): the server is stuck on the dead connection')
