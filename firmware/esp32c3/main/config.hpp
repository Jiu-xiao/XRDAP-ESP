// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "sdkconfig.h"

// ESP32-C3 版 XR-DAP 的固定参数。

namespace XrDap::Config
{
// SWD 引脚，和 S3 版相同。避开的引脚：GPIO2/8/9 是启动配置引脚（不少开发板 GPIO8 接 RGB 灯，
// GPIO9 是 BOOT 键），GPIO12 到 17 接 Flash，GPIO18/19 是 USB，GPIO20/21 是 UART0。
// GPIO4 到 7 也是 JTAG 引脚，只有烧了 eFuse 改用引脚 JTAG 时才被占用，默认空闲。
constexpr int kSwclkGpio = 5;
constexpr int kSwdioGpio = 4;

// 引脚驱动强度，0 最弱，3 最强。沿用 S3 版的 1（S3 上实测减小过冲）。
constexpr int kPadDriveStrength = 1;

// 写一次 SWCLK 沿本身在每个时钟周期里多出的 CPU 周期数，用来校正 SWCLK 频率。
// S3 实测是 13；RISC-V 上一次写沿是两条 CSR 指令加循环，没有实测，先用同一个数。
// 用逻辑分析仪量出实际频率后再改：频率偏低就改大，偏高就改小。
constexpr uint32_t kEdgeWriteCycles = 13U;

constexpr uint16_t kDapPacketSize = 1024;  // 通告给上位机的 DAP 包长
constexpr uint16_t kTcpPort = 3241;

// 单核芯片，所有任务都在核 0。优先级从低到高：
//   空闲任务 0 < libxr 定时器 3 < Wi-Fi 命令行 4 < 串口 DAP 8 < TCP DAP 10 < lwIP 18
//   < 采样 19 < esp_timer 22 < Wi-Fi 23。
// 采样任务由节拍定时器唤醒，节拍之间阻塞，每个周期至少留 1/8 周期给别的任务，所以可以排在
// 串口、TCP 和 lwIP 之上：上位机取数据时不再挤掉节拍，lwIP 最多晚一拍处理。它低于 esp_timer
// 和 Wi-Fi 驱动，不拖住无线协议栈；Wi-Fi 收发仍会偶尔挤掉节拍，这是单核的限制。
constexpr int kCore = 0;
constexpr unsigned kSamplingPriority = 19;
constexpr unsigned kLibxrTimerPriority = 3;
constexpr unsigned kSerialPriority = 8;

// 串口 DAP 链路走芯片自带的 USB Serial/JTAG（电脑上是一个 CDC 串口），不需要 USB 转串口芯片。
// 波特率对 USB Serial/JTAG 没有意义，xrdap-forward 的 --baud 填什么都行。
constexpr uint32_t kSerialBaud = 4000000;

// UART0（GPIO21 TX、GPIO20 RX，有的开发板接了 USB 转串口芯片）上也有一个 Wi-Fi 命令行，
// 系统日志控制台因此关闭（libxr 的 UART 驱动不能和控制台共用一个 UART）。
constexpr int kConsoleUart = 0;
constexpr int kConsoleTxGpio = 21;
constexpr int kConsoleRxGpio = 20;
constexpr uint32_t kConsoleBaud = 115200;
}  // namespace XrDap::Config
