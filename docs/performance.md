# 性能

[English](performance.en.md)

本文列出 XRDAP-ESP 0.6.0 的测量结果和测量条件。

## 测量条件

主机为 Windows 11，传统 ESP32 经串口的一行在 Linux 主机上测量。目标芯片：S3 和 C3 为 STM32H723，传统 ESP32 为 STM32F401RC。测试程序为 XRDAP-Core 的 [`tests/`](https://github.com/Jiu-xiao/XRDAP-Core/tree/main/tests)（bce599e）。每一行都在该连接的最高稳定 SWD 时钟下测量。

| 项目 | 测法 |
|---|---|
| 最高稳定 SWD | 逐档请求 SWD 时钟，每档写入 4 KiB 并读回比对 50 轮（传统 ESP32 经串口时 20 轮） |
| 小变量读 / 写 | 用厂商扩展命令读、写一个 4 字节 SRAM 变量，取往返时间的中位数（VND-05） |
| 块写 / 读 / 流水读 | 在 SRAM 中写入、读出 16 KiB；流水读同时有 4 个请求在途（PERF-02） |
| OpenOCD 读 128 KiB | 读目标 Flash 128 KiB 的用时；目标脚本之后把 SWD 时钟设为该行的最高稳定值 |
| probe-rs 读 128 KiB | probe-rs 0.31 的 `probe-rs read` 读 128 KiB，扣除连接时间后的传输时间 |
| 8 个变量采样 | `xrdap watch` 采样 8 个 SRAM 变量，连续 60 s 不跳拍、不丢帧的最短周期 |

经原生 USB 时，OpenOCD 使用 USB 后端。经 Wi-Fi 时，OpenOCD 用 CMSIS-DAP TCP 后端直接连接探针的 3241 端口；Wi-Fi 三行的 OpenOCD 一列在 Linux 主机上用从 master 编译的 OpenOCD（0.12.0+dev-02421）测量。串口上的探针由 `xrdap share usbip` 共享、`usbip attach` 挂载，OpenOCD 同样使用 USB 后端；Keil 和 pyOCD 也以这种方式使用串口和 Wi-Fi 上的探针。传统 ESP32 经串口的一行在 Linux 上经 `xrdap share openocd` 连接 OpenOCD。Windows 上的 OpenOCD 为 openocd-esp32 v0.12.0-esp32-20260831。

## 结果

| 连接 | 最高稳定 SWD | 小变量读 / 写 | 块写 / 读 / 流水读 | OpenOCD 读 128 KiB | probe-rs 读 128 KiB | 8 个变量采样最短周期 |
|---|---|---|---|---|---|---|
| S3 原生 USB | 最快一档，约 23.7 MHz | 132 / 138 µs | 370 / 441 / 552 KiB/s | 0.17 s | 约 0.27 s | 200 µs |
| S3 板上 USB 转串口，4 Mbaud | 同上 | 793 / 789 µs | 139 / 140 / 140 KiB/s | 0.68 s | - | 200 µs |
| S3 Wi-Fi | 同上 | 2355 / 2451 µs | 82 / 93 / 227 KiB/s | 0.39–0.44 s | - | 250 µs |
| C3 芯片 USB 口上的串口链路 | 最快一档，约 5.2 MHz | 388 / 389 µs | 170 / 176 / 183 KiB/s | 0.64 s | - | 350 µs |
| C3 Wi-Fi | 同上 | 3484 / 3464 µs | 55 / 57 / 100 KiB/s | 0.78–0.88 s | - | 350 µs |
| 传统 ESP32 板上 USB 转串口，921600 baud（Linux） | 最快一档，约 5 MHz | 1077 / 1108 µs | 51 / 54 / 55 KiB/s | 2.0 s | - | 600 µs |
| 传统 ESP32 Wi-Fi | 同上 | 4994 / 5336 µs | 29 / 32 / 66 KiB/s | 0.93–1.09 s | - | 400 µs |

Wi-Fi 上的探针经 `xrdap share usbip` 挂载、由 Windows 上的 openocd-esp32 经 USB 后端读 128 KiB 时用时更长：S3 0.6–1.3 s，C3 1.2–1.5 s，传统 ESP32 1.4–1.7 s。

probe-rs 一列只有原生 USB 的结果：Windows 上 probe-rs 0.31 看不到 usbip-win2 挂载的设备，而串口和 Wi-Fi 上的探针只能经 USB/IP 供 probe-rs 使用。

## 传统 ESP32 在 Windows 上的串口

上表中传统 ESP32 经串口的一行在 Linux 上测量。Windows 上使用 Silicon Labs 2016 年的 VCP 驱动（6.7.4.261）时，板上 CP2102 的串口链路明显变慢：块写 / 读 / 流水读为 20 / 22 / 21 KiB/s，小变量读写为 6.1 ms，8 个变量采样的最短周期为 900 µs。Windows 上推荐使用当前版本的 Silicon Labs CP210x 驱动。
