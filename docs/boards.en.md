# Boards

[中文](boards.md)

This document lists the pins, download mode, status lights and buttons of each board XRDAP-ESP supports, and notes on wiring. The firmware file of each board is in the "Boards" section of the README.

## ESP32-S3 board

Firmware `xrdap-esp32-s3.bin`, for ESP32-S3 boards with a USB-to-serial chip (CH343).

| Use | Pin |
|---|---|
| SWCLK | GPIO5 |
| SWDIO | GPIO4 |
| Native USB (the port marked USB) | D- GPIO19, D+ GPIO20 |
| Serial link | UART0 (TX GPIO43, RX GPIO44), through the on-board USB-to-serial chip, 4 Mbaud |
| Status light | The RGB LED (WS2812) on GPIO48, on boards like the ESP32-S3-DevKitC-1 v1.0 |
| BOOT key | GPIO0, for download mode only |

The BOOT key sits on the auto-download circuit of the USB-to-serial chip, where the serial DTR and RTS lines can pull it low, so it serves download mode only. On this board the Wi-Fi settings are removed with `xrdap wifi clear` or the console command `wifi clear`.

## Wireless DAP Lite board

Firmware `xrdap-esp32-s3-lite.bin`. The board carries an ESP32-S3 module and a 2×5 header, and reaches the computer over native USB and Wi-Fi.

| Use | Pin |
|---|---|
| SWCLK | GPIO37 |
| SWDIO | Reaches the header through the on-board SGM3157 analog switch; the firmware holds the switch select pin GPIO36 low and drives and reads SWDIO on GPIO47 |
| Status light | The on-board WS2812, GPIO48 |
| SW1 | GPIO0; held while USB is plugged in, it enters download mode |
| SW2 | GPIO21; held for 5 s, it removes the Wi-Fi settings and restarts the probe |

The module is one without Octal PSRAM (WROOM-1-N4 or N8, MINI-1-N4 or N8); modules with Octal PSRAM use GPIO35 to GPIO37.

## ESP32-S2 board

Firmware `xrdap-esp32-s2.bin`. The board carries an ESP32-S2FH4 (4 MB of flash in the package, no PSRAM) and a 2×5 header, and reaches the computer over native USB and Wi-Fi.

| Use | Pin |
|---|---|
| SWCLK | GPIO37 |
| SWDIO | GPIO35, through a 0 Ω resistor to the header; the on-board SGM3157 is not fitted |
| Target reset (nRESET) | GPIO40, open drain, released at start-up so that the target is not reset |
| Status light | WS2812, GPIO17 |
| KEY | GPIO21; held for 5 s, it removes the Wi-Fi settings and restarts the probe |
| BOOT | Held while USB is plugged in, it enters download mode |

The header also has TDI (GPIO38), TDO (GPIO36) and the target serial TXD (GPIO42) and RXD (GPIO41), which the current firmware does not use. The S2 is single-core with 320 KB of internal RAM, and its Wi-Fi buffers and task stacks are smaller than on the S3.

## ESP32-C3 board

Firmware `xrdap-esp32-c3.bin`.

| Use | Pin |
|---|---|
| SWCLK | GPIO5 |
| SWDIO | GPIO4 |
| The chip's own USB port (USB Serial/JTAG) | D- GPIO18, D+ GPIO19; carries the serial link and the Wi-Fi console |
| UART0 | TX GPIO21, RX GPIO20; Wi-Fi console, 115200 baud |

The probe reaches the computer through the chip's own USB port. On boards whose USB connector is wired to GPIO18 and GPIO19 a USB cable in that connector is enough; on boards whose connector goes to a USB-to-serial chip (for example the ESP32-C3-DevKitM-1) a second USB cable goes to the GPIO18, GPIO19, 5V and GND header pins. On boards that bring UART0 out, UART0 offers the Wi-Fi console as well.

## Classic ESP32 board

Firmware `xrdap-esp32.bin`, for ESP32 boards with a USB-to-serial chip.

| Use | Pin |
|---|---|
| SWCLK | GPIO18 |
| SWDIO | GPIO19 |
| Serial link and Wi-Fi console | UART0 (TX GPIO1, RX GPIO3), the port of the on-board USB-to-serial chip, 921600 baud |

GPIO5 is a strapping pin of the classic ESP32, so SWD uses GPIO18 and GPIO19.

## Download mode

| Board | Entering download mode |
|---|---|
| ESP32-S3 board | Through the USB-to-serial port, esptool and the web flasher enter it with the auto-download circuit; through the native USB port, BOOT is held while USB is plugged in, or BOOT is held while RST is pressed once |
| Classic ESP32 board | esptool and the web flasher enter it with the auto-download circuit of the USB-to-serial chip |
| ESP32-C3 board | esptool and the web flasher enter it through the chip's own USB port |
| ESP32-S2 board | BOOT is held while USB is plugged in, then released |
| Lite board | USB is unplugged and plugged in again with SW1 held, then SW1 is released; the computer sees the native USB serial port `303A:1001`, and esptool takes `--after watchdog_reset` |

On some boards the auto-download circuit can only reset the chip and cannot pull GPIO0 low, and esptool reports `Wrong boot mode`. Then BOOT is held, EN (RESET) is pressed once and BOOT is released, and the esptool command takes `--before no_reset`.

## Status lights and buttons

The S3 board, the Lite board and the S2 board have a status light, which follows the rule shared by the XR-DAP probes (the "Status light" section of the XRDAP-Core README):

| Light | Meaning |
|---|---|
| One rainbow sweep | Power-up |
| Red | Wired: USB connected to a computer, or a command over USB or serial within 5 s |
| Blue | Wireless: a network debug client (OpenOCD or `xrdap` on the TCP port, `xrdap share` included), or Wi-Fi only |
| Purple | Neither USB nor Wi-Fi connected; breathing while joining Wi-Fi |
| Main colour fading to purple and back every 2 s | The host reported connected through DAP_HostStatus |
| Red and blue alternating every 1 s | The host reported the target running |
| Flash | Every DAP command (over USB, serial or Wi-Fi) brightens the light, which then fades; steady traffic flickers at about 15 Hz |
| Fast purple blink | SW2 on the Lite board or KEY on the S2 board is held; after 5 s the light turns solid purple, the hotspot settings are removed and the probe restarts |
| Solid purple | The hotspot settings were just changed or removed (button, console, `xrdap wifi` or command 0x86); the probe restarts |

With both USB and Wi-Fi connected, the light shows wired, red. When a host exits without DAP_Disconnect, 5 s without commands also returns the light to the main colour.

SW2 on the Lite board and KEY on the S2 board, held for 5 s, remove the hotspot settings and restart the probe. The BOOT key of each board is for download mode only.

## Wiring and SWD clock

SWCLK, SWDIO and GND of the probe go to the target's SWD port, one probe per target. The clock at which SWD runs reliably depends on the wire length, the pull-ups on the target board and the routing; 4 MHz is the usual start. When errors appear at a high clock, the SWCLK and SWDIO waveforms are the first thing to check, and shorter wires or series resistors help. The highest clock of each chip is in [performance.en.md](performance.en.md).
