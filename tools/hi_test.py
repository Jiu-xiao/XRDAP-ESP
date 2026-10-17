import subprocess, sys, os, time
OCD = r'C:\Users\a2592\.espressif\tools\openocd-esp32\v0.12.0-esp32-20250707\openocd-esp32\bin\openocd.exe'
SCR = r'C:\Users\a2592\.espressif\tools\openocd-esp32\v0.12.0-esp32-20250707\openocd-esp32\share\openocd\scripts'
FT = 'D:/Codex/tmp/xrdap-s3/flashtest/'
speed = int(sys.argv[1]); n = int(sys.argv[2])
HEAD = f'''adapter driver cmsis-dap
cmsis-dap vid_pid 0x303a 0x4a01
transport select swd
source [find target/stm32g4x.cfg]
adapter speed {speed}
init
catch {{halt}}
poll
mmw 0xE0042008 0x1800 0
'''
def run(body, tag):
    cfg = f'D:/Codex/tmp/xrdap-s3/hi_{tag}.cfg'
    open(cfg, 'w').write(HEAD + body + '\nshutdown\n')
    t = time.time()
    p = subprocess.run([OCD, '-s', SCR, '-f', cfg], capture_output=True, text=True, timeout=120)
    return p.stdout + p.stderr, time.time() - t

ok = 0
for i in range(n):
    img = 'g431_flash_a.bin' if i % 2 == 0 else 'g431_flash_b.bin'
    out, dt = run(f'set r [catch {{program {FT}{img} 0x08000000 verify}} m]\necho "PROGRAM RESULT: $r"\n', 'p')
    good = 'PROGRAM RESULT: 0' in out
    ok += good
    if not good:
        errs = [l for l in out.splitlines() if 'rror' in l or 'ack' in l.lower() or 'Failed' in l][:3]
        print(f'  run {i} FAIL {errs}')
print(f'program+verify {speed} kHz: {ok}/{n}')
# dump, compare against the last programmed image
last = 'g431_flash_a.bin' if (n - 1) % 2 == 0 else 'g431_flash_b.bin'
out, dt = run(f'set t0 [clock milliseconds]\ndump_image {FT}dump_hi.bin 0x08000000 0x20000\necho "DUMP_MS: [expr {{[clock milliseconds]-$t0}}]"\n', 'd')
ms = [l for l in out.splitlines() if 'DUMP_MS' in l]
ref = open(FT.replace('/', '\\') + last, 'rb').read()
try:
    got = open(FT.replace('/', '\\') + 'dump_hi.bin', 'rb').read()[:len(ref)]
    same = got == ref
except Exception as e:
    same = f'no dump ({e})'
print('dump', ms, f'KiB/s ~ {128000/float(ms[0].split(":")[-1].strip()) if ms else 0:.0f}' if ms else '', 'matches image:', same)
