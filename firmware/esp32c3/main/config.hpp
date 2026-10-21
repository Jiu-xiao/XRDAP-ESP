// SPDX-License-Identifier: Apache-2.0
#pragma once

// ESP32-C3 版 XR-DAP 的固定参数；各芯片共用的在 probe_config.hpp。
//
// Fixed parameters of the ESP32-C3 XR-DAP firmware; shared ones are in probe_config.hpp.

#include <cstdint>

#include "probe_config.hpp"

namespace XrDap::Config
{
/// DAP_Info 产品名 / DAP_Info product name.
inline constexpr char kProductName[] = "XR-DAP-C3";
/// USB/IP 虚拟设备的产品名 / Product name of the USB/IP virtual device.
inline constexpr char kUsbProductName[] = "XR-DAP-C3 CMSIS-DAP";
/// mDNS TXT 里的芯片名 / Chip name in the mDNS TXT.
inline constexpr char kChipName[] = "esp32c3";

/// SWCLK 引脚。避开的引脚：GPIO2/8/9 是启动配置引脚（不少开发板 GPIO8 接 RGB 灯、GPIO9
/// 是 BOOT 键），GPIO12 到 17 接 Flash，GPIO18/19 是 USB，GPIO20/21 是 UART0 / SWCLK
/// pin. Avoided pins: GPIO2/8/9 are strapping pins (GPIO8 drives an RGB LED and GPIO9 is
/// BOOT on many boards), GPIO12 to 17 connect the flash, GPIO18/19 are USB, GPIO20/21
/// are UART0.
constexpr int kSwclkGpio = 5;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 4;

/// 写一次 SWCLK 沿在每个时钟周期多出的 CPU 周期数，用来校正 SWCLK 频率；偏低就加大，
/// 偏高就减小 / Extra CPU cycles one SWCLK edge write adds to each clock period, used
/// to calibrate the SWCLK frequency; raise it if the frequency is low, lower it if
/// high.
constexpr uint32_t kEdgeWriteCycles = 13U;

/// 所有任务所在的核（C3 为单核）/ Core running every task (the C3 is single-core).
constexpr int kCore = 0;
// 优先级从低到高：libxr 定时器 3 < 命令行 4 < 串口 DAP 8 < 网络服务 10 < lwIP 18 < 采样
// 19 < esp_timer 22 < Wi-Fi 23。采样在节拍之间阻塞，每周期给别的任务留至少 1/8，所以可以
// 排在 lwIP 之上；它低于 esp_timer 和 Wi-Fi 驱动，不拖住无线协议栈。
// Priorities low to high: libxr timer 3 < console 4 < serial DAP 8 < network server 10
// < lwIP 18 < sampling 19 < esp_timer 22 < Wi-Fi 23. Sampling blocks between ticks and
// leaves at least 1/8 of each period to others, so it can sit above lwIP; it stays below
// esp_timer and the Wi-Fi driver so the radio stack is not starved.
/// 采样任务优先级 / Sampling task priority.
constexpr unsigned kSamplingPriority = 19;
/// libxr 定时器任务优先级 / libxr timer task priority.
constexpr unsigned kLibxrTimerPriority = 3;
/// 串口 DAP 任务优先级 / Serial DAP task priority.
constexpr unsigned kSerialPriority = 8;

/// 串口 DAP 链路走芯片自带的 USB Serial/JTAG，波特率对它没有意义 / The serial DAP link
/// uses the built-in USB Serial/JTAG, where the baud rate has no effect.
constexpr uint32_t kSerialBaud = 4000000;

/// UART0 上的 Wi-Fi 命令行，给引出了 UART0 的开发板用；系统日志控制台因此关闭 / Wi-Fi
/// console on UART0 for boards that expose it; the system log console is disabled for
/// it.
constexpr int kConsoleUart = 0;
/// 命令行 TX 引脚 / Console TX pin.
constexpr int kConsoleTxGpio = 21;
/// 命令行 RX 引脚 / Console RX pin.
constexpr int kConsoleRxGpio = 20;
/// 命令行波特率 / Console baud rate.
constexpr uint32_t kConsoleBaud = 115200;
}  // namespace XrDap::Config
