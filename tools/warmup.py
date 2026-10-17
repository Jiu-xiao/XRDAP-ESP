import sys
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
from winusb_dap import UsbClient
try:
    c = UsbClient(); c.close(); print('warm-up opened OK')
except Exception as e:
    print('warm-up failed as expected')
