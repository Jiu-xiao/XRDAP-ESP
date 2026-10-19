// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "sdkconfig.h"

// ESP32-C3 / ESP32-H2 版 XR-DAP 的固定参数。

namespace XrDap::Config
{
// SWD 引脚。两块开发板（ESP32-C3-DevKitM-1、ESP32-H2-DevKitM-1）都用 GPIO5 / GPIO4，和 S3 版相同。
// 避开的引脚：
//   C3：GPIO2/8/9 是启动配置引脚（GPIO8 还接了板上 RGB 灯，GPIO9 是 BOOT 键），
//       GPIO12 到 17 接 Flash，GPIO18/19 是 USB，GPIO20/21 是 UART0（板上 USB 转串口）。
//   H2：GPIO2/3/8/9/25 是启动配置引脚（GPIO8 接 RGB 灯，GPIO9 是 BOOT 键），
//       GPIO15 到 21 接内部 Flash，GPIO23/24 是 UART0，GPIO26/27 是 USB。
// C3 的 GPIO4 到 7、H2 的 GPIO2 到 5 也是 JTAG 引脚，只有烧了 eFuse 改用引脚 JTAG 时才被占用，默认空闲。
constexpr int kSwclkGpio = 5;
constexpr int kSwdioGpio = 4;

// 引脚驱动强度，0 最弱，3 最强。沿用 S3 版的 1（S3 上实测减小过冲）；C3/H2 上没有实测。
constexpr int kPadDriveStrength = 1;

// 写一次 SWCLK 沿本身在每个时钟周期里多出的 CPU 周期数，用来校正 SWCLK 频率。
// S3 实测是 13；RISC-V 上一次写沿是两条 CSR 指令加循环，没有实测，先用同一个数。
// 用逻辑分析仪量出实际频率后再改：频率偏低就改大，偏高就改小。
constexpr uint32_t kEdgeWriteCycles = 13U;

constexpr uint16_t kDapPacketSize = 1024;  // 通告给上位机的 DAP 包长
constexpr uint16_t kTcpPort = 3241;        // 只有 C3 有 Wi-Fi

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

#if CONFIG_IDF_TARGET_ESP32C3
// C3：UART0（GPIO21 TX、GPIO20 RX，开发板上接 USB 转串口芯片）给 Wi-Fi 命令行用，
// 系统日志控制台因此关闭（libxr 的 UART 驱动不能和控制台共用一个 UART）。
constexpr bool kHasWifi = true;
constexpr int kConsoleUart = 0;
constexpr int kConsoleTxGpio = 21;
constexpr int kConsoleRxGpio = 20;
constexpr uint32_t kConsoleBaud = 115200;
#else
// H2：没有 Wi-Fi，UART0（GPIO24 TX、GPIO23 RX）是普通的系统日志控制台。
constexpr bool kHasWifi = false;
#endif
}  // namespace XrDap::Config
