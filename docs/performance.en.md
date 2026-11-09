# Performance

[中文](performance.md)

This document lists the measurements of XRDAP-ESP 0.6.0 and their conditions.

## Conditions

The host runs Windows 11; the row of the classic ESP32 over serial was measured on a Linux host. Targets: STM32H723 for the S3 and the C3, STM32F401RC for the classic ESP32. The test program is XRDAP-Core's [`tests/`](https://github.com/Jiu-xiao/XRDAP-Core/tree/main/tests) (bce599e). Every row is measured at the highest stable SWD clock of that connection.

| Item | Method |
|---|---|
| Highest stable SWD | SWD clocks requested step by step; at each step 4 KiB is written and read back for comparison 50 times (20 times for the classic ESP32 over serial) |
| Small variable read / write | One 4-byte SRAM variable read and written with the vendor extension commands; median round trip (VND-05) |
| Block write / read / pipelined read | 16 KiB written to and read from SRAM; the pipelined read keeps 4 requests in flight (PERF-02) |
| OpenOCD reading 128 KiB | Time to read 128 KiB of target flash; after the target script, the SWD clock is set to the highest stable value of the row |
| probe-rs reading 128 KiB | `probe-rs read` of probe-rs 0.31 reading 128 KiB; transfer time without the connection time |
| Sampling 8 variables | Shortest period at which `xrdap watch` samples 8 SRAM variables for 60 s without skipped ticks or lost frames |

Over native USB, OpenOCD uses its USB backend. Over Wi-Fi, OpenOCD connects directly to port 3241 of the probe with the CMSIS-DAP TCP backend; the OpenOCD column of the three Wi-Fi rows was measured on a Linux host with OpenOCD built from master (0.12.0+dev-02421). A probe on serial is shared with `xrdap share usbip` and attached with `usbip attach`, and OpenOCD uses the USB backend as well; Keil and pyOCD use probes on serial and Wi-Fi the same way. For the row of the classic ESP32 over serial, OpenOCD connects through `xrdap share openocd` on Linux. On Windows, OpenOCD is openocd-esp32 v0.12.0-esp32-20260831.

## Results

| Connection | Highest stable SWD | Small variable read / write | Block write / read / pipelined read | OpenOCD reading 128 KiB | probe-rs reading 128 KiB | Shortest period, 8 variables |
|---|---|---|---|---|---|---|
| S3 native USB | Fastest step, about 23.7 MHz | 132 / 138 µs | 370 / 441 / 552 KiB/s | 0.17 s | about 0.27 s | 200 µs |
| S3 on-board USB-to-serial, 4 Mbaud | Same | 793 / 789 µs | 139 / 140 / 140 KiB/s | 0.68 s | - | 200 µs |
| S3 Wi-Fi | Same | 2355 / 2451 µs | 82 / 93 / 227 KiB/s | 0.39–0.44 s | - | 250 µs |
| C3 serial link on the chip's USB port | Fastest step, about 5.2 MHz | 388 / 389 µs | 170 / 176 / 183 KiB/s | 0.64 s | - | 350 µs |
| C3 Wi-Fi | Same | 3484 / 3464 µs | 55 / 57 / 100 KiB/s | 0.78–0.88 s | - | 350 µs |
| Classic ESP32 on-board USB-to-serial, 921600 baud (Linux) | Fastest step, about 5 MHz | 1077 / 1108 µs | 51 / 54 / 55 KiB/s | 2.0 s | - | 600 µs |
| Classic ESP32 Wi-Fi | Same | 4994 / 5336 µs | 29 / 32 / 66 KiB/s | 0.93–1.09 s | - | 400 µs |

A probe on Wi-Fi attached through `xrdap share usbip`, read by openocd-esp32 on Windows over the USB backend, takes longer to read 128 KiB: 0.6–1.3 s for the S3, 1.2–1.5 s for the C3, 1.4–1.7 s for the classic ESP32.

The probe-rs column has results for native USB only: on Windows, probe-rs 0.31 does not see devices attached by usbip-win2, and probes on serial and Wi-Fi reach probe-rs only through USB/IP.

## Classic ESP32 serial on Windows

The row of the classic ESP32 over serial in the table above was measured on Linux. On Windows with the 2016 Silicon Labs VCP driver (6.7.4.261), the serial link of the on-board CP2102 is markedly slower: block write / read / pipelined read 20 / 22 / 21 KiB/s, small variable read and write 6.1 ms, and a shortest period of 900 µs for 8 variables. On Windows, a current Silicon Labs CP210x driver is recommended.
