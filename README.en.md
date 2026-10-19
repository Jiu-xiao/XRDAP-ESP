# XRDAP-ESP

[中文](README.md)

A CMSIS-DAP debug probe built on ESP32-S3 or ESP32-C3, based on [libxr](https://github.com/xrobot-org/libxr). It works over a USB cable or over Wi-Fi. Keil, pyOCD, probe-rs and OpenOCD treat it as an ordinary CMSIS-DAP probe. The probe can also sample target variables at a fixed period, and a host tool plots the curves. Tested on STM32G431KB and STM32F401RC.

The host tool is in [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host): a single `xrdap` executable. Without arguments it starts the GUI; with a command it runs as a command-line tool.

## Choosing a board

| | ESP32-S3 | ESP32-C3 |
|---|---|---|
| USB cable, for Keil, pyOCD, probe-rs, OpenOCD | Works directly (native USB probe) | Run `xrdap share usbip` on the computer first |
| Wi-Fi, for Keil, pyOCD, probe-rs, OpenOCD | Attach with USB/IP | Attach with USB/IP |
| Wi-Fi, for OpenOCD | OpenOCD TCP backend, direct | Same as S3 |
| Maximum SWD clock | about 23.7 MHz | about 20 MHz |
| Shortest probe-side sampling period for 4 variables (USB) | 118 µs | 300 µs |

The S3 is the fastest and simplest choice. A C3 also works fully, with one extra step: `xrdap share`.

## 1. Setup

### Wiring

| Probe | Target |
|---|---|
| GPIO5 | SWCLK |
| GPIO4 | SWDIO |
| GND | GND |

The pins are the same on both chips. Connect one probe to one target. On the C3, use the chip's own USB port (D- on GPIO18, D+ on GPIO19). If the board's USB connector is wired directly to these two pins, plug into it. If the connector goes to a USB-to-serial chip (for example ESP32-C3-DevKitM-1), run a separate USB cable to the GPIO18, GPIO19, 5V and GND header pins.

### Flashing the firmware

Download `xrdap-esp32-s3.bin` or `xrdap-esp32-c3.bin` from [Releases](../../releases) (checksums are in `SHA256SUMS` on the same page) and flash it at address 0:

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
python -m esptool --chip esp32c3 write_flash 0x0 xrdap-esp32-c3.bin
```

Upgrades use the same command. The Wi-Fi settings are kept. If esptool cannot enter download mode, hold BOOT and press RESET once.

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

Keil: select CMSIS-DAP Debugger under Debug, choose SW as the port in Settings, and an IDCODE in the SW Device field means the connection works. Start with Max Clock at 4 MHz. OpenOCD and probe-rs were tested; pyOCD was not.

### 2.2 C3 over USB, with Keil, pyOCD, probe-rs, OpenOCD

The C3's USB port is a fixed-function USB Serial/JTAG, which appears as a serial port on the computer. `xrdap share usbip` turns it into a USB probe (replace `COM7` with the actual port; on Linux it is `/dev/ttyACM0`):

```
xrdap -p COM7 share usbip
```

Then attach it in another terminal:

```
usbip attach -r 127.0.0.1 -b 1-1
```

`XR-DAP-C3 CMSIS-DAP` then appears in the system and is used as in 2.1. Attaching needs a USB/IP client. Linux has one in the kernel (`sudo modprobe vhci-hcd`, and prefix the commands with `sudo`). On Windows, install [usbip-win2](https://github.com/vadimgrn/usbip-win2). To avoid a terminal, connect to the probe in the `xrdap` GUI and tick 共享为 USB DAP (share as USB DAP). The effect is the same.

### 2.3 Over Wi-Fi, with Keil, pyOCD, probe-rs, OpenOCD

Set up Wi-Fi first as described in 2.6. The probe serves USB/IP on port 3240. Attach directly; `xrdap` is not needed:

```
usbip list -r xrdap-721384.local
usbip attach -r xrdap-721384.local -b 1-1
```

`xrdap-721384.local` is the probe's mDNS host name (`xrdap-` plus the last 6 digits of the MAC). An IP address also works, and `xrdap list` shows both. After attaching, use the tools as in 2.1. When finished, run `usbip detach -p 0`.

### 2.4 Over Wi-Fi, OpenOCD direct

OpenOCD can also skip USB/IP and connect to port 3241 of the probe with its CMSIS-DAP TCP backend:

```
openocd -c "adapter driver cmsis-dap; cmsis-dap backend tcp; cmsis-dap tcp host xrdap-721384.local; cmsis-dap tcp port 3241; transport select swd" -f target/stm32f4x.cfg
```

The TCP backend is not in an official OpenOCD release yet. See "OpenOCD version" in section 3.

### 2.5 Plotting variables and changing parameters

See [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host). Open `xrdap`, select an ELF, add variables by name and start sampling. The probe samples at a fixed period, which is not limited by USB or Wi-Fi round-trip latency.

### 2.6 Configuring Wi-Fi and changing the hotspot

With the probe connected to the computer (by USB or serial):

```
xrdap wifi set 热点名 密码
xrdap wifi status
```

Replace 热点名 (hotspot name) and 密码 (password) with the hotspot SSID and its password. After saving, the probe restarts in about 0.5 seconds and connects to the hotspot. To change the hotspot, run `set` again. `xrdap wifi clear` removes the settings, and the probe no longer connects to Wi-Fi. If the hotspot goes away, the probe keeps trying to reconnect.

Without `xrdap`, a serial terminal also works: use the extra virtual serial port that appears when the S3 is plugged in by USB, or the C3's USB port (the terminal must not touch DTR/RTS). Enter `wifi set 热点名 密码`, `wifi status`, `wifi clear` or `help`.

```
python -m serial.tools.miniterm COM7 115200 --dtr 0 --rts 0
```

## 3. Reference

### Connection overview

| Connection | S3 | C3 | Used by |
|---|---|---|---|
| Native USB (CMSIS-DAP v2) | yes | no | All tools |
| Serial link on the chip's USB port | no | yes | `xrdap`; `xrdap share` passes it on to other tools |
| On-board USB-to-serial (UART0, 4 Mbaud) | yes | no | Same as above |
| Wi-Fi, USB/IP, port 3240 | yes | yes | All tools after attaching |
| Wi-Fi, OpenOCD TCP backend, port 3241 | yes | yes | OpenOCD, `xrdap` |

All connections share one DAP and serve one client at a time. Ports 3240 and 3241 also exclude each other: while one is connected, the other waits. While a network client is connected, the S3's native USB is temporarily off and comes back after disconnect. If a client does not disconnect cleanly, the probe releases it after about 11 seconds.

### Finding the probe

- Serial number: `XRobot-XRDAP-` plus the 12 uppercase hex digits of the factory MAC, for example `XRobot-XRDAP-E8069093EF1C`. The USB descriptor, DAP_Info and mDNS all carry this value. Use it to tell several probes apart (`adapter serial` in OpenOCD, `-u` in pyOCD, `--probe` in probe-rs).
- mDNS: host name `xrdap-` plus the last 3 bytes of the MAC in lowercase (`xrdap-93ef1c.local` in the example above), service `_xrdap._tcp`, TXT records `serial`, `chip`, `fw`.
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
- The CMSIS-DAP TCP backend in 2.4 is not in any official release (the latest, 0.12.0, came out in 2023). Build it from master. On Windows, master before commit e89e73839d53 (2026-08-15) has a socket handling bug: it treats "no data yet" as an error, waits more than ten seconds at start-up and floods the log with `error reading header`. Use a version that includes this commit.

### Drivers on Windows

Windows sometimes reinstalls the driver of the S3 interface and loses the device interface GUID. Then no tool can open the probe. Remove the device from an administrator PowerShell and plug it in again:

```
pnputil /remove-device "USB\VID_1D50&PID_6199\<序列号>" /subtree
```

Replace `<序列号>` (serial number) with the probe's serial number.

### Performance

S3, target STM32G431KB, jumper wires:

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

OpenOCD reading 128 KiB from an STM32F401 (Linux, 4 MHz requested):

| Path | S3 | C3 |
|---|---|---|
| Wi-Fi, TCP backend | 1.6 s | 1.9 s |
| Wi-Fi, USB/IP | 2.1 s | 2.7 s |
| C3 over USB, `xrdap share usbip` | - | 1.8 s |

The C3 has a single core, so Wi-Fi traffic and sampling run on the same core. Sampling 4 variables over Wi-Fi at 1 ms skips no ticks. At 500 µs about 0.2% of ticks are skipped.

### SWD clock and wiring

SWCLK high and low times are equal, and the frequency is computed in CPU cycles (4 MHz measured 4.03 MHz on the S3; a request of 25 MHz or more uses the fastest setting, about 23.7 MHz, not measured with an oscilloscope). The C3 frequency correction value has not been calibrated with a logic analyzer. The usable speed depends on wire length, pull-ups on the target board and routing. Start with 4 MHz. If errors appear at a high clock, check the SWCLK and SWDIO waveforms first, shorten the wires or add series resistors. The pin drive strength is set to 1 (IDF default is 2) to reduce overshoot and ringing.

### Building from source

ESP-IDF v5.5 is required. libxr is the `third_party/libxr` submodule.

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP.git
cd XRDAP-ESP/firmware/esp32s3      # for the C3 use firmware/esp32c3
idf.py build
idf.py -p COM3 flash
```

`idf.py build merge-bin` produces the same merged image as the release, `build/merged-binary.bin`.

| Directory | Content |
|---|---|
| `firmware/esp32s3`, `firmware/esp32c3` | Per-chip projects: `main.cpp`, `config.hpp` (pins, cores, priorities), and the S3 SWD engine |
| `firmware/common/vendor` | XR vendor extension: protocol, MEM-AP access, sampling scheduler, frame buffer, 0x86 Wi-Fi command |
| `firmware/common/net` | Wi-Fi, OpenOCD TCP server, USB/IP virtual device, mDNS |
| `firmware/common/serial`, `console` | Serial DAP link, C3 serial splitting, Wi-Fi command line |
| `firmware/common/probe` | Parameters, probe identity and start-up steps shared by both chips |
| `docs/protocol.en.md` | Serial frame, TCP frame, vendor commands, probe identity, mDNS, USB/IP device |

Every push builds both firmware images and checks formatting (clang-format 21.1.8) in GitHub Actions. A `v*` tag publishes a release automatically.

### Vendor extension

Besides the standard CMSIS-DAP commands, the probe implements 0x80 to 0x86: reading and writing several addresses at once, reading variables along a pointer chain, sampling at a fixed period on the probe with the host fetching the results in batches, and configuring Wi-Fi through DAP. While sampling, the probe accepts only DAP_Info, DAP_HostStatus and this command group, so sampling and OpenOCD debugging cannot run at the same time. The formats are in [docs/protocol.en.md](docs/protocol.en.md).

## License

Apache-2.0, see [LICENSE](LICENSE).
