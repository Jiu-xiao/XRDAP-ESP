# Connections

[中文](connections.md)

This document describes the connections between an XRDAP-ESP probe and the computer, how to find a probe, the rule for several connections at once, and the setup of USB/IP, OpenOCD, the Windows driver and Wi-Fi.

## Connections of each chip

| Connection | S3 | S2 | C3 | Classic ESP32 | Used by |
|---|---|---|---|---|---|
| Native USB (CMSIS-DAP v2) | yes | yes | no | no | All debug tools |
| Serial link on the chip's USB port | no | no | yes | no | `xrdap`; other debug tools through `xrdap share` |
| On-board USB-to-serial (UART0) | yes, 4 Mbaud (not on the Lite board) | no | no | yes, 921600 baud | Same as above |
| Wi-Fi, through `xrdap share usbip` on a computer | yes | yes | yes | yes | All debug tools after attaching |
| Wi-Fi, CMSIS-DAP TCP backend, port 3241 | yes | yes | yes | yes | OpenOCD, `xrdap` |

The native USB of the S3 and S2 is the USB device `1D50:6199`, named `XR-DAP-S3 CMSIS-DAP` or `XR-DAP-S2 CMSIS-DAP`, and Windows picks the WinUSB driver from its descriptors. The same USB device also has a virtual serial port, which is the Wi-Fi console.

The serial links use the frame format of section 1 of XRDAP-Core's [`docs/protocol.en.md`](https://github.com/Jiu-xiao/XRDAP-Core/blob/main/docs/protocol.en.md) and serve `xrdap`. `xrdap` picks the baud rate from the serial chip: CH343 and similar try 4 Mbaud first, CP2102 and CH340 use 921600. The C3's USB port carries both DAP frames and the Wi-Fi console, and the firmware tells them apart (section 5 of the same document).

## Finding the probe

The serial number is `XRobot-XRDAP-` followed by the 12 uppercase hex digits of the factory MAC, for example `XRobot-XRDAP-0123456789AB`. The USB descriptor, DAP_Info and mDNS all carry this value, and it tells several probes apart: `adapter serial` in OpenOCD, `-u` in pyOCD, `--probe` in probe-rs.

On Wi-Fi the probe announces over mDNS the host name `xrdap-` followed by the last 3 bytes of the MAC in lowercase hex (`xrdap-6789ab.local` for the example above), the service `_xrdap._tcp` and the TXT records `serial`, `chip` and `fw`.

`xrdap list` lists the probes on USB and the LAN, the probes on the chip's own USB port (C3) and the other USB serial ports that may lead to a probe; the connections of one probe are grouped together, each with the way to select it with `-p`. `xrdap list --ask-serial` opens these serial ports one by one and asks, which lists the probes behind a USB-to-serial chip; opening a port may reset the board on it.

## One connection at a time

All connections share one DAP. That the probe is used over one connection at a time is up to the user: the probe runs the commands of every connection, and when two connections send commands at once it runs one at a time, USB commands first, and the SWD state can get mixed up.

`DAP_Connect` records the connection it came from. In the cases below that connection lapses, sampling in progress stops, and the vendor extension commands need a new `DAP_Connect`:

| Case | Connection that lapses |
|---|---|
| Any connection sends `DAP_Disconnect` | Any |
| The TCP client disconnects | One on TCP |
| A USB bus reset (unplug or host reset) | One on USB |
| No serial request for 5 s | One on serial |

Wi-Fi port 3241 serves one client at a time, and other clients wait. When a client does not disconnect cleanly, the probe releases it after about 11 s. The full rules are in section 3.1 of XRDAP-Core's [`docs/protocol.en.md`](https://github.com/Jiu-xiao/XRDAP-Core/blob/main/docs/protocol.en.md).

## USB/IP

`xrdap share usbip` on a computer shares a probe on serial or Wi-Fi as a USB/IP device. Once attached, it is a USB CMSIS-DAP v2 probe with VID:PID `1D50:6199`, named `XR-DAP-C3 CMSIS-DAP` (`XR-DAP-ESP32 CMSIS-DAP` for the classic ESP32, `XR-DAP-S3 CMSIS-DAP` and `XR-DAP-S2 CMSIS-DAP` for the S3 and S2). On Wi-Fi the probe itself serves CMSIS-DAP on TCP port 3241 only; USB/IP comes from `xrdap`.

`-p` takes the serial port (`/dev/ttyACM0` or `/dev/ttyUSB0` on Linux), or for a probe on Wi-Fi its mDNS host name or IP, for example `-p xrdap-6789ab.local`:

```
xrdap share usbip -p COM3
```

`xrdap share` runs until Ctrl-C, so the device is attached in a second terminal:

```
usbip attach -r 127.0.0.1 -b 1-1
```

Once attached, the debug tools use it as a USB probe; when finished, `usbip detach -p 0` detaches it and Ctrl-C in the `xrdap` terminal stops sharing. Ticking 共享为 USB DAP (share as USB DAP) in the `xrdap` GUI after connecting to the probe does the same. For another computer on the LAN, see [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) (`--listen`).

| System | Install | Attach | Detach |
|---|---|---|---|
| Linux | In the kernel: `sudo modprobe vhci-hcd`; on Ubuntu the `usbip` command is in the `linux-tools-generic` package | `sudo usbip attach -r <address> -b 1-1` | `sudo usbip detach -p 0` |
| Windows | [usbip-win2](https://github.com/vadimgrn/usbip-win2) | `usbip attach -r <address> -b 1-1` | `usbip detach -p 0` |

On Linux this udev rule lets ordinary users open the device; the native USB of the S3 and S2 has the same VID:PID, so the rule covers it too:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="6199", MODE="0666"
```

## OpenOCD

Over native USB and USB/IP, OpenOCD uses its USB backend (`-f interface/cmsis-dap.cfg`), and the official release 0.12.0 is enough. A probe on Wi-Fi can also be reached directly on port 3241 with OpenOCD's CMSIS-DAP TCP backend, without USB/IP. The example uses an STM32F4 target; for another chip, `target/stm32f4x.cfg` is replaced:

```
openocd -c "adapter driver cmsis-dap; cmsis-dap backend tcp; cmsis-dap tcp host xrdap-6789ab.local; cmsis-dap tcp port 3241; transport select swd" -f target/stm32f4x.cfg
```

For a probe on serial, `xrdap share openocd` serves the same TCP backend on the computer; see XRDAP-Host.

The TCP backend is not in the official release 0.12.0 and is built from master. On Windows it needs a version that includes commit e89e73839d53: a version without it treats "no data yet" as an error on Windows, waits more than ten seconds at start-up and prints `error reading header` over and over, and reads and writes may fail. Without such a version, the probe is shared with `xrdap share usbip`, attached, and reached through the USB backend.

## Drivers on Windows

Windows sometimes reinstalls the driver of the S3 interface and loses the device interface GUID; then no tool can open the probe. The device is removed from an administrator PowerShell and plugged in again, with `<serial>` the probe's serial number:

```
pnputil /remove-device "USB\VID_1D50&PID_6199\<serial>" /subtree
```

With the 2016 Silicon Labs driver (6.7.4.261) on Windows, the serial link of the CP2102 on classic ESP32 boards is markedly slower than on Linux (figures in [performance.en.md](performance.en.md#classic-esp32-serial-on-windows)); a current Silicon Labs CP210x driver is recommended.

## Wi-Fi settings

The probe joins an existing hotspot as a Wi-Fi client. The Wi-Fi of the ESP32 family works in the 2.4 GHz band; routers often give both bands one name, so when the probe cannot connect, the 2.4 GHz band is the first thing to check. With a password the authentication is WPA2-Personal; without one the network is open. The SSID has 1 to 32 bytes, the password 8 to 63 bytes.

`xrdap wifi set`, `xrdap wifi status` and `xrdap wifi clear` set, query and remove the hotspot settings, with `-p` selecting the probe; for the first setup the probe is connected to the computer by USB or serial. In the `xrdap` GUI, after connecting to the probe, the "Wi-Fi" panel takes the SSID and password, and 保存并重启 (save and restart) does the same. After saving, the probe restarts in about 0.5 s and joins the hotspot; another hotspot is set by saving again, and after a removal the probe no longer joins Wi-Fi. When the hotspot goes away, the probe keeps reconnecting.

The hotspot settings live in NVS and are kept across firmware updates. They are removed by the console command `wifi clear`, by the clear request of command 0x86 or of the control port (which `xrdap wifi clear` sends), by a long press of SW2 on the Lite board or KEY on the S2 board, and by the erase and rebuild the probe does at boot when the NVS partition is damaged (full, or a different format version). Every removal records its source. `xrdap wifi status`, the console command `wifi status` and the 0x86 status reply show the source of the last removal and the cause of the last reset (power-on, software restart, watchdog, panic, brown-out), which tells why the settings are gone; after a task watchdog timeout the console's `wifi status` also names the tasks that were running.

### Console in a serial terminal

Without `xrdap`, Wi-Fi can also be set in a serial terminal. The console is on these ports:

| Board | Port |
|---|---|
| S3 board, Lite board, S2 board | The virtual serial port that appears with the native USB plugged in |
| C3 board | The chip's own USB port; on boards that bring UART0 out, also UART0 at 115200 baud |
| Classic ESP32 board | The port of the on-board USB-to-serial chip, 921600 baud |

The terminal keeps DTR and RTS at 0 when it opens the port, so that the chip is not reset. With pySerial's miniterm, for example (921600 instead of 115200 for the classic ESP32):

```
python -m serial.tools.miniterm COM3 115200 --dtr 0 --rts 0
```

The commands are `wifi set <SSID> [<password>]` (an SSID or password with spaces in double quotes, no password for an open network), `wifi status`, `wifi clear`, `heap` (free heap and the remaining stack of each task) and `help`.
