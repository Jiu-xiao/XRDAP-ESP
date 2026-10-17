"""True USB throughput of the XR probe: requests AND responses use overlapped WinUSB I/O, so the host
controller always has the next IN transfer queued (the synchronous client leaves a gap after every
response and under-reports the device).

usage: xrusb_pipe.py [seconds]
Test 1: DAP_ExecuteCommands (0x7F) carrying N x DAP_Info, ~1 KB response, no SWD at all.
Test 2: the same with a small request/response (DAP_Info only) to see the per-command latency limit.
"""
import ctypes as C
import sys
import time
from ctypes import wintypes as W

sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
from winusb_dap import UsbClient


class OVERLAPPED(C.Structure):
    _fields_ = [('Internal', C.c_size_t), ('InternalHigh', C.c_size_t), ('Offset', W.DWORD),
                ('OffsetHigh', W.DWORD), ('hEvent', W.HANDLE)]


class Pipe:
    def __init__(self, c):
        self.c = c
        k = c.kernel
        k.CreateEventW.argtypes = [C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR]
        k.CreateEventW.restype = W.HANDLE
        k.ResetEvent.argtypes = [W.HANDLE]
        c.usb.WinUsb_GetOverlappedResult.argtypes = [C.c_void_p, C.POINTER(OVERLAPPED), C.POINTER(C.c_uint32), W.BOOL]
        for name in ('WinUsb_WritePipe', 'WinUsb_ReadPipe'):
            getattr(c.usb, name).argtypes = [C.c_void_p, C.c_ubyte, C.c_void_p, C.c_uint32,
                                            C.POINTER(C.c_uint32), C.POINTER(OVERLAPPED)]
        self.events = []

    def _slot(self):
        ov = OVERLAPPED()
        ov.hEvent = self.c.kernel.CreateEventW(None, True, False, None)
        self.events.append(ov.hEvent)
        return ov

    def post(self, fn, ep, buf, length):
        ov = self._slot()
        ok = fn(self.c.handle, ep, buf, length, None, C.byref(ov))
        if not ok and C.get_last_error() != 997:
            raise C.WinError(C.get_last_error())
        return ov

    def wait(self, ov):
        n = C.c_uint32()
        if not self.c.usb.WinUsb_GetOverlappedResult(self.c.handle, C.byref(ov), C.byref(n), True):
            raise C.WinError(C.get_last_error())
        self.c.kernel.CloseHandle(ov.hEvent)
        return n.value

    def run(self, req, resp_len, depth, count):
        """Send `count` copies of req with `depth` in flight; return (seconds, bytes read)."""
        c = self.c
        wbuf = C.create_string_buffer(bytes(req), len(req))
        pending = []
        total = 0
        t0 = time.perf_counter()
        for i in range(count + depth):
            if i < count:
                rbuf = C.create_string_buffer(c.packet_size)
                w = self.post(c.usb.WinUsb_WritePipe, c.ep_out, wbuf, len(req))
                r = self.post(c.usb.WinUsb_ReadPipe, c.ep_in, rbuf, c.packet_size)
                pending.append((w, r, rbuf))
            if len(pending) >= depth or i >= count:
                if pending:
                    w, r, rbuf = pending.pop(0)
                    self.wait(w)
                    n = self.wait(r)
                    if n != resp_len:
                        raise RuntimeError('response length %d, expected %d' % (n, resp_len))
                    total += n
        return time.perf_counter() - t0, total


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    with UsbClient() as c:
        p = Pipe(c)
        big = bytes([0x7F, 255]) + bytes([0x00, 0xFF]) * 255
        r0 = c.exchange(big)
        small = bytes([0x00, 0xFF])
        r1 = c.exchange(small)
        for label, req, rl in (('1 KB response (0x7F x255 DAP_Info)', big, len(r0)),
                               ('4 B response (DAP_Info)', small, len(r1))):
            for depth in (1, 2, 3, 4):
                best = None
                for _ in range(3):
                    dt, tot = p.run(req, rl, depth, count)
                    if best is None or dt < best[0]:
                        best = (dt, tot)
                dt, tot = best
                print('%-36s depth %d: %8.1f KiB/s, %7.0f cmd/s, %5.2f ms per command' % (
                    label, depth, tot / 1024.0 / dt, count / dt, dt / count * 1000.0), flush=True)


if __name__ == '__main__':
    main()


def run_list(self, reqs, resp_lens, depth, rounds):
    """Send reqs[i] / expect resp_lens[i] cyclically, `depth` commands in flight."""
    c = self.c
    pending = []
    total = 0
    count = rounds * len(reqs)
    wbufs = [C.create_string_buffer(bytes(r), len(r)) for r in reqs]
    t0 = time.perf_counter()
    for i in range(count + depth):
        if i < count:
            k = i % len(reqs)
            rbuf = C.create_string_buffer(c.packet_size)
            w = self.post(c.usb.WinUsb_WritePipe, c.ep_out, wbufs[k], len(reqs[k]))
            r = self.post(c.usb.WinUsb_ReadPipe, c.ep_in, rbuf, c.packet_size)
            pending.append((w, r, k))
        if len(pending) >= depth or i >= count:
            if pending:
                w, r, k = pending.pop(0)
                self.wait(w)
                n = self.wait(r)
                if n != resp_lens[k]:
                    raise RuntimeError('response length %d, expected %d (cmd %d)' % (n, resp_lens[k], k))
                total += n
    return time.perf_counter() - t0, total


Pipe.run_list = run_list
