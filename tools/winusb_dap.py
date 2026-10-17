"""Generic WinUSB CMSIS-DAP v2 client (adapted from RMLink rmlink_native_usb.py, Apache-2.0)."""
# SPDX-License-Identifier: Apache-2.0
import ctypes as C
from ctypes import wintypes as W
import uuid
from rmlink_protocol import MAX_PACKET_SIZE, packet_size_from_info


class GUID(C.Structure):
    _fields_ = [('data', C.c_ubyte * 16)]


class InterfaceData(C.Structure):
    _fields_ = [('size', W.DWORD), ('guid', GUID), ('flags', W.DWORD), ('reserved', C.c_size_t)]


class UsbInterface(C.Structure):
    _pack_ = 1
    _fields_ = [(name, C.c_ubyte) for name in ('length', 'type', 'number', 'alternate', 'endpoints', 'cls', 'subclass', 'protocol', 'string')]


class PipeInfo(C.Structure):
    _fields_ = [('type', C.c_uint32), ('id', C.c_ubyte), ('max_packet', C.c_uint16), ('interval', C.c_ubyte)]


class UsbClient:
    GUID_TEXT = '{CDB3B5AD-293B-4663-AA36-1AAE46463776}'

    def __init__(self, *, bcd_device=None, serial=None):
        self.packet_size = 64
        self.file = self.handle = None
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        self.setup = C.WinDLL('setupapi', use_last_error=True)
        self.usb = C.WinDLL('winusb', use_last_error=True)
        ptr = C.c_void_p
        self.kernel.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, ptr, W.DWORD, W.DWORD, W.HANDLE]
        self.kernel.CreateFileW.restype = W.HANDLE
        self.kernel.CloseHandle.argtypes = [W.HANDLE]
        self.setup.SetupDiGetClassDevsW.argtypes = [C.POINTER(GUID), W.LPCWSTR, W.HWND, W.DWORD]
        self.setup.SetupDiGetClassDevsW.restype = W.HANDLE
        self.setup.SetupDiEnumDeviceInterfaces.argtypes = [W.HANDLE, ptr, C.POINTER(GUID), W.DWORD, C.POINTER(InterfaceData)]
        self.setup.SetupDiGetDeviceInterfaceDetailW.argtypes = [W.HANDLE, C.POINTER(InterfaceData), ptr, W.DWORD, C.POINTER(W.DWORD), ptr]
        self.setup.SetupDiDestroyDeviceInfoList.argtypes = [W.HANDLE]
        self.usb.WinUsb_Initialize.argtypes = [W.HANDLE, C.POINTER(ptr)]
        self.usb.WinUsb_Free.argtypes = [ptr]
        self.usb.WinUsb_QueryInterfaceSettings.argtypes = [ptr, C.c_ubyte, C.POINTER(UsbInterface)]
        self.usb.WinUsb_QueryPipe.argtypes = [ptr, C.c_ubyte, C.c_ubyte, C.POINTER(PipeInfo)]
        self.usb.WinUsb_SetPipePolicy.argtypes = [ptr, C.c_ubyte, C.c_uint32, C.c_uint32, ptr]
        self.usb.WinUsb_GetDescriptor.argtypes = [ptr, C.c_ubyte, C.c_ubyte, C.c_uint16, ptr, C.c_uint32, C.POINTER(C.c_uint32)]
        for name in ('WinUsb_WritePipe', 'WinUsb_ReadPipe'):
            getattr(self.usb, name).argtypes = [ptr, C.c_ubyte, ptr, C.c_uint32, C.POINTER(C.c_uint32), ptr]
        try:
            for path in self._paths():
                self.file = self.kernel.CreateFileW(path, 0xc0000000, 0, None, 3, 0x40000000, None)
                if self.file in (None, C.c_void_p(-1).value):
                    self.file = None
                    continue
                handle = ptr()
                if not self.usb.WinUsb_Initialize(self.file, C.byref(handle)):
                    self.close()
                    continue
                self.handle = handle
                try:
                    descriptor = self._descriptor(1, 0)
                    self.product = self._descriptor(3, descriptor[15], 0x409)[2:].decode('utf-16le', errors='strict')
                except OSError as e:
                    print('DBG descriptor error', e)
                    self.close()
                    continue
                if len(descriptor) < 18:
                    self.close()
                    continue
                self.bcd_device = int.from_bytes(descriptor[12:14], 'little')
                if bcd_device is not None and self.bcd_device != bcd_device:
                    self.close()
                    continue
                self.product = self._descriptor(3, descriptor[15], 0x409)[2:].decode('utf-16le', errors='strict')
                if self.product not in ('RMLink CMSIS-DAP', 'XR-DAP-S3', 'CMSIS-DAP v2'):
                    self.close()
                    continue
                self.serial = self._descriptor(3, descriptor[16], 0x409)[2:].decode('utf-16le', errors='strict')
                if serial is not None and self.serial != serial:
                    self.close()
                    continue
                interface = UsbInterface()
                self._check(self.usb.WinUsb_QueryInterfaceSettings(handle, 0, C.byref(interface)))
                self.ep_in = self.ep_out = None
                for index in range(interface.endpoints):
                    pipe = PipeInfo()
                    self._check(self.usb.WinUsb_QueryPipe(handle, 0, index, C.byref(pipe)))
                    if pipe.type == 2:
                        if pipe.id & 0x80: self.ep_in = pipe.id
                        else: self.ep_out = pipe.id
                if self.ep_in is None or self.ep_out is None:
                    raise RuntimeError('No Bulk IN/OUT endpoint pair')
                timeout = C.c_uint32(3000)
                for ep in (self.ep_in, self.ep_out):
                    self._check(self.usb.WinUsb_SetPipePolicy(handle, ep, 3, C.sizeof(timeout), C.byref(timeout)))
                # Short bootstrap OUT works with any supported logical size.
                self._send(b'\x00\xff', pad=False)
                self.packet_size = packet_size_from_info(self._receive(MAX_PACKET_SIZE))
                return
            raise RuntimeError('No available RMLink WinUSB interface; check OTG cable, firmware and device occupancy')
        except BaseException:
            self.close()
            raise

    @staticmethod
    def _check(ok):
        if not ok: raise C.WinError(C.get_last_error())

    def _paths(self):
        guid = GUID((C.c_ubyte * 16).from_buffer_copy(uuid.UUID(self.GUID_TEXT).bytes_le))
        devices = self.setup.SetupDiGetClassDevsW(C.byref(guid), None, None, 0x12)
        if devices in (None, C.c_void_p(-1).value): raise C.WinError(C.get_last_error())
        try:
            index = 0
            while True:
                interface = InterfaceData(); interface.size = C.sizeof(interface)
                if not self.setup.SetupDiEnumDeviceInterfaces(devices, None, C.byref(guid), index, C.byref(interface)):
                    if C.get_last_error() == 259: break
                    raise C.WinError(C.get_last_error())
                required = W.DWORD()
                self.setup.SetupDiGetDeviceInterfaceDetailW(devices, C.byref(interface), None, 0, C.byref(required), None)
                if required.value < 8: raise RuntimeError('Invalid interface detail size')
                detail = C.create_string_buffer(required.value)
                C.cast(detail, C.POINTER(W.DWORD))[0] = 8 if C.sizeof(C.c_void_p) == 8 else 6
                self._check(self.setup.SetupDiGetDeviceInterfaceDetailW(devices, C.byref(interface), detail, required, None, None))
                yield C.wstring_at(C.addressof(detail) + 4)
                index += 1
        finally:
            self.setup.SetupDiDestroyDeviceInfoList(devices)

    def _descriptor(self, kind, index, lang=0):
        buffer = C.create_string_buffer(256); count = C.c_uint32()
        self._check(self.usb.WinUsb_GetDescriptor(self.handle, kind, index, lang, buffer, 256, C.byref(count)))
        return buffer.raw[:count.value]

    def exchange(self, request):
        if bytes(request)[:1] == b'\x07':
            raise ValueError('TransferAbort has no response; use abort_transfer()')
        self.send(request)
        return self.receive()

    def abort_transfer(self): self.send(b'\x07')

    def exchange_many(self, requests):
        requests = [bytes(request) for request in requests]
        if not 1 <= len(requests) <= 8:
            raise ValueError('DAP pipeline must contain 1..8 packets')
        if any(not 0 < len(request) <= self.packet_size for request in requests):
            raise ValueError('DAP packet exceeds negotiated capacity')
        if any(request[0] == 7 for request in requests):
            raise ValueError('TransferAbort has no response; use abort_transfer()')
        for request in requests:
            self.send(request)
        return [self.receive() for _ in requests]

    def send(self, request):
        self._send(request, pad=getattr(self, "pad", True))

    def _send(self, request, *, pad=True):
        if not 0 < len(request) <= self.packet_size: raise ValueError('DAP packet exceeds negotiated capacity')
        length = self.packet_size if pad else len(request)
        packet = C.create_string_buffer(bytes(request).ljust(length, b'\0'), length)
        count = C.c_uint32()
        self._check(self.usb.WinUsb_WritePipe(self.handle, self.ep_out, packet, length, C.byref(count), None))
        if count.value != length: raise RuntimeError('Short USB write; completion state unknown, do not retry')

    def receive(self):
        return self._receive(self.packet_size)

    def _receive(self, length):
        response = C.create_string_buffer(length)
        count = C.c_uint32()
        # A response that is an exact multiple of the 64-byte USB packet size is followed by a
        # zero-length packet (USB short-packet termination); skip those empty completions.
        for _ in range(4):
            self._check(self.usb.WinUsb_ReadPipe(self.handle, self.ep_in, response, length, C.byref(count), None))
            if count.value:
                return response.raw[:count.value]
        raise RuntimeError('Empty USB response')

    def close(self):
        if self.handle: self.usb.WinUsb_Free(self.handle); self.handle = None
        if self.file: self.kernel.CloseHandle(self.file); self.file = None

    def __enter__(self): return self
    def __exit__(self, *args): self.close()
