# XRDAP-ESP32-S3

ESP32-S3 做的 CMSIS-DAP 调试探针，基于 [libxr](https://github.com/xrobot-org/libxr)。可以通过 USB 或 Wi-Fi（TCP）连接，用来烧录和调试 SWD 目标芯片（已测 STM32G431KB）。

## 接线

| 探针 | 目标 |
|---|---|
| GPIO5 | SWCLK |
| GPIO4 | SWDIO |
| GND | GND |

引脚驱动强度设为最低（0，ESP-IDF 默认是 2），减小边沿过冲和振铃。

## 编译和烧录

需要 ESP-IDF v5.5 和 libxr-c（`feat/usb-slot-model-dap` 分支，commit `22ba8642` 或更新）。

```powershell
scripts\build.ps1 -LibxrRoot D:\src\libxr-c build
scripts\build.ps1 -LibxrRoot D:\src\libxr-c flash -p COM3
```

引脚、核分配、TCP 端口等固定参数在 `firmware/main/config.hpp`。

## 上位机怎么连

### USB

插上 ESP32-S3 的 USB 口（板上标 USB 的那个，不是串口桥那个）。设备名 `XR-DAP-S3`，VID:PID 为 `1D50:6199`（XRUSB 登记的标识），序列号是 `XRobot-XRDAP-` 加芯片唯一 ID。CMSIS-DAP v2（Bulk），Windows 自带 WinUSB，免驱。同时会多出一个 CDC 虚拟串口。

OpenOCD：

```tcl
adapter driver cmsis-dap
cmsis-dap vid_pid 0x1d50 0x6199
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

同一台电脑上有多个使用这组 VID:PID 的设备时，用 `adapter serial <序列号>` 指定。

Keil：调试器选 CMSIS-DAP Debugger，端口选 SW，设备选你的目标芯片。pyOCD、probe-rs 按平常的 CMSIS-DAP 方式用，不需要额外配置。

### Wi-Fi 配网

探针要先知道热点的名称和密码。用 USB 连接后，电脑上会多出一个虚拟串口（设备管理器里的 `USB 串行设备`），用任意串口终端打开，波特率随意，输入：

```
wifi set 热点名 密码
```

热点名或密码里有空格时用双引号括起来；开放网络不填密码。保存后设备会重启并连接热点。其他命令：

| 命令 | 作用 |
|---|---|
| `wifi status` | 查看热点、是否连接、IP、信号强度 |
| `wifi clear` | 清除已保存的热点参数 |
| `help` | 列出命令 |

热点参数存在设备的 NVS 里（命名空间 `rmlink`，键 `wifi`），格式和 RMLink 固件一致，用 RMLink 配过网的板子不用重配。没有热点参数时 Wi-Fi 不启动，只能用 USB。

### Wi-Fi（TCP）

探针连上 Wi-Fi 后，在 TCP 3241 端口提供 CMSIS-DAP。设备的 IP 用上面的 `wifi status` 查看，也可以在路由器里查。

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
- 有 TCP 客户端连接时，USB 会关闭（虚拟串口也会消失）；客户端断开后 USB 自动恢复。
- 同一时间只服务一个 TCP 客户端。客户端异常消失（没有正常断开）时，约 11 秒后连接会被释放。

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

- SWD 时钟建议先用 4 MHz。能跑多快取决于外部条件：线的长度和阻抗、目标板的上拉和走线、杜邦线质量等。我们的板子用杜邦线，4 MHz 下连续测 5 分钟没有出错；8 MHz 及以上偶尔出现写应答 FAULT（约万分之一个块），OpenOCD 和 probe-rs 会自动重试，所以平时看不到。需要更高时钟的话，建议用示波器看 SWCLK 和 SWDIO 的波形，调整外围电路（缩短线、串联电阻、上拉等），再试更高的时钟。
- 无线每个命令有约 0.4 ms 的固定开销，每次读一个包的方式做不到 1 kHz 的采样，需要探针板上采样的协议，目前没有。

## 许可证

Apache-2.0，见 [LICENSE](LICENSE)。
