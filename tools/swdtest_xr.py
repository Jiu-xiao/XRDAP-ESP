"""Standard CMSIS-DAP SWD probe test over native USB (no connector service)."""
import os
import struct
import sys
import time

sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
from winusb_dap import UsbClient


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
        self.x([0x04, 0x00, 0x64, 0x00, 0x00, 0x00])          # idle 0, WAIT retry 100
        self.x([0x13, 0x00])                                  # turnaround 1, no data phase
        ones = bytes([0xFF] * 7)
        self.x(bytes([0x12, 56]) + ones)                       # line reset
        self.x(bytes([0x12, 16, 0x9E, 0xE7]))                  # JTAG -> SWD
        self.x(bytes([0x12, 56]) + ones)
        self.x(bytes([0x12, 8, 0x00]))

    def transfer(self, reqs):
        """reqs: list of (request_byte, data or None). Returns (count, ack, words)."""
        p = bytearray([0x05, 0x00, len(reqs)])
        for r, d in reqs:
            p.append(r)
            if d is not None:
                p += le(d)
        r = self.x(p)
        words = [struct.unpack_from('<I', r, 3 + 4 * i)[0] for i in range((len(r) - 3) // 4)]
        return r[1], r[2], words

    def bringup(self):
        n, ack, w = self.transfer([(0x02, None)])
        assert ack == 1, 'IDCODE ack=%d' % ack
        idcode = w[0]
        self.transfer([(0x00, 0x1E)])                          # ABORT clear sticky
        self.transfer([(0x08, 0x00000000)])                    # SELECT bank 0
        self.transfer([(0x04, 0x50000000)])                    # CTRL/STAT power up
        for _ in range(20):
            n, ack, w = self.transfer([(0x06, None)])
            if ack == 1 and (w[0] & 0xA0000000) == 0xA0000000:
                break
            time.sleep(0.01)
        else:
            raise RuntimeError('power-up timeout')
        self.transfer([(0x01, 0x23000052)])                    # AP CSW 32-bit
        return idcode

    def read_word(self, addr):
        n, ack, w = self.transfer([(0x05, addr), (0x0F, None), (0x0E, None)])
        assert ack == 1, 'read ack=%d' % ack
        return w[-1]


def main():
    clock = int(sys.argv[1]) if len(sys.argv) > 1 else 1000000
    with UsbClient() as c:
        d = Dap(c)
        d.connect(clock)
        idcode = d.bringup()
        print('clock request %d Hz' % clock)
        print('DPIDR   = %08x' % idcode)
        print('CPUID   = %08x' % d.read_word(0xE000ED00))
        print('DBGMCU_IDCODE(F4) = %08x' % d.read_word(0xE0042000))
        print('Flash size (G4) = %d KB' % (d.read_word(0x1FFF75E0) & 0xFFFF))
        c.exchange(b'\x03')                                    # DAP_Disconnect


if __name__ == '__main__':
    main()
