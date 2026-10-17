# ESP32-S3 handoff (written 2026-10-16, by the XR-DAP bring-up session)

I have released the board: no process of mine holds COM108 or the USB interface. The check opened and
closed COM108 once, which resets the ESP32 through the CH343 DTR/RTS lines; it came back as
`CMSIS-DAP v2` + `COM33`.

## Board and wiring
- ESP32-S3 N8R8 (8 MB flash), MAC e8:06:90:93:ef:1c. Two USB ports: CH343 UART = **COM108** (flash/log, opening it
  resets the chip), native USB (OTG) = the XR-DAP device (VID:PID 303A:4A01, serial XRDAP-S3-0001, COM33 for the CDC).
- SWD: GPIO5 = SWCLK, GPIO4 = SWDIO, common ground. Target STM32G431KB (DPIDR 2ba01477, CPUID 410fc241).
  The target runs an IWDG: halt the core and set `DBGMCU_APB1FZR1 |= 0x1800` before any long measurement.
- Bare Dupont wires, push-pull SWCLK/SWDIO, internal pull-up on SWDIO. A logic-analyzer probe on SWCLK/SWDIO is
  enough to break SWD at ~12.5 MHz; without probes 12.5 MHz works. 33 ohm series resistors + external 10k were not tried.

## What is on the board now
`D:\Codex\tmp\xrdap-s3\build-dbg`: USB-only (`XRDAP_WIFI_ENABLE=0`) + debug state print every 500 ms on UART
(`dapdbg:` line: response queue, endpoint states, EP1 registers), legal endpoint layout, built against
`libxr-c` (HEAD 52862a09) **plus my three uncommitted patches** (`libxr-patches\`, includes the ZLP fix).

## Flash backups (originals)
- `D:\Codex\tmp\rml\backup\s3-orig-8MB.bin` (read before I flashed anything).
- `D:/Codex/tmp/xrdap-s3-c/backup/s3_full_20261016.bin` (the other session's).
- NVS holds the Wi-Fi credentials for the lab AP `XR-LAB` (namespace `rmlink`, blob `wifi`, written earlier by the user
  with RMLink's provisioning). The Wi-Fi build reads them; never print them. Flashing with `flash_args` does not touch NVS.

## Firmware images (all in `D:\Codex\tmp\xrdap-s3`, app source `app-c\main`)
| build dir | what |
|---|---|
| `build-c` | production: USB at boot, Wi-Fi up -> USB deinit -> CMSIS-DAP over TCP 3241 |
| `build-usb` | USB-only, no Wi-Fi |
| `build-dbg` | USB-only + state print (needs `-fira-region=one`, see below) |
| `build-ct` / `build-lt` / `build-lb` | logic-analyzer clock test / loop-timing / on-device DAP benchmark |
Build: `idf-c.ps1`, `idf-usb.ps1`, `idf-dbg.ps1`, `idf-ct.ps1`, `idf-lt.ps1`, `idf-lb.ps1` (IDF 5.5.2, PowerShell).
Flash: `python -m esptool --chip esp32s3 --port COM108 -b 460800 --before default_reset --after hard_reset write_flash @flash_args`
from the build dir. RMLink images (`rml\b*`, `rml\bt*`) are the upstream baseline builds.

## Things that bit me
- GCC 14.2 sometimes dies with an internal compiler error (`ira` pass) in `SwdGeneralGPIO::Transfer`; `-fira-region=one`
  avoids it; so does keeping the DAP command path out of large inlined functions (`noinline` helper).
- `libxr-c` now needs the EP0 buffer >= 128 bytes (two halves) and endpoint buffers 64-byte aligned; IN endpoints only EP1..EP4 on S3.
- Dedicated GPIO instructions work only on the core that created the bundle (core 0 here); the TCP task must run on core 0.
- USB stalls (device-side) recover with `esptool read_mac` (hard reset), no replug. Host-side wedges under the old illegal layout needed a replug.
- A client that zero-pads every request to 1024 B halves wired throughput; `winusb_dap.py` has `c.pad = False`.
- The Windows WinUSB client `D:\Codex\tmp\xrdap-s3\tools\winusb_dap.py` skips the empty ZLP completions.
- Lab Pi: `ssh lab3b` (hostapd `XR-LAB`, 10.43.0.0/24); my scripts `swdtcp.py`, `swdbench.py` are in `/tmp` (tmpfs, lost on reboot);
  copies in `D:\Codex\tmp\rml`. The ESP gets 10.43.0.168 in the Wi-Fi build.

## Numbers so far (details: `D:\Codex\tmp\rml\results-20261016.md`)
- On-device (no transport), 12.5 MHz natural clock: block read 1071 KiB/s, scattered 84 words 59990 words/s.
- End to end: USB block read 438-452 KiB/s (unpadded, 4 commands in flight), USB scatter 37.7k words/s, Wi-Fi scatter 28.3k words/s.
- RMLink baseline on the same board: scatter 36.5k words/s at "10 MHz" over Wi-Fi.

## Open items
1. End-to-end is well below on-device (452 vs 1071 KiB/s over USB, 28k vs 60k words/s over Wi-Fi): not yet explained. Candidates: no overlap
   between command execution and response sending in the device; Python client overhead; Wi-Fi RX/SWD/TX run in one task.
2. Not measured with OpenOCD/pyOCD/Keil at all.
3. Late Wi-Fi connection (after the 20 s wait) does not switch from USB to TCP.
4. Gap 5.3-12.5 MHz in the cycle-paced SWCLK (spin loop has a floor of ~8 cycles per edge); no clock ceiling for requests above 12.5 MHz.
5. libxr changes are uncommitted in `D:/Codex/tmp/usb-impl-20261014/libxr-c` (other session's worktree; my three patches are in it).
