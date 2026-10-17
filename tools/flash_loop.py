import subprocess, time, sys
OCD = r'C:\Users\a2592\.espressif\tools\openocd-esp32\v0.12.0-esp32-20250707\openocd-esp32\bin\openocd.exe'
SCR = r'C:\Users\a2592\.espressif\tools\openocd-esp32\v0.12.0-esp32-20250707\openocd-esp32\share\openocd\scripts'
FT = 'D:/Codex/tmp/xrdap-s3/flashtest/'
n = int(sys.argv[1]); t0 = time.time()
cfg = 'D:/Codex/tmp/xrdap-s3/fl_{}.cfg'
for i in range(n):
    img = 'rnd1.bin' if i % 2 == 0 else 'rnd2.bin'
    open(cfg.format('x'), 'w').write(f'''adapter driver cmsis-dap
cmsis-dap vid_pid 0x303a 0x4a01
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
init
catch {{halt}}
poll
mmw 0xE0042008 0x1800 0
set r [catch {{program {FT}{img} 0x08000000 verify}} m]
echo "PROGRAM RESULT: $r"
shutdown
''')
    s = time.time()
    try:
        p = subprocess.run([OCD, '-s', SCR, '-f', cfg.format('x')], capture_output=True, text=True, timeout=60)
        out = p.stdout + p.stderr
        res = 'OK' if 'PROGRAM RESULT: 0' in out else 'FAIL ' + ' | '.join([l for l in out.splitlines() if 'rror' in l][:2])[:150]
    except subprocess.TimeoutExpired:
        res = 'TIMEOUT (openocd killed)'
    print(f'[{time.time()-t0:5.1f}s] run {i} {img}: {res} ({time.time()-s:.1f}s)', flush=True)
    time.sleep(0.5)
print('DONE', flush=True)
