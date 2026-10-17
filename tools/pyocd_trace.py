import sys
from pyocd.probe.pydapaccess.interface import pyusb_v2_backend as b
cls = b.PyUSBv2
ow, orr = cls.write, cls.read
def w(self, data):
    print('OUT', len(data), bytes(data[:24]).hex(), flush=True); return ow(self, data)
def r(self):
    d = orr(self); print('IN ', len(d), bytes(d[:24]).hex(), flush=True); return d
cls.write, cls.read = w, r
from pyocd.__main__ import PyOCDTool
sys.argv = ['pyocd'] + sys.argv[1:]
PyOCDTool().run()
