# XRDAP-ESP

[中文](README.md)

A CMSIS-DAP debug probe built on ESP32-S3, ESP32-C3 or the classic ESP32, based on [libxr](https://github.com/xrobot-org/libxr). It works over a USB cable or over Wi-Fi. Keil, pyOCD, probe-rs and OpenOCD treat it as an ordinary CMSIS-DAP probe. The probe can also sample target variables at a fixed period, and a host tool plots the curves.

The host tool is in [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host): a single `xrdap` executable. Without arguments it starts the GUI; with a command it runs as a command-line tool.

## Choosing a board

| | ESP32-S3 | ESP32-C3 | Classic ESP32 |
|---|---|---|---|
| USB cable, for Keil, pyOCD, probe-rs, OpenOCD | Works directly (native USB probe) | Run `xrdap share usbip` on the computer first | Same as C3 (through the board's USB-to-serial chip) |
| Wi-Fi, for Keil, pyOCD, probe-rs, OpenOCD | Attach with USB/IP | Attach with USB/IP | Attach with USB/IP |
| Wi-Fi, for OpenOCD | OpenOCD TCP backend, direct | Same as S3 | Same as S3 |
| SWD clock | up to about 23.7 MHz | up to about 20 MHz | up to about 5 MHz (GPIO register bit-banding, limited by the peripheral bus) |
| Shortest probe-side sampling period (USB cable) | 4 variables 118 µs | 4 variables 300 µs | 8 variables 1 ms, 1 variable 200 µs |
| OpenOCD reading 128 KiB (USB cable) | 0.62 s | 1.8 s | 3.4 s |

The S3 is the fastest and simplest choice. A C3 or a classic ESP32 also works, with one extra step: `xrdap share`. The classic ESP32 has no native USB and no CPU-dedicated GPIO, so SWD and the serial link are one step slower; debugging and flashing work fine.

## 1. Setup

### Wiring

| Target | ESP32-S3, ESP32-C3 | Classic ESP32 |
|---|---|---|
| SWCLK | GPIO5 | GPIO18 |
| SWDIO | GPIO4 | GPIO19 |
| GND | GND | GND |

Connect one probe to one target. GPIO5 of the classic ESP32 is a strapping pin, so GPIO18 and GPIO19 are used instead; its serial link is on UART0 (GPIO1, GPIO3), which is the port of the on-board USB-to-serial chip, so a USB cable is enough. On the C3, use the chip's own USB port (D- on GPIO18, D+ on GPIO19). If the board's USB connector is wired directly to these two pins, plug into it. If the connector goes to a USB-to-serial chip (for example ESP32-C3-DevKitM-1), run a separate USB cable to the GPIO18, GPIO19, 5V and GND header pins.

The "Wireless DAP Lite" board (ESP32-S3 module, 2×5 header) uses its own image, `xrdap-esp32-s3-lite.bin`. SWCLK is GPIO37. SWDIO reaches the header through the on-board SGM3157 analog switch; the firmware holds the switch select pin GPIO36 low and drives and reads SWDIO on GPIO47. The board has no USB-to-serial chip, so only native USB and Wi-Fi are available. Use a module without Octal PSRAM (WROOM-1-N4 or N8, MINI-1-N4 or N8); modules with Octal PSRAM use GPIO35 to 37.

S3 status light (dev board: the RGB LED on GPIO48; Lite board: its WS2812). Holding SW2 on the Lite board for 5 seconds clears the credentials and restarts the probe. The dev board's BOOT key is for download mode only: it sits on the USB-to-serial chip's auto-download circuit, where the serial DTR/RTS lines can pull it low, so the running firmware does not read it. On the dev board, Wi-Fi is cleared with `xrdap wifi clear` or the console command `wifi clear` (2.6).

| Light | Meaning |
|---|---|
| One rainbow sweep | Power-up |
| Red | Wired: USB connected to a PC, or a command over USB or serial within 5 seconds |
| Blue | Wireless: a network debug client (OpenOCD TCP or USB/IP), or Wi-Fi only |
| Purple | Neither USB nor Wi-Fi connected; breathing while joining Wi-Fi |
| Main colour fading to purple and back every 2 seconds | The host reported connected through DAP_HostStatus |
| Red and blue alternating every second | The host reported the target running |
| Flash | Every DAP command (USB, serial or Wi-Fi) brightens the light, which then fades; steady traffic flickers at about 15 Hz |
| Fast purple blink | SW2 held on the Lite board; after 5 seconds it turns solid purple, the credentials are cleared and the probe restarts |
| Solid purple | The credentials were just changed (button, console or command 0x86); the probe restarts |

With both USB and Wi-Fi connected, wired wins and the light is red. When a host exits without DAP_Disconnect, 5 seconds without commands also returns the light to the main colour. The board's WS2812B runs from 5 V; its data high threshold is 0.65 × 5 V = 3.25 V, so 3.3 V drive is just enough and an individual LED may stay dark or show the wrong colour; debugging is not affected.

### Flashing the firmware

Open the [web flasher](https://jiu-xiao.github.io/XRDAP-ESP/) in desktop Chrome or Edge, choose the board and press Flash firmware. From the command line instead, download `xrdap-esp32-s3.bin`, `xrdap-esp32-s3-lite.bin` (Lite board), `xrdap-esp32-c3.bin` or `xrdap-esp32.bin` from [Releases](../../releases) (checksums are in `SHA256SUMS` on the same page) and flash it at address 0:

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
python -m esptool --chip esp32c3 write_flash 0x0 xrdap-esp32-c3.bin
python -m esptool --chip esp32 write_flash 0x0 xrdap-esp32.bin
```

Upgrades use the same command. The Wi-Fi settings are kept. The dev board's BOOT key is only needed for flashing. S3 images of v0.5.1 and earlier have an 8MB flash header and reboot in a loop on boards with 4MB flash (N4, some Lite boards); with those older images add `--flash_size 4MB` after `write_flash`. If esptool reports `Wrong boot mode` and cannot enter download mode (on some boards the auto-download circuit can only reset and cannot pull GPIO0 low), hold BOOT, press RESET (EN) once, release BOOT, and then add `--before no_reset` to the command.

The Lite board has neither a reset button nor a USB-to-serial chip, so it enters download mode by hand before flashing: unplug USB, hold SW1 (the button on GPIO0) while plugging it back in, then release it. In download mode the PC sees the native USB serial port (`303A:1001`); run the command above. After flashing, esptool resets the chip and the new firmware starts.

### Installing the host tool

Download `xrdap` for your platform from [XRDAP-Host Releases](https://github.com/Jiu-xiao/XRDAP-Host/releases). With the probe plugged in, run:

```
xrdap list
```

If the probe is listed, the firmware and the connection are fine.

## 2. Usage by scenario

The OpenOCD examples below use an STM32F401 target. For another chip, replace `target/stm32f4x.cfg`.

### 2.1 S3 over USB, with Keil, pyOCD, probe-rs, OpenOCD

Plug into the S3's native USB port (the one marked USB on the board, not the serial bridge). It is a CMSIS-DAP v2 probe and needs no driver on Windows.

```
openocd -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg
pyocd list
probe-rs list
```

Keil: select CMSIS-DAP Debugger under Debug, choose SW as the port in Settings, and an IDCODE in the SW Device field means the connection works. Start with Max Clock at 4 MHz.

### 2.2 C3 or classic ESP32 over USB, with Keil, pyOCD, probe-rs, OpenOCD

The C3's USB port is a fixed-function USB Serial/JTAG, and the classic ESP32's USB port goes to a USB-to-serial chip; either appears as a serial port on the computer. `xrdap share usbip` turns it into a USB probe (replace `COMx` with the actual port; on Linux it is `/dev/ttyACM0` or `/dev/ttyUSB0`):

```
xrdap -p COMx share usbip
```

Then attach it in another terminal:

```
usbip attach -r 127.0.0.1 -b 1-1
```

`XR-DAP-C3 CMSIS-DAP` (`XR-DAP-ESP32 CMSIS-DAP` for the classic ESP32) then appears in the system and is used as in 2.1. Attaching needs a USB/IP client. Linux has one in the kernel (`sudo modprobe vhci-hcd`, and prefix the commands with `sudo`). On Windows, install [usbip-win2](https://github.com/vadimgrn/usbip-win2). To avoid a terminal, connect to the probe in the `xrdap` GUI and tick 共享为 USB DAP (share as USB DAP). The effect is the same.

### 2.3 Over Wi-Fi, with Keil, pyOCD, probe-rs, OpenOCD

Set up Wi-Fi first as described in 2.6. The probe serves USB/IP on port 3240. Attach directly; `xrdap` is not needed:

```
usbip list -r xrdap-xxxxxx.local
usbip attach -r xrdap-xxxxxx.local -b 1-1
```

`xrdap-xxxxxx.local` is the probe's mDNS host name (`xxxxxx` is the last 6 hex digits of the MAC). An IP address also works, and `xrdap list` shows both. After attaching, use the tools as in 2.1. When finished, run `usbip detach -p 0`.

### 2.4 Over Wi-Fi, OpenOCD direct

OpenOCD can also skip USB/IP and connect to port 3241 of the probe with its CMSIS-DAP TCP backend:

```
openocd -c "adapter driver cmsis-dap; cmsis-dap backend tcp; cmsis-dap tcp host xrdap-xxxxxx.local; cmsis-dap tcp port 3241; transport select swd" -f target/stm32f4x.cfg
```

The TCP backend is not in an official OpenOCD release yet. See "OpenOCD version" in section 3.

### 2.5 Plotting variables and changing parameters

See [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host). Open `xrdap`, select an ELF, add variables by name and start sampling. The probe samples at a fixed period, which is not limited by USB or Wi-Fi round-trip latency.

### 2.6 Configuring Wi-Fi and changing the hotspot

With the probe connected to the computer (by USB or serial), connect to it in the `xrdap` GUI, expand "Wi-Fi", fill in the SSID and password and click "保存并重启" (save and restart); or use the command line:

```
xrdap wifi set lab password
xrdap wifi status
```

Replace `lab` and `password` with the hotspot SSID and its password. After saving, the probe restarts in about 0.5 seconds and connects to the hotspot. To change the hotspot, run `set` again. `xrdap wifi clear` removes the settings, and the probe no longer connects to Wi-Fi. If the hotspot goes away, the probe keeps trying to reconnect.

The credentials live in NVS. They are removed by `wifi clear`, the 0x86 clear command, a long press of SW2 on the Lite board, and the erase and rebuild the probe does at boot when the NVS partition is damaged (full, or a different format version). Every removal records its source. The console command `wifi status` and the 0x86 status reply show the source of the last clear and the cause of the last reset (power-on, software restart, watchdog, panic, brown-out), which tells why credentials are gone.

Without `xrdap`, a serial terminal also works: use the extra virtual serial port that appears when the S3 is plugged in by USB, or the C3's USB port (the terminal must not touch DTR/RTS). Enter `wifi set lab password`, `wifi status`, `wifi clear` or `help`.

```
python -m serial.tools.miniterm COMx 115200 --dtr 0 --rts 0
```

## 3. Reference

### Connection overview

| Connection | S3 | C3 | Classic ESP32 | Used by |
|---|---|---|---|---|
| Native USB (CMSIS-DAP v2) | yes | no | no | All tools |
| Serial link on the chip's USB port | no | yes | no | `xrdap`; `xrdap share` passes it on to other tools |
| On-board USB-to-serial (UART0) | yes, 4 Mbaud | no | yes, 921600 | Same as above |
| Wi-Fi, USB/IP, port 3240 | yes | yes | yes | All tools after attaching |
| Wi-Fi, OpenOCD TCP backend, port 3241 | yes | yes | yes | OpenOCD, `xrdap` |

`xrdap` picks the baud rate from the serial chip: CH343 and similar try 4 Mbaud first, while CP2102 and CH340 use 921600, so `--baud` need not be given.

All connections share one DAP, and one connection owns the probe at a time: the first connection that sends a command other than DAP_Info becomes the owner, and commands from the others are answered busy (DAP_Info is still answered) until the owner sends DAP_Disconnect, its TCP connection closes, the USB bus resets (unplug or host reset), or the serial link has been silent for 5 seconds. The Wi-Fi ports 3240 and 3241 accept one client at a time: while one is connected, the other waits. If a client does not disconnect cleanly, the probe releases it after about 11 seconds.

### Finding the probe

- Serial number: `XRobot-XRDAP-` plus the 12 uppercase hex digits of the factory MAC, for example `XRobot-XRDAP-0123456789AB`. The USB descriptor, DAP_Info and mDNS all carry this value. Use it to tell several probes apart (`adapter serial` in OpenOCD, `-u` in pyOCD, `--probe` in probe-rs).
- mDNS: host name `xrdap-` plus the last 3 bytes of the MAC in lowercase (`xrdap-6789ab.local` in the example above), service `_xrdap._tcp`, TXT records `serial`, `chip`, `fw`.
- `xrdap list` lists probes on USB, serial and the LAN, with the connections of one probe grouped together.

### USB/IP clients

| System | Install | Attach | Detach |
|---|---|---|---|
| Linux | In the kernel: `sudo modprobe vhci-hcd`; on Ubuntu the `usbip` command is in the `linux-tools-generic` package | `sudo usbip attach -r <address> -b 1-1` | `sudo usbip detach -p 0` |
| Windows | [usbip-win2](https://github.com/vadimgrn/usbip-win2) | `usbip attach -r <address> -b 1-1` | `usbip detach -p 0` |

On Linux, add a udev rule so that a normal user can open the device:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="6199", MODE="0666"
```

The virtual device has VID:PID 1D50:6199 and bcdDevice 0x0200. The S3 native USB uses 0x0100. They differ because Windows caches descriptors by these three values.

### OpenOCD version

- USB (2.1) and USB/IP (2.2, 2.3) use OpenOCD's USB backend. The official release 0.12.0 is enough.
- The CMSIS-DAP TCP backend in 2.4 is not in the official release 0.12.0. Build it from master. On Windows, master before commit e89e73839d53 has a socket handling bug: it treats "no data yet" as an error, waits more than ten seconds at start-up and floods the log with `error reading header`. Use a version that includes this commit.

### Drivers on Windows

Windows sometimes reinstalls the driver of the S3 interface and loses the device interface GUID. Then no tool can open the probe. Remove the device from an administrator PowerShell and plug it in again:

```
pnputil /remove-device "USB\VID_1D50&PID_6199\<序列号>" /subtree
```

Replace `<序列号>` (serial number) with the probe's serial number.

### Performance

Typical values; real results vary with the target chip and the wiring. S3:

| Item | Result |
|---|---|
| USB block read / block write (12 MHz) | about 970 / 780 KiB/s |
| OpenOCD reading 128 KiB over USB (4 MHz) | 0.62 s |
| OpenOCD flashing 16 KiB with verify over USB (4 MHz) | 1.09 s |
| Smallest command round trip over Wi-Fi | about 1.3 ms |
| Probe-side sampling of 8 variables, 2 ms period, 5 minutes continuous | 149999 frames, no lost frames, no skipped ticks, timestamp deviation -27 to +12 µs |

Shortest probe-side sampling period (4-byte variables, no skipped ticks or lost frames over 2 seconds), SWD about 23.7 MHz:

| Variables | USB | Serial | Wi-Fi |
|---|---|---|---|
| 1 | 86 µs | 86 µs | 118 µs |
| 8 | 167 µs | 220 µs | 251 µs |
| 32 | 604 µs | 681 µs | 608 µs |

OpenOCD reading 128 KiB (4 MHz requested):

| Path | S3 | C3 |
|---|---|---|
| Wi-Fi, TCP backend | 1.6 s | 1.9 s |
| Wi-Fi, USB/IP | 2.1 s | 2.7 s |
| C3 over USB, `xrdap share usbip` | - | 1.8 s |

The C3 has a single core, so Wi-Fi traffic and sampling run on the same core. Sampling 4 variables over Wi-Fi at 1 ms skips no ticks. At 500 µs about 0.2% of ticks are skipped.

Classic ESP32 (serial at 921600 baud): reads and writes are correct for SWD clock requests from 1 to 40 MHz. The GPIO registers are on the peripheral bus (about 12 CPU cycles per write, 17 per read), so any request above 5 MHz runs at full speed, and `xrdap info` reports about 30 µs per access (about 150 SWCLK cycles, i.e. just under 5 MHz). Probe-side sampling of 8 variables at 1 ms and of 1 variable at 200 µs skips no ticks and loses no frames. At shorter periods the probe still skips no ticks, but the 921600 serial link cannot carry all the data and the buffer starts dropping frames. Through `xrdap share usbip`, OpenOCD reads 128 KiB in 3.4 s. Over Wi-Fi, 4 variables at 500 µs, 8 variables at 600 µs and 1 variable at 150 µs lose no frames (at 150 µs a tick is skipped now and then); OpenOCD reads 128 KiB in 3.2 s through a USB/IP mount and in 2.3 s through the TCP backend.

### SWD clock and wiring

On the S3, SWCLK high and low times are equal and the frequency is computed in CPU cycles; a request of 25 MHz or more uses the fastest setting, about 23.7 MHz. The C3 and the classic ESP32 use libxr's generic GPIO bit-banging; libxr turns the requested frequency into delay-loop counts with `kSwdTiming` in `config.hpp` (CPU cycles per SWCLK period), and the clock lands within half a step of the request. The usable speed depends on wire length, pull-ups on the target board and routing. Start with 4 MHz. If errors appear at a high clock, check the SWCLK and SWDIO waveforms first, shorten the wires or add series resistors. The pin drive strength is set to 1 (IDF default is 2) to reduce overshoot and ringing.

### Building from source

ESP-IDF v5.5 is required. libxr is the `third_party/libxr` submodule; the vendor extension, the status light rule and the probe identity come from [XRDAP-Core](https://github.com/Jiu-xiao/XRDAP-Core), the `third_party/xrdap-core` submodule.

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP.git
cd XRDAP-ESP/firmware/esp32s3      # for the C3 use firmware/esp32c3, for the classic ESP32 firmware/esp32
idf.py build
idf.py -p COMx flash
```

`idf.py build merge-bin` produces the same merged image as the release, `build/merged-binary.bin`. The Lite board image adds one more configuration layer in `firmware/esp32s3`:

```
idf.py -B build-lite -D SDKCONFIG=build-lite/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.lite" build merge-bin
```

The board can also be chosen in `idf.py menuconfig` under XR-DAP → Board.

The pure logic (console, 0x86, TCP framing, USB/IP and the status light inputs) does not depend on ESP-IDF; `firmware/tests` runs its unit tests on a PC (CMake and a C++17 compiler, GCC or MSVC):

```
cmake -S firmware/tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests
```

| Directory | Content |
|---|---|
| `firmware/esp32s3`, `firmware/esp32c3`, `firmware/esp32` | Per-chip projects: `main.cpp`, `config.hpp` (pins, cores, priorities; the S3 picks pins by board), and the S3 SWD engine |
| `firmware/common/net` | Wi-Fi and credential storage, 0x86 Wi-Fi command, OpenOCD TCP server, USB/IP virtual device, mDNS |
| `firmware/common/serial`, `console` | Serial DAP link, C3 serial splitting, Wi-Fi command line |
| `firmware/common/probe` | Parameters, probe identity, start-up steps, restart, status light inputs, the sampling task and its gptimer, shared by all three chips |
| `firmware/tests` | Unit tests that run on a PC |
| `firmware/common/swd` | S3 and C3 dedicated-GPIO pins, classic ESP32 GPIO-register pins |
| `third_party/xrdap-core` | XR vendor extension, status light rule, probe identity and protocol document ([XRDAP-Core](https://github.com/Jiu-xiao/XRDAP-Core)) |

Every push builds the four firmware images (S3, S3 Lite, C3, classic ESP32), runs the unit tests and checks formatting (clang-format 21.1.8) in GitHub Actions. A `v*` tag publishes a release. The `pages` workflow publishes the web flasher from the newest release images and the page in `web/` on main, after a tagged build finishes or when `web/` changes (`web/make_site.py` writes one ESP Web Tools manifest per board from `web/boards.json`).

### Vendor extension

Besides the standard CMSIS-DAP commands, the probe implements 0x80 to 0x86: reading and writing several addresses at once, reading variables along a pointer chain, sampling at a fixed period on the probe with the host fetching the results in batches, and configuring Wi-Fi through DAP. While sampling, the probe accepts only DAP_Info, DAP_HostStatus and this command group, so sampling and OpenOCD debugging cannot run at the same time. The formats are in [docs/protocol.en.md](https://github.com/Jiu-xiao/XRDAP-Core/blob/main/docs/protocol.en.md) of XRDAP-Core.

## License

Apache-2.0, see [LICENSE](LICENSE).
