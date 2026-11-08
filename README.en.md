# XRDAP-ESP

[中文](README.md)

XRDAP-ESP is CMSIS-DAP v2 debug probe firmware for the Espressif ESP32-S3, ESP32-S2, ESP32-C3 and classic ESP32, built on [libxr](https://github.com/xrobot-org/libxr) and part of the XR-DAP family. Keil, pyOCD, probe-rs and OpenOCD use it as an ordinary CMSIS-DAP probe, connected to the computer by a USB cable or over Wi-Fi. The firmware also implements the XR vendor extension, with which the host tool of [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) reads and writes variables and has the probe sample them at a fixed period. The code shared by the XR-DAP probes is in [XRDAP-Core](https://github.com/Jiu-xiao/XRDAP-Core).

## Boards

| Board | Firmware file | Connection to the computer |
|---|---|---|
| ESP32-S3 board | `xrdap-esp32-s3.bin` | Native USB, on-board USB-to-serial, Wi-Fi |
| Wireless DAP Lite board (ESP32-S3 module) | `xrdap-esp32-s3-lite.bin` | Native USB, Wi-Fi |
| ESP32-S2 board (ESP32-S2FH4) | `xrdap-esp32-s2.bin` | Native USB, Wi-Fi |
| ESP32-C3 board | `xrdap-esp32-c3.bin` | Serial on the chip's own USB port, Wi-Fi |
| Classic ESP32 board | `xrdap-esp32.bin` | On-board USB-to-serial, Wi-Fi |

The native USB of the ESP32-S3 and ESP32-S2 works directly as a CMSIS-DAP probe. The S3 has the highest SWD clock and sampling speed of these chips; the S2 is single-core with less internal RAM. The ESP32-C3 and the classic ESP32 reach the computer over serial, and other debug tools use them through `xrdap share`; the classic ESP32 generates SWD with the GPIO registers on its peripheral bus and is the slowest. Pins, download mode, status lights and buttons of each board are in [docs/boards.en.md](docs/boards.en.md).

## Flashing and updates

The [web flasher](https://jiu-xiao.github.io/XRDAP-ESP/) page, opened in desktop Chrome or Edge, writes the firmware of the latest release over Web Serial once the board is chosen and Flash firmware is pressed.

Every [release](https://github.com/Jiu-xiao/XRDAP-ESP/releases) has the files below:

| File | Contents |
|---|---|
| The five `.bin` files of the table above | The merged image of each board, with the bootloader, partition table and application, written at address 0 |
| `SHA256SUMS` | SHA-256 checksums of these files |

With the downloaded files and `SHA256SUMS` in one directory, `sha256sum -c --ignore-missing SHA256SUMS` shows `OK` for each file that matches the release. On the command line esptool writes the image:

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
python -m esptool --chip esp32s2 write_flash 0x0 xrdap-esp32-s2.bin
python -m esptool --chip esp32c3 write_flash 0x0 xrdap-esp32-c3.bin
python -m esptool --chip esp32 write_flash 0x0 xrdap-esp32.bin
```

The Lite board enters download mode by hand before flashing: USB is unplugged and plugged in again with SW1 held, then SW1 is released, and the computer sees the native USB serial port `303A:1001`. Flashing over this port takes `--after watchdog_reset`; the watchdog then resets the chip and the new firmware starts:

```
python -m esptool --chip esp32s3 --after watchdog_reset write_flash 0x0 xrdap-esp32-s3-lite.bin
```

How the other boards enter download mode is in [docs/boards.en.md](docs/boards.en.md#download-mode). Updates use the same page or commands, and the Wi-Fi settings are kept.

## Connections

The native USB of the ESP32-S3 and ESP32-S2 is a CMSIS-DAP v2 probe on the computer, used directly by the debug tools. The ESP32-C3 and the classic ESP32 reach the computer over serial; `xrdap share usbip` shares such a probe as a USB/IP device, which, attached in a second terminal, is a USB probe as well. `-p` takes the serial port (`/dev/ttyACM0` or `/dev/ttyUSB0` on Linux); on Linux the `vhci-hcd` module is loaded first and `usbip` runs as root:

```
xrdap share usbip -p COM3
usbip attach -r 127.0.0.1 -b 1-1
```

On Wi-Fi the probe serves CMSIS-DAP on TCP port 3241. A computer on the LAN shares it with the same command, with the probe's mDNS host name or IP after `-p`, for example `-p xrdap-6789ab.local`. OpenOCD can also connect to the probe directly with its CMSIS-DAP TCP backend:

```
openocd -c "adapter driver cmsis-dap; cmsis-dap backend tcp; cmsis-dap tcp host xrdap-6789ab.local; cmsis-dap tcp port 3241; transport select swd" -f target/stm32f4x.cfg
```

`xrdap list` lists the connections of each probe and how to write them after `-p`. The connections of each chip, the USB/IP clients, the OpenOCD versions and the rule for several connections at once are in [docs/connections.en.md](docs/connections.en.md).

## Wi-Fi

The probe joins a 2.4 GHz hotspot as a Wi-Fi client, with WPA2-Personal or an open network. `xrdap wifi set lab password` writes the hotspot settings (`lab` the SSID, `password` the password), and the probe saves them, restarts and joins the hotspot; `xrdap wifi status` shows the connection and `xrdap wifi clear` removes the settings. The "Wi-Fi" panel of the `xrdap` GUI offers the same settings, and on the Lite and S2 boards a long press of the board's button removes them as well. How the settings are stored, the record of each removal and the commands in a serial terminal are in [docs/connections.en.md](docs/connections.en.md#wi-fi-settings).

## Reading and sampling variables

With the XR vendor extension one command reads or writes several addresses and follows pointer chains, and the probe samples variables at a fixed period, with the computer fetching the results in batches; the period does not depend on the USB or Wi-Fi round trip. `read`, `write` and `watch` of `xrdap` use these commands; the extension is protocol version 2 and needs `xrdap` 0.6.0 or newer:

```
xrdap read --elf app.elf motor.speed motor.state
xrdap watch --elf app.elf motor.speed motor.current --period 1ms --csv log.csv
```

While sampling, the probe runs only DAP_Info, DAP_HostStatus and the vendor extension commands, so sampling and OpenOCD debugging take turns. The extension commands are 0x80 to 0x86, with 0x86 for the Wi-Fi settings; their formats are in section 3 of XRDAP-Core's [`docs/protocol.en.md`](https://github.com/Jiu-xiao/XRDAP-Core/blob/main/docs/protocol.en.md).

## Performance

Typical values with a USB cable; real results vary with the target chip and the wiring. All measurements and their conditions are in [docs/performance.en.md](docs/performance.en.md).

| Item | ESP32-S3 (native USB) | ESP32-C3 (chip's USB port) | Classic ESP32 (serial, 921600 baud) |
|---|---|---|---|
| Highest SWD clock | about 23.7 MHz | about 5.2 MHz | about 5 MHz |
| Shortest probe-side sampling period (SWD 10 MHz requested) | 4 variables 125 µs | 4 variables 225 µs | 8 variables 900 µs, 1 variable 350 µs |
| OpenOCD reading 128 KiB (10 MHz requested; C3 and classic ESP32 through `xrdap share openocd`) | about 0.3 s | 0.64 s | 2.0 s |

## Building

Building needs ESP-IDF v5.5 (CI uses v5.5.2). libxr and XRDAP-Core are submodules under `third_party/`. `firmware/esp32sx` is the project shared by the S3 and the S2; `firmware/esp32c3` and `firmware/esp32` are those of the C3 and the classic ESP32:

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP.git
cd XRDAP-ESP/firmware/esp32sx
idf.py build merge-bin
```

`build/merged-binary.bin` is the same merged image as the release file, and `idf.py -p COM3 flash` writes the build to the board directly. `firmware/esp32sx` builds the S3 board by default, and the S3 boards can also be chosen in `idf.py menuconfig` under XR-DAP → Board. These commands build the Lite board (one more configuration layer) and the S2 board (another target, with `sdkconfig.defaults.esp32s2` layered automatically for it):

```
idf.py -B build-lite -D SDKCONFIG=build-lite/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.lite" build merge-bin
idf.py -D IDF_TARGET=esp32s2 -B build-s2 -D SDKCONFIG=build-s2/sdkconfig build merge-bin
```

The logic of the console, 0x86, TCP framing, the status light inputs and serial multiplexing has unit tests on the build machine, which need CMake, a C++20 compiler and libudev (`libudev-dev` on Ubuntu). From the repository root:

```
cmake -S firmware/tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests
```

GitHub Actions builds the five images, runs the unit tests and checks the formatting (clang-format 21.1.8); a pushed `v*` tag publishes a release, and the web flasher follows it. The version is `kFirmwareVersion` in `firmware/common/probe/probe_info.hpp`.

## Directories

| Directory | Contents |
|---|---|
| `firmware/esp32sx/` | The ESP32-S3 and ESP32-S2 project: `main.cpp`, `config.hpp` (pins chosen by target and board), the SWD engine, the status light, the clear button |
| `firmware/esp32c3/`, `firmware/esp32/` | The ESP32-C3 and classic ESP32 projects: `main.cpp`, `config.hpp` (pins, SWD timing) |
| `firmware/common/probe/` | Parameters, probe identity, start-up steps, restart, status light inputs, the execution task and the timer of the sampling tick, shared by every chip |
| `firmware/common/net/` | Wi-Fi and the storage of its settings, the 0x86 Wi-Fi command, the CMSIS-DAP TCP server, mDNS |
| `firmware/common/serial/`, `firmware/common/console/` | Splitting DAP frames from the console on a serial port, the Wi-Fi console |
| `firmware/common/swd/` | SWD pins: dedicated GPIO on the S2, S3 and C3, GPIO registers on the classic ESP32 |
| `firmware/tests/` | Unit tests on the build machine |
| `third_party/`, `firmware/components/libxr/` | The submodules libxr and XRDAP-Core; the component that brings libxr into ESP-IDF |
| `web/` | The web flasher page (ESP Web Tools) and the list of board images `boards.json`, published on GitHub Pages by the `pages` workflow |

## License

Apache-2.0, see [LICENSE](LICENSE).
