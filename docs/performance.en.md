# Performance

[中文](performance.md)

This document lists the measurements of XRDAP-ESP with the conditions of each. The values are typical; real results vary with the target chip and the wiring.

## Overview

With a USB cable:

| Item | ESP32-S3 | ESP32-C3 | Classic ESP32 |
|---|---|---|---|
| Highest SWD clock | about 23.7 MHz | about 5.2 MHz | about 5 MHz (SWD through the GPIO registers, limited by the peripheral bus) |
| Shortest probe-side sampling period | 4 variables 125 µs | 4 variables 225 µs | 8 variables 900 µs, 1 variable 350 µs |
| OpenOCD reading 128 KiB | about 0.3 s | 0.64 s | 2.0 s |

The OpenOCD row requests 10 MHz; the C3 and the classic ESP32 go through `xrdap share openocd`.

## ESP32-S3

| Item | Result |
|---|---|
| USB block read / block write (SWD about 23.7 MHz) | about 410 / 360 KiB/s |
| OpenOCD reading 128 KiB over USB (10 MHz) | about 0.3 s |
| OpenOCD flashing 16 KiB with verify over USB (10 MHz, STM32H723, including the erase of one 128 KiB sector) | about 1.1 s |
| Smallest command round trip over Wi-Fi (computer on 5 GHz and probe on 2.4 GHz, same router) | about 3.3 ms |
| Probe-side sampling of 8 variables, 2 ms period, 5 minutes continuous | 149999 frames, no lost frames, no skipped ticks, timestamp deviation -27 to +12 µs |

Shortest probe-side sampling period (4-byte variables, no skipped ticks or lost frames). The USB and Wi-Fi columns were measured at SWD 10 MHz over two runs of 3 seconds each, the serial column at SWD about 23.7 MHz over 2 seconds:

| Variables | USB | Serial | Wi-Fi |
|---|---|---|---|
| 1 | 100 µs | 86 µs | 100 µs |
| 8 | 150 µs | 220 µs | 200 µs |
| 32 | 300 µs | 681 µs | 608 µs |

A requested SWD clock of 25 MHz or more uses the fastest step, about 23.7 MHz.

## OpenOCD reading 128 KiB

4 MHz requested:

| Path | S3 | C3 |
|---|---|---|
| Wi-Fi, TCP backend | 0.9 s | 1.9 s |
| Wi-Fi, USB/IP | 2.1 s | 2.7 s |
| C3 over USB, `xrdap share usbip` | - | 1.8 s |
| C3 over USB, `xrdap share openocd` (10 MHz requested) | - | 0.64 s |

## ESP32-C3

The fastest SWD step is about 5.2 MHz, and higher requests run at that step. The C3 is single-core, so Wi-Fi traffic and sampling share one core: sampling 4 variables over Wi-Fi skips no ticks at a 500 µs period and about 1% of ticks at 250 µs.

## Classic ESP32

Measured over serial at 921600 baud. Reads and writes are correct for SWD clock requests from 1 to 40 MHz. The GPIO registers are on the peripheral bus (about 12 CPU cycles per write, 17 per read), so any request above 5 MHz runs at full speed, which is just under 5 MHz. Probe-side sampling of 8 variables at 900 µs and of 1 variable at 350 µs skips no ticks and loses no frames; at shorter periods the probe still skips no ticks, but the 921600 baud serial link cannot carry all the data and the buffer starts dropping frames. Through `xrdap share openocd` (10 MHz requested), OpenOCD reads 128 KiB in 2.0 s.

Over Wi-Fi, 4 variables at 500 µs, 8 variables at 600 µs and 1 variable at 150 µs lose no frames (at 150 µs a tick is skipped now and then); OpenOCD reads 128 KiB in 3.2 s through a USB/IP attachment and in 2.3 s through the TCP backend.
