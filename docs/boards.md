# 板子

[English](boards.en.md)

本文列出 XRDAP-ESP 支持的各块板子的引脚、下载模式、状态灯和按键，以及接线的注意事项。各板的固件文件见 README 的“板子”一节。

## ESP32-S3 开发板

固件 `xrdap-esp32-s3.bin`，用于带 USB 转串口芯片（CH343）的 ESP32-S3 开发板。

| 用途 | 引脚 |
|---|---|
| SWCLK | GPIO5 |
| SWDIO | GPIO4 |
| 原生 USB（板上标 USB 的口） | D- GPIO19、D+ GPIO20 |
| 串口链路 | UART0（TX GPIO43、RX GPIO44），经板上的 USB 转串口芯片，4 Mbaud |
| 状态灯 | GPIO48 的 RGB 灯（WS2812），ESP32-S3-DevKitC-1 v1.0 一类的板子 |
| BOOT 键 | GPIO0，只用于进入下载模式 |

BOOT 键接在 USB 转串口芯片的自动下载电路上，串口的 DTR、RTS 可以把它拉低，因此它只用于进入下载模式。这块板子的 Wi-Fi 设置用 `xrdap wifi clear` 或命令行 `wifi clear` 清除。

## “高速无线 DAP 调试器 Lite”板

固件 `xrdap-esp32-s3-lite.bin`。板上为 ESP32-S3 模组和 2×5 排针，与电脑之间使用原生 USB 和 Wi-Fi。

| 用途 | 引脚 |
|---|---|
| SWCLK | GPIO37 |
| SWDIO | 经板上的 SGM3157 模拟开关接到排针；固件把开关的选择脚 GPIO36 固定为低，用 GPIO47 收发 SWDIO |
| 状态灯 | 板上的 WS2812，GPIO48 |
| SW1 | GPIO0，插上 USB 时按住进入下载模式 |
| SW2 | GPIO21，长按 5 s 清除 Wi-Fi 设置并重启 |

模组使用不带 Octal PSRAM 的型号（WROOM-1-N4、N8，MINI-1-N4、N8），带 Octal PSRAM 的型号占用 GPIO35 到 GPIO37。

## ESP32-S2 板

固件 `xrdap-esp32-s2.bin`。板上为 ESP32-S2FH4（芯片内 4 MB Flash，不带 PSRAM）和 2×5 排针，与电脑之间使用原生 USB 和 Wi-Fi。

| 用途 | 引脚 |
|---|---|
| SWCLK | GPIO37 |
| SWDIO | GPIO35，经 0 Ω 电阻接到排针，板上的 SGM3157 不焊 |
| 目标复位（nRESET） | GPIO40，开漏输出，启动时保持释放，不复位目标 |
| 状态灯 | WS2812，GPIO17 |
| KEY | GPIO21，长按 5 s 清除 Wi-Fi 设置并重启 |
| BOOT | 插上 USB 时按住进入下载模式 |

排针上另有 TDI（GPIO38）、TDO（GPIO36）和目标串口 TXD（GPIO42）、RXD（GPIO41），当前固件不使用。S2 为单核，内部 RAM 为 320 KB，Wi-Fi 缓冲和任务栈比 S3 小。

## ESP32-C3 开发板

固件 `xrdap-esp32-c3.bin`。

| 用途 | 引脚 |
|---|---|
| SWCLK | GPIO5 |
| SWDIO | GPIO4 |
| 芯片自带的 USB 口（USB Serial/JTAG） | D- GPIO18、D+ GPIO19，承载串口链路和 Wi-Fi 命令行 |
| UART0 | TX GPIO21、RX GPIO20，Wi-Fi 命令行，115200 baud |

探针经芯片自带的 USB 口连接电脑。USB 座直接连到 GPIO18、GPIO19 的开发板经这个 USB 座连接；USB 座接 USB 转串口芯片的开发板（例如 ESP32-C3-DevKitM-1）另接一根 USB 线到排针的 GPIO18、GPIO19、5V 和 GND。引出 UART0 的开发板上，UART0 也提供 Wi-Fi 命令行。

## 传统 ESP32 开发板

固件 `xrdap-esp32.bin`，用于带 USB 转串口芯片的 ESP32 开发板。

| 用途 | 引脚 |
|---|---|
| SWCLK | GPIO18 |
| SWDIO | GPIO19 |
| 串口链路和 Wi-Fi 命令行 | UART0（TX GPIO1、RX GPIO3），即板上 USB 转串口芯片所接的口，921600 baud |

GPIO5 是传统 ESP32 的启动配置引脚，因此 SWD 使用 GPIO18 和 GPIO19。

## 下载模式

| 板子 | 进入下载模式 |
|---|---|
| ESP32-S3 开发板 | 经 USB 转串口芯片的口时，esptool 和网页烧录经自动下载电路进入；经原生 USB 口时，按住 BOOT 插上 USB，或按住 BOOT 短按 RST |
| 传统 ESP32 开发板 | esptool 和网页烧录经 USB 转串口芯片的自动下载电路进入 |
| ESP32-C3 开发板 | esptool 和网页烧录经芯片自带的 USB 口进入 |
| ESP32-S2 板 | 按住 BOOT 插上 USB，然后松开 |
| Lite 板 | 拔下 USB，按住 SW1 再插上，然后松开；电脑上出现原生 USB 的串口 `303A:1001`，esptool 加 `--after watchdog_reset` |

有的开发板的自动下载电路只能复位芯片、无法拉低 GPIO0，esptool 报 `Wrong boot mode`。这时按住 BOOT，短按 EN（RESET），松开 BOOT，再在 esptool 命令中加 `--before no_reset`。

## 状态灯和按键

S3 开发板、Lite 板和 S2 板有状态灯，显示规则与 XR-DAP 其他探针相同（XRDAP-Core README 的“状态灯”一节）：

| 灯 | 含义 |
|---|---|
| 彩虹扫过一次 | 上电 |
| 红 | 有线：USB 已连接电脑，或 5 s 内有经 USB、串口来的命令 |
| 蓝 | 无线：有网络调试客户端（TCP 端口上的 OpenOCD 或 `xrdap`，包括 `xrdap share`），或只连着 Wi-Fi |
| 紫 | USB 和 Wi-Fi 都没有连接；正在连接 Wi-Fi 时呼吸 |
| 主色与紫色来回，2 s 一次 | 上位机经 DAP_HostStatus 报告已连接 |
| 红蓝来回，1 s 一次 | 上位机报告目标运行中 |
| 闪亮 | 每条 DAP 命令（经 USB、串口或 Wi-Fi）使灯先变亮再逐渐变暗，命令连续时灯持续闪烁 |
| 紫色快闪 | Lite 板的 SW2 或 S2 板的 KEY 正被按住；按满 5 s 后变为紫色常亮，清除热点参数并重启 |
| 紫色常亮 | 热点参数刚被修改或清除（按键、命令行、`xrdap wifi` 或 0x86 命令），探针随即重启 |

USB 和 Wi-Fi 都连接时按有线显示红色。上位机异常退出、没有发送 DAP_Disconnect 时，5 s 内没有命令后灯也回到主色。

Lite 板的 SW2 和 S2 板的 KEY 长按 5 s 清除热点参数并重启。各开发板的 BOOT 键只用于进入下载模式。

## 接线和 SWD 时钟

探针的 SWCLK、SWDIO 和 GND 接到目标的 SWD 口，一个目标只接一个探针。SWD 能稳定运行的时钟取决于线长、目标板上的上拉和走线，一般从 4 MHz 开始；高时钟下出错时检查 SWCLK、SWDIO 的波形，缩短连线或加串联电阻。各芯片的最高时钟见 [performance.md](performance.md)。
