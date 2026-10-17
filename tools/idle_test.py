import sys
sys.path.insert(0, r'D:/Codex/tmp/xrdap-s3/tools')
from winusb_dap import UsbClient
def x(c, h): return c.exchange(bytes.fromhex(h)).hex()
with UsbClient() as c:
    for idle in (0, 1, 2, 4):
        print('--- idle', idle)
        print(x(c, '1100'.replace('00', '00')) if False else '')
        x(c, '1140420f00'); x(c, '0201'); x(c, '1140420f00')
        x(c, '04%02x96000000' % idle)       # TransferConfigure idle, wait retry 0x0096, match retry 0
        x(c, '1300'); x(c, '010001'); x(c, '010100')
        x(c, '1233ffffffffffffff'); x(c, '12109ee7'); x(c, '1233ffffffffffffff'); x(c, '120800')
        print('DPIDR      ', x(c, '0600010002'))   # DAP_Transfer count1: DP read IDCODE
        print('ABORT+seq  ', x(c, '050004001e00000008000000000400' + '0f005006'))
