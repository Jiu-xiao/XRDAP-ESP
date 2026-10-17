"""CMSIS-DAP over TCP (DAP\\0 framing) SWD test. Runs on the lab Pi."""
import socket
import struct
import sys
import time

HOST = '10.43.0.168'
PORT = 3241


class Tcp:
    def __init__(self, host=HOST, port=PORT):
        self.s = socket.create_connection((host, port), timeout=3)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def rd(self, n):
        b = b''
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise ConnectionError('closed')
            b += c
        return b

    def exchange(self, req):
        self.s.sendall(b'DAP\0' + struct.pack('<H', len(req)) + b'\x01\x00' + bytes(req))
        h = self.rd(8)
        assert h[:4] == b'DAP\0' and h[6] == 2, h
        return self.rd(struct.unpack('<H', h[4:6])[0])

    def send_many(self, reqs):
        out = b''.join(b'DAP\0' + struct.pack('<H', len(r)) + b'\x01\x00' + bytes(r) for r in reqs)
        self.s.sendall(out)
        res = []
        for _ in reqs:
            h = self.rd(8)
            res.append(self.rd(struct.unpack('<H', h[4:6])[0]))
        return res

    def close(self):
        self.s.close()


def le(v):
    return struct.pack('<I', v)


class Dap:
    def __init__(self, c):
        self.c = c

    def x(self, b):
        return self.c.exchange(bytes(b))

    def connect(self, clock):
        assert self.x([0x02, 0x01]) == b'\x02\x01', 'DAP_Connect'
        assert self.x(b'\x11' + le(clock))[:2] == b'\x11\x00', 'SWJ_Clock'
        self.x([0x04, 0x00, 0x64, 0x00, 0x00, 0x00])
        self.x([0x13, 0x00])
        ones = bytes([0xFF] * 7)
        self.x(bytes([0x12, 56]) + ones)
        self.x(bytes([0x12, 16, 0x9E, 0xE7]))
        self.x(bytes([0x12, 56]) + ones)
        self.x(bytes([0x12, 8, 0x00]))

    def pkt(self, reqs):
        p = bytearray([0x05, 0x00, len(reqs)])
        for r, d in reqs:
            p.append(r)
            if d is not None:
                p += le(d)
        return bytes(p)

    def parse(self, r):
        return r[1], r[2], [struct.unpack_from('<I', r, 3 + 4 * i)[0] for i in range((len(r) - 3) // 4)]

    def transfer(self, reqs):
        return self.parse(self.x(self.pkt(reqs)))

    def bringup(self):
        n, ack, w = self.transfer([(0x02, None)])
        assert ack == 1, 'IDCODE ack=%d' % ack
        idcode = w[0]
        self.transfer([(0x00, 0x1E)])
        self.transfer([(0x08, 0x00000000)])
        self.transfer([(0x04, 0x50000000)])
        for _ in range(20):
            n, ack, w = self.transfer([(0x06, None)])
            if ack == 1 and (w[0] & 0xA0000000) == 0xA0000000:
                break
            time.sleep(0.01)
        else:
            raise RuntimeError('power-up timeout')
        self.transfer([(0x01, 0x23000052)])
        return idcode

    def read_word(self, addr):
        n, ack, w = self.transfer([(0x05, addr), (0x0F, None), (0x0E, None)])
        assert ack == 1, 'read ack=%d' % ack
        return w[-1]

    def scatter_pkt(self, addrs):
        reqs = []
        for a in addrs:
            reqs += [(0x05, a), (0x0F, None)]
        return self.pkt(reqs)


def info(c):
    r = c.exchange(b'\x00\xff')
    print('DAP packet size', struct.unpack('<H', r[2:4])[0])


def main():
    clock = int(sys.argv[1]) if len(sys.argv) > 1 else 1000000
    c = Tcp()
    info(c)
    d = Dap(c)
    d.connect(clock)
    idcode = d.bringup()
    print('clock request %d Hz' % clock)
    print('DPIDR   = %08x' % idcode)
    print('CPUID   = %08x' % d.read_word(0xE000ED00))
    print('DBGMCU_IDCODE = %08x' % d.read_word(0xE0042000))
    print('Flash size (G4) = %d KB' % (d.read_word(0x1FFF75E0) & 0xFFFF))
    c.exchange(b'\x03')
    c.close()


if __name__ == '__main__':
    main()
