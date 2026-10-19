# XRDAP-ESP

[中文](README.md)

A CMSIS-DAP debug probe built on ESP32-S3 and ESP32-C3, based on [libxr](https://github.com/xrobot-org/libxr). It flashes and debugs SWD target chips (tested with STM32G431KB and STM32F401RC). The probe connects over USB, serial or Wi-Fi; the three links share one DAP instance. Besides the standard CMSIS-DAP commands it provides the XR vendor extension (multi-address read/write, pointer chains, probe-side periodic sampling).

| | ESP32-S3 | ESP32-C3 |
|---|---|---|
| Product name (DAP_Info) | `XR-DAP-S3` | `XR-DAP-C3` |
| USB CMSIS-DAP v2 (works directly with Keil, pyOCD, probe-rs, OpenOCD) | Yes | No (the chip's USB is the fixed-function USB Serial/JTAG) |
| Serial DAP link (to OpenOCD through `xrdap-forward`) | UART0 via the on-board USB-to-serial bridge, 4 Mbaud | The chip's USB port (USB Serial/JTAG) |
| Wi-Fi, CMSIS-DAP on TCP 3241 (OpenOCD TCP backend) | Yes | Yes |
| Wi-Fi, USB/IP virtual USB probe on port 3240 (works directly with Keil, pyOCD, probe-rs, OpenOCD after attaching) | Yes | Yes |
| Wi-Fi command line (provisioning) | USB virtual serial port | The chip's USB port (shared with the DAP link, told apart automatically), UART0 |
| Provisioning over DAP (`xrdap wifi set`, vendor command 0x86) | USB, serial, TCP | Serial, TCP |
| mDNS advertisement (`xrdap-xxxxxx.local`) | Yes | Yes |
| Maximum SWD clock | About 23.7 MHz (assembly fast path) | Reads and writes work at a requested 20 MHz |
| XR vendor extension (multi-address read/write, pointer chains, probe-side sampling) | Yes | Yes |

Both firmwares share the vendor extension, TCP server, serial adapter, command line and Wi-Fi code under `firmware/common`, so their behaviour is identical.

The host tools are in the [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) repository: the command line `xrdap`, the GUI `xrdap-gui`, the serial forwarder `xrdap-forward`, and the Rust crate `xrdap`.

## Wiring

Both chips use GPIO5 and GPIO4:

| Probe | Target |
|---|---|
| GPIO5 | SWCLK |
| GPIO4 | SWDIO |
| GND | GND |

The pin drive strength is set to 1 (the ESP-IDF default is 2) to reduce edge overshoot and ringing. On the S3, level 0 (weakest) produced more errors above 10 MHz in measurements.

Connect only one probe to a target: a probe that is not in use may still drive SWCLK and SWDIO.

The DAP serial link of the ESP32-C3 uses the chip's USB D-/D+ (GPIO18 / GPIO19). A board whose USB connector goes straight to these two pins works when plugged in. On boards such as the ESP32-C3-DevKitM-1, where the USB connector goes to a USB-to-serial chip (UART0), a second USB cable has to be wired to the header: D- to GPIO18, D+ to GPIO19, plus 5V and GND.

## Download and flash

Download `xrdap-esp32-s3.bin` or `xrdap-esp32-c3.bin` from [Releases](../../releases); `SHA256SUMS` on the same page lists the checksums of both files. Each file is a merged full image (bootloader, partition table, application) that is flashed from address 0.

ESP32-S3:

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
```

ESP32-C3:

```
python -m esptool --chip esp32c3 write_flash 0x0 xrdap-esp32-c3.bin
```

esptool flashes through the chip's ROM download mode: the S3 uses its USB port or the serial bridge port, the C3 uses its USB port or UART0. To enter download mode manually, hold BOOT and press RESET once.

### Upgrades and Wi-Fi settings

The Wi-Fi credentials are stored in the NVS partition. From 0.2.0 on, NVS sits after the application partition (at `0x210000` on the S3, `0x190000` on the C3). The merged release image covers only address 0 to the end of the application, so upgrading with the same command keeps the Wi-Fi settings.

Upgrading from older firmware (0.1.x, NVS at `0x9000`) to 0.2.0 does not carry the credentials over from the old location; Wi-Fi has to be provisioned once more, and later upgrades keep it. `esptool erase_flash` wipes the whole flash, including the credentials.

## Build from source

ESP-IDF v5.5 is required. libxr is a git submodule at `third_party/libxr`, tracking the libxr branch `feat/xrdap-serial-transport`. Clone with `--recurse-submodules` (for an existing clone, run `git submodule update --init`).

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP.git
cd XRDAP-ESP/firmware/esp32s3      # firmware/esp32c3 for the C3
idf.py build
idf.py -p COM3 flash
```

`idf.py build merge-bin` additionally produces `build/merged-binary.bin`, which has the same layout as the release images and can be flashed with the `esptool write_flash 0x0` command from the previous section.

| Directory | Content |
|---|---|
| `firmware/esp32s3` | S3 project: `main.cpp`, `config.hpp` (pins, core assignment, priorities and other fixed parameters), SWD engine and assembly fast path |
| `firmware/esp32c3` | C3 project: `main.cpp`, `config.hpp`, SWD clock adaptation |
| `firmware/common` | Shared: vendor extension, TCP server, serial adapter, serial separation, Wi-Fi command line and connection, mDNS, probe identity, dedicated GPIO |
| `firmware/components/libxr` | Wraps the libxr submodule as an ESP-IDF component |
| `docs/protocol.md` | Serial frame format, vendor commands, probe identity, mDNS |

GitHub Actions builds both firmwares on every push and pull request; pushing a `v*` tag publishes a release (the two images and `SHA256SUMS`).

## Connection methods

The probe's connections share one DAP instance, and only one is used at a time. The OpenOCD configurations below target an STM32G431KB; replace the `source [find target/...]` line for another chip. OpenOCD is Espressif's v0.12.0-esp32-20260831 or newer.

### USB (ESP32-S3)

Plug in the USB port of the ESP32-S3 (the one labelled USB on the board, not the serial bridge port). The USB product name is `XR-DAP-S3 CMSIS-DAP` and VID:PID is `1D50:6199` (the identifier registered with XRUSB). The serial number is `XRobot-XRDAP-` followed by the factory MAC (12 upper-case hexadecimal digits). The interface is CMSIS-DAP v2 (Bulk); Windows provides WinUSB, so no driver is needed. A CDC virtual serial port appears as well and is used for Wi-Fi provisioning. USB is closed automatically while a TCP client is connected and restored after the client disconnects.

OpenOCD: save as `s3-usb.cfg` and run `openocd -f s3-usb.cfg`:

```tcl
adapter driver cmsis-dap
adapter usb vid_pid 0x1d50 0x6199
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

This OpenOCD build does not accept `cmsis-dap vid_pid`; use `adapter usb vid_pid`. When several devices with this VID:PID are present on one computer, select one with an extra line `adapter serial XRobot-XRDAP-E8069093EF1C` (replace with the probe's serial number).

Keil: choose CMSIS-DAP Debugger as the debugger, SW as the port, and the target chip as the device. In Settings on the Debug tab, the target IDCODE appearing in the SW Device field indicates a working connection; start with Max Clock at 4 MHz.

pyOCD:

```
pyocd list
pyocd pack install stm32g431kb
pyocd flash -t stm32g431kbtx -f 4m app.hex
pyocd gdbserver -t stm32g431kbtx -f 4m
```

`pyocd list` should show `XR-DAP-S3 CMSIS-DAP` (the pyOCD lines are untested). With several CMSIS-DAP probes attached, add `-u <serial number>`. The target name follows the output of `pyocd list --targets`.

probe-rs:

```
probe-rs list
probe-rs run --chip STM32G431KBTx --probe 1d50:6199 --speed 4000 app.elf
```

`probe-rs list` shows `XR-DAP-S3 CMSIS-DAP -- 1d50:6199-0:XRobot-XRDAP-<MAC>`. With several probes attached, pass the string after `--` to `--probe`.

Windows occasionally reinstalls the driver for this interface and loses the device interface GUID, so that neither OpenOCD nor xrdap can open the probe. Removing the device from an administrator PowerShell and plugging it in again recovers it: `pnputil /remove-device "USB\VID_1D50&PID_6199\<serial number>" /subtree`.

### Serial (ESP32-S3)

The S3 provides CMSIS-DAP on UART0. UART0 goes to the on-board USB-to-serial chip (GPIO43 TX, GPIO44 RX), that is, the serial bridge USB port, at 4 Mbaud (the CH343 on the board does not work correctly at 5 Mbaud). Once UART0 carries DAP, the system log console is disabled. There is no automatic switching between USB and serial; do not use both at the same time.

OpenOCD cannot use a serial port directly. On the computer, `xrdap-forward` converts the serial port into the CMSIS-DAP TCP backend of OpenOCD. Run it first in one terminal (replace `COM35` with the serial bridge port; on Linux a device name such as `/dev/ttyUSB0`):

```
xrdap-forward --serial COM35
```

The S3's USB-to-serial chip resets the ESP32 once through DTR/RTS when the port is opened. `xrdap-forward` queries the probe repeatedly, waits until it has booted (up to 10 seconds) and then listens on 127.0.0.1:3241. In another terminal, run OpenOCD with the following configuration, saved as `s3-serial.cfg`:

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 127.0.0.1
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

```
openocd -f s3-serial.cfg
```

On Windows two more lines are needed in the configuration, see the Wi-Fi (TCP) section below.

### Serial (ESP32-C3)

The C3 provides CMSIS-DAP on the chip's USB port (USB Serial/JTAG, a CDC serial port on the computer); the baud rate has no effect. USB Serial/JTAG resets the chip by itself according to DTR and RTS, so `xrdap-forward` takes `--keep-lines` to leave these two lines unchanged:

```
xrdap-forward --serial COM7 --keep-lines
```

The OpenOCD configuration is the same as for the S3 serial method; save it as `c3-serial.cfg`:

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 127.0.0.1
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32f4x.cfg]
adapter speed 4000
```

```
openocd -f c3-serial.cfg
```

This port also carries the Wi-Fi command line, see Wi-Fi provisioning below. The serial frame format is described in [docs/protocol.en.md](docs/protocol.en.md).

### Wi-Fi (TCP)

Once connected to Wi-Fi, the probe serves CMSIS-DAP on TCP port 3241, identically on S3 and C3. The IP address can be read with `xrdap wifi status` or the serial command `wifi status`, or looked up in the router; the probe also advertises the mDNS host name `xrdap-xxxxxx.local` (xxxxxx being the last 3 bytes of the factory MAC, lower case), see "Finding probes" below.

OpenOCD needs a build with the TCP backend. To connect by IP address, save this as `wifi.cfg`:

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 192.168.1.50   ;# replace with the probe's IP
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

To connect by host name, replace only the host line:

```tcl
cmsis-dap tcp host xrdap-93ef1c.local
```

On Windows, add two lines after `cmsis-dap tcp port`:

```tcl
cmsis-dap quirk enable
cmsis-dap tcp min_timeout 10
```

The complete Windows configuration:

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 192.168.1.50
cmsis-dap tcp port 3241
cmsis-dap quirk enable
cmsis-dap tcp min_timeout 10
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

Notes:

- Only OpenOCD supports TCP. Keil, pyOCD and probe-rs do not support CMSIS-DAP over TCP; use USB/IP below instead.
- Only one TCP client is served at a time. When a client disappears without disconnecting properly, the connection is released after about 11 seconds.
- On Windows this OpenOCD build's TCP backend treats "no data yet" as an error (it checks `errno`, which Winsock does not set): startup waits about 10 seconds, `error reading header` floods the log, and with several requests in flight the responses become misaligned and `command mismatch` is reported. `quirk enable` makes it send one request at a time, and `min_timeout 10` shortens the startup wait to about 1 second; the startup errors are still printed and can be ignored. OpenOCD upstream has fixed this (commit e89e73839d53); Espressif has no release containing the fix yet. Linux does not need these two lines.
- The serial method (`xrdap-forward` plus OpenOCD) uses the same two lines on Windows.

### Wi-Fi (USB/IP virtual USB probe)

From firmware 0.3.0, the probe also serves a USB/IP device on port 3240. After a computer attaches it with a USB/IP client, the system shows a USB CMSIS-DAP v2 probe (VID:PID 1D50:6199, bcdDevice 0x0200, product name `XR-DAP-S3 CMSIS-DAP` or `XR-DAP-C3 CMSIS-DAP`, serial number the same as in DAP_Info). Keil, pyOCD, probe-rs and OpenOCD use it as a USB probe, and Windows uses the WinUSB driver automatically.

Linux (USB/IP is built into the kernel):

```
sudo modprobe vhci-hcd
usbip list -r xrdap-721384.local
sudo usbip attach -r xrdap-721384.local -b 1-1
openocd -f interface/cmsis-dap.cfg -c "adapter serial XRobot-XRDAP-9C139E721384" -f target/stm32f4x.cfg
sudo usbip port
sudo usbip detach -p 0
```

For a normal user to open the device, the udev rule `SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="6199", MODE="0666"` is needed; otherwise run the debug tool with sudo. On Windows, install [usbip-win2](https://github.com/vadimgrn/usbip-win2) first; the command is the same, `usbip attach -r <probe address> -b 1-1`.

Notes:

- Ports 3240 and 3241 are served by the same task and only one client is served at a time; while one is connected, the other waits in the queue. `usbip list` only queries the device and does not occupy the probe.
- While attached, the USB device of the S3 is closed (same as for TCP 3241) and restored after the disconnect.
- When the external tool does not disconnect properly, the connection is released after about 11 seconds.
- Measured (ubuntu24, OpenOCD reading 128 KiB of an F401): S3 2.1 s, C3 2.7 s; under the same conditions TCP 3241 takes 1.6 s and 1.9 s.
- With a C3 plugged into the computer, the USB cable has lower latency than Wi-Fi: use `xrdap-forward -p COM7 --usbip` of XRDAP-Host or the "共享为 USB DAP" (share as USB DAP) option of `xrdap-gui` to provide the same device on the local machine, see [the XRDAP-Host documentation](https://github.com/Jiu-xiao/XRDAP-Host/blob/main/README.en.md#share-as-usb-dap-usbip).

### Wi-Fi provisioning

The probe has to learn the access point name and password first, in one of three ways. Quote the name or password with double quotes when it contains spaces; omit the password for an open network; a password is 8 to 63 characters. After saving, the device restarts and connects to the access point; without credentials Wi-Fi does not start. The credentials are stored in the device NVS (namespace `rmlink`, key `wifi`) in the same format as the RMLink firmware, so a board provisioned with RMLink does not need to be provisioned again.

#### S3: USB virtual serial port

After a USB connection the S3 shows an additional virtual serial port (`USB Serial Device` in Device Manager), which the command line owns exclusively. Open it with a serial terminal; the baud rate does not matter:

```
python -m serial.tools.miniterm COM36 115200
```

Enter:

```
wifi set <ssid> <password>
```

#### C3: the USB port shared with the DAP link

The command line of the C3 is on the chip's USB port, the same port as the DAP serial link, and no mode switch is needed: the probe tells DAP frames and typed commands apart automatically (`firmware/common/serial_mux.hpp`; the mechanism is described in section 5 of [docs/protocol.en.md](docs/protocol.en.md)). The command line answers only after a line has been entered with Enter, and drops a whole line that contains control characters; while a DAP frame was seen within the last 2 seconds, text input is ignored, so debug tools never receive command-line output. The terminal must not change DTR/RTS, otherwise the chip may be reset:

```
python -m serial.tools.miniterm COM7 115200 --dtr 0 --rts 0
```

Enter the same `wifi set <ssid> <password>`. A board that exposes UART0 (GPIO21 TX, GPIO20 RX) can also use UART0 at 115200 baud.

Commands in the terminal:

| Command | Effect |
|---|---|
| `wifi set <ssid> [<password>]` | Save the credentials and restart |
| `wifi status` | Show the access point, connection state, IP and signal strength |
| `wifi clear` | Clear the saved credentials and restart |
| `help` | List the commands |

#### Over DAP: `xrdap wifi`

Vendor command 0x86 reads and writes the credentials through the current DAP connection (USB, serial or TCP) without a serial terminal, on both S3 and C3. `-p` selects the connection, see "Finding probes" below:

```
xrdap wifi status
xrdap -p COM7 wifi set <ssid> <password>
xrdap wifi clear
```

After `wifi set` or `wifi clear` succeeds the probe restarts about 0.5 seconds later and the current connection drops. `wifi status` shows the access point name, whether it is connected, the IP and the signal strength, and never the password. The command format is in section 3.7 of [docs/protocol.en.md](docs/protocol.en.md).

### Finding probes

#### xrdap list

```
xrdap list
```

Lists probes on USB, serial ports and probes on the LAN (mDNS), grouped by serial number: entries for the same probe on USB, serial and Wi-Fi appear together, each with the matching `--probe` value.

#### Serial number

A serial number looks like `XRobot-XRDAP-E8069093EF1C`: `XRobot-XRDAP-` followed by the factory MAC as 12 upper-case hexadecimal digits. The same value appears in the S3 USB descriptor, in `DAP_Info` and in the mDNS records.

#### mDNS

Once it has an IP address the probe advertises the service `_xrdap._tcp` (port 3241) with the host name `xrdap-` followed by the last 3 MAC bytes in lower-case hexadecimal; the serial number `XRobot-XRDAP-E8069093EF1C` corresponds to `xrdap-93ef1c.local`. The TXT records are `serial`, `chip` (`esp32s3` or `esp32c3`) and `fw` (firmware version). The OpenOCD option `cmsis-dap tcp host` accepts this host name directly.

#### The --probe option of xrdap

Without `-p`, a single USB probe is used directly; otherwise `-p` has to select one:

```
xrdap info                          # selected automatically when only one USB probe exists
xrdap -p COM35 info                 # serial; Espressif USB Serial/JTAG ports automatically keep DTR/RTS
xrdap -p 192.168.0.108 info         # TCP by IP
xrdap -p xrdap-93ef1c.local info    # TCP by mDNS host name
xrdap -p 93EF1C info                # by serial number suffix
```

## XR vendor extension

Besides the standard CMSIS-DAP commands the probe implements a set of vendor commands: 0x80 to 0x85 read and write several addresses at once, read variables along pointer chains, and sample variables periodically on the probe for the host to fetch in batches; 0x86 reads and writes the Wi-Fi credentials over DAP. Sampling is timed by the probe, so its rate is not limited by the link round-trip latency. Behaviour is identical over USB, serial and TCP. The INFO command (0x80) returns the feature bits; bit 4 set means 0x86 is supported.

During sampling the probe accepts only `DAP_Info`, `DAP_HostStatus` and these commands; every other command is answered with `0xFF`, so sampling cannot run together with OpenOCD debugging. The command formats and sampling rules are in [docs/protocol.en.md](docs/protocol.en.md); 0x86 is in section 3.7.

The sampling task is woken by a timer interrupt on each tick, blocks between ticks, and is scheduled above the DAP command, TCP and serial tasks, so the host fetching data does not displace ticks. Each period leaves at least 1/8 of the period (15 µs minimum) to other tasks: when a tick finishes too close to the next one, or is already late, the next tick is skipped and counted in the skipped tick count, so the probe always stays responsive.

## xrdap examples

`xrdap` (command line) and `xrdap-gui` (GUI) use these commands to read and write target memory by variable name and to sample on the probe and plot curves. Variable addresses and types come from the DWARF debug information in the target program's ELF, hence the `--elf` option:

```
xrdap --elf app.elf read motor.speed
xrdap --elf app.elf write motor.kp=1.5
xrdap --elf app.elf watch motor.speed --period 1ms --csv log.csv
xrdap halt
xrdap run
xrdap reset --halt
```

`read` reads a variable once, `write` takes `variable=value`, and `watch` samples on the probe at the given period and writes a CSV file. `halt`, `run` and `reset` control the target core; `reset --halt` stops at the reset vector after resetting. Every command accepts `-p` to select the probe, for example `xrdap -p 192.168.0.108 --elf app.elf watch motor.speed --period 1ms`. See `xrdap --help` and [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) for the full options.

## Measurements: ESP32-S3

Target STM32G431KB, jumper wires, measured with OpenOCD or a custom client. The OpenOCD items at 4 MHz and the sampling item were measured on 2026-10-18.

| Item | Result |
|---|---|
| USB block read (12 MHz, 4 requests in flight) | About 970 KiB/s, the full-speed USB limit |
| USB block write | About 780 KiB/s |
| OpenOCD over USB, read 128 KiB (12 MHz) | About 690 KiB/s |
| OpenOCD over USB, read 128 KiB (4 MHz) | 0.62 s |
| OpenOCD over USB, flash 16 KiB and verify (4 MHz) | 1.09 s |
| OpenOCD over USB, connect and disconnect 20 times (4 MHz) | All 20 succeeded, 0.09 s on average |
| Wi-Fi block read / write (12 MHz) | About 380 / 450 KiB/s |
| Wi-Fi minimum command round trip | About 1.3 ms |
| Wi-Fi, reading 4 to 8 variables, refreshes per second | About 500 to 620 |
| Probe-side sampling of 8 items (with dividers), period 2 ms, 5 minutes continuous (USB) | 149999 frames, no dropped frames or skipped ticks, timestamp deviation from k x 2 ms between -27 and +12 µs |
| OpenOCD on Windows through serial forwarding, read 128 KiB / flash 16 KiB and verify (4 MHz, with the `quirk` and `min_timeout` lines) | 2.1 s / 2.3 s (including about 1 s connection wait) |
| OpenOCD on Windows over Wi-Fi, read 128 KiB / flash 16 KiB and verify (4 MHz, with the `quirk` and `min_timeout` lines) | 2.5 s / 3.0 s (including about 1 s connection wait) |

### Limits of each connection (2026-10-19, SWD requested 25 MHz, actual about 23.7 MHz)

The host is a Python script on Windows; each item is the average over 1.5 seconds, one request at a time (synchronous). The Wi-Fi round trip averages about 2 ms with a maximum of 6 ms, and the Wi-Fi figures vary with the environment.

| Item | USB | Serial 4 Mbaud | Wi-Fi TCP |
|---|---|---|---|
| Read one word (DAP command), per second | 7,200 | 1,390 | 260 to 360 |
| Write one word, per second | 7,100 | 1,400 | 270 to 350 |
| Vendor READ polling 1 variable, per second | 6,100 | 1,290 | 250 to 380 |
| Vendor READ polling 8 variables, per second | 2,400 | 770 | 230 to 260 |
| Vendor READ polling 32 variables, per second | 820 | 290 | 200 to 235 |
| SRAM bulk read (1000 bytes per packet), KiB/s | 534 | 180 | 200 to 210 |
| SRAM bulk write, KiB/s | 429 | 185 | 190 to 220 |
| Four packets at once on one TCP connection: read / write, KiB/s | - | - | 559 / 529 |

Shortest period at which probe-side sampling (every variable read on every tick, 4-byte direct reads) runs for 2 seconds without skipped ticks or dropped frames:

| Variables | USB | Serial | Wi-Fi |
|---|---|---|---|
| 1 | 86 µs (11.6 kHz) | 86 µs (11.6 kHz) | 118 µs (8.5 kHz) |
| 2 | 86 µs (11.6 kHz) | 101 µs (9.9 kHz) | 137 µs (7.3 kHz) |
| 4 | 118 µs (8.5 kHz) | 137 µs (7.3 kHz) | 137 µs (7.3 kHz) |
| 8 | 167 µs (6.0 kHz) | 220 µs (4.5 kHz) | 251 µs (4.0 kHz) |
| 16 | 325 µs (3.1 kHz) | 372 µs (2.7 kHz) | 372 µs (2.7 kHz) |
| 32 | 604 µs (1.66 kHz) | 681 µs (1.47 kHz) | 608 µs (1.64 kHz) |

With 32 variables the total throughput is about 53,000 reads per second on USB, about 47,000 on serial and about 53,000 on Wi-Fi. With SWD at 4 MHz, 32 variables need a period of more than 2 ms. Every pointer level adds one read. Reading one variable is 3 SWD transfers (write the address, read the data, read RDBUFF once more); at 25 MHz this takes about 11.4 µs per variable, of which about 6 µs is time on the wire.

Flash (OpenOCD, 2 KiB pages, STM32G431KB): erasing takes about 16 ms per page and does not depend on the connection; programming runs at about 75 to 84 KiB/s, determined by the chip's flash.

| Item (128 KiB) | USB | Serial forwarding (Windows) | Wi-Fi (Windows) |
|---|---|---|---|
| Erase | 2.0 s | 2.0 s | 2.0 s |
| Program | 1.5 s | 1.6 s | 1.7 s |
| Verify | 0.94 s | 0.95 s | 0.98 s |
| Read | 0.21 s | 0.69 s | 0.68 s |

## Measurements: ESP32-C3

2026-10-19, a C3 board with native USB, target STM32F401RC, jumper wires.

- Serial link (USB Serial/JTAG): 1/2/4/8-byte RAM reads and writes read back correctly after writing; no errors at SWD clocks of 1, 4, 10 and 20 MHz.
- Probe-side sampling (SWD 20 MHz): over the USB port, 4 variables run without skipped ticks down to 300 µs (about 3.3 kHz), and 32 variables at 2 ms. Over Wi-Fi, 4 variables at a 1 ms period run for 5 seconds without skipped ticks; at 500 µs, 19 ticks are skipped (about 0.2%). The C3 has a single core, so Wi-Fi traffic and sampling share a core, and shorter periods skip more ticks; sampling over the USB port also skips an occasional tick while Wi-Fi is connected.
- OpenOCD: exporting 64 KiB of SRAM through `xrdap-forward` takes 0.56 s (2 MHz), and 0.87 s directly over Wi-Fi; flashing a 420-byte test program and verifying takes 1.9 s over the USB port and 2.9 s over Wi-Fi.
- Terminal input, DAP reads and writes, and sampling alternate on the same USB port without problems, and binary data never makes the command line print anything.

## Known issues

- SWD clock: SWCLK high and low times are equal, and the actual frequency is close to the requested one (4.03 MHz measured at 4 MHz on the S3; a request of 25 MHz or more uses the fastest setting, about 23.7 MHz, a figure calculated from CPU cycle counts and not measured with an oscilloscope). The SWCLK frequency correction of the C3 reuses the measured S3 values and has not been measured. How fast the link can run depends on external conditions: wire length and impedance, pull-ups and routing on the target board, jumper wire quality. Start with 4 MHz. On the S3 test board with jumper wires, 3000 rounds each at 4 to 20 MHz (each round writes 1016 bytes and reads them back for comparison, about 24 million words in total) produced no acknowledge errors and no data mismatch. Measure again after changing wires or target board; when problems appear at high clocks, look at the SWCLK and SWDIO waveforms with an oscilloscope first and adjust the external circuit (shorter wires, series resistors, pull-ups, etc.).
- The earlier SWD implementation (the one bundled with libxr) often produced FAULT write acknowledges above 10 MHz and occasional data errors: the GPIO_ENABLE register write that switches to output goes over the peripheral bus and takes effect later than expected, so the first two data bits were pulled up to 1. The register is now read back after switching, waiting for the write to complete before data is sent.
- The Windows problem of the OpenOCD TCP backend is described in the Wi-Fi (TCP) section of "Connection methods".
- Upgrading from 0.1.x to 0.2.0 requires provisioning Wi-Fi once more, see "Upgrades and Wi-Fi settings".

## License

Apache-2.0, see [LICENSE](LICENSE).
