# XRDAP-ESP32-S3

ESP32-S3 做的 CMSIS-DAP 调试探针，基于 [libxr](https://github.com/xrobot-org/libxr)。可以通过 USB 或 Wi-Fi（TCP）连接，用来烧录和调试 SWD 目标芯片（已测 STM32G431KB）。

## 接线

| 探针 | 目标 |
|---|---|
| GPIO5 | SWCLK |
| GPIO4 | SWDIO |
| GND | GND |

用杜邦线连接时，引脚驱动强度已降到 1（默认 2）。默认强度边沿过冲，会让 SWD 应答出错。

## 编译和烧录

需要 ESP-IDF v5.5 和 libxr-c（`feat/usb-slot-model-dap` 分支，commit `22ba8642` 或更新）。

```powershell
scripts\build.ps1 -LibxrRoot D:\src\libxr-c build
scripts\build.ps1 -LibxrRoot D:\src\libxr-c flash -p COM3
```

引脚、核分配、TCP 端口等固定参数在 `firmware/main/config.hpp`。

## 上位机怎么连

### USB

插上 ESP32-S3 的 USB 口（板上标 USB 的那个，不是串口桥那个）。设备是 `XR-DAP-S3`，VID:PID 为 `303A:4A01`，CMSIS-DAP v2（Bulk），Windows 自带 WinUSB，免驱。同时会多出一个 CDC 虚拟串口。

OpenOCD：

```tcl
adapter driver cmsis-dap
cmsis-dap vid_pid 0x303a 0x4a01
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

Keil：调试器选 CMSIS-DAP Debugger，端口选 SW，设备选你的目标芯片。pyOCD、probe-rs 按平常的 CMSIS-DAP 方式用，不需要额外配置。

### Wi-Fi（TCP）

探针连上 Wi-Fi 后，在 TCP 3241 端口提供 CMSIS-DAP。设备的 IP 在串口日志里（`已获得 IP`），也可以在路由器里查。

OpenOCD 要用带 TCP 后端的版本（Espressif 的 v0.12.0-esp32-20260831 及以后）：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 192.168.1.50   ;# 换成探针的 IP
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

注意：

- TCP 只有 OpenOCD 支持。Keil、pyOCD、probe-rs 都不支持 CMSIS-DAP over TCP。
- 有 TCP 客户端连接时，USB 会关闭；客户端断开后 USB 自动恢复。
- 同一时间只服务一个 TCP 客户端。客户端异常消失（没有正常断开）时，约 11 秒后连接会被释放。
- Wi-Fi 热点参数从设备 NVS 里读取（命名空间 `rmlink`，键 `wifi`），目前沿用 RMLink 固件配网时保存的参数。没有这个参数时 Wi-Fi 不启动，只能用 USB。

## 实测速度

目标 STM32G431KB，杜邦线连接，OpenOCD / 自写客户端测得。

| 项目 | 结果 |
|---|---|
| USB 块读（12 MHz，4 个请求同时在途） | 约 970 KiB/s，已到全速 USB 的上限 |
| USB 块写 | 约 780 KiB/s |
| USB 下 OpenOCD 读 128 KiB（12 MHz） | 约 690 KiB/s |
| Wi-Fi 块读 / 块写（12 MHz） | 约 380 / 450 KiB/s |
| Wi-Fi 最小命令往返 | 约 1.3 ms |
| Wi-Fi 读 4 到 8 个变量，每秒刷新次数 | 约 500 到 620 次 |

## 已知问题

- SWD 时钟建议用 4 MHz。4 MHz 下连续测 5 分钟没有出错；8 MHz 及以上偶尔出现写应答 FAULT（约万分之一个块），OpenOCD 和 probe-rs 会自动重试，所以平时看不到。
- 无线每个命令有约 0.4 ms 的固定开销，每次读一个包的方式做不到 1 kHz 的采样，需要探针板上采样的协议，目前没有。
- 没有配网功能，见上面 Wi-Fi 一节。
- 许可证尚未确定。
