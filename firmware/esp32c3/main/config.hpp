// SPDX-License-Identifier: Apache-2.0
#pragma once

// ESP32-C3 版 XR-DAP 的固定参数；各芯片共用的在 probe_config.hpp。
//
// Fixed parameters of the ESP32-C3 XR-DAP firmware; shared ones are in probe_config.hpp.

#include <cstdint>

#include "debug/swd_general_gpio.hpp"
#include "probe_config.hpp"

namespace XrDap::Config
{
/// DAP_Info 产品名 / DAP_Info product name.
inline constexpr char kProductName[] = "XR-DAP-C3";
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

/// libxr SwdGeneralGPIO 的时序（CPU 周期 ×10）：带延时时每个 SWCLK 周期 a + b·N。160 MHz
/// 下用 CPU 周期计数器实测：请求字节、读数据相、写数据相和 SWJ 序列按 N >= 2 拟合，
/// b = 8.0（一次延时循环 4 个周期），a 取各路径的平均 24.7。c 设为 0，不用无延时路径：
/// 无延时时读一位在 SWCLK 拉低后紧接着采 SWDIO，离上一个上升沿只有十几 ns，目标还没送出
/// 新的一位（实测读到上一位，Cortex-M4 和 M7 目标都会出错）；N = 1 在采样前多等约 25 ns，
/// 没有错误。最快一档是 N = 1，约 5.2 MHz / Timing of libxr's SwdGeneralGPIO (CPU
/// cycles x10): a + b*N per SWCLK period with delay. Measured with the CPU cycle counter
/// at 160 MHz: the request byte, read and write data phases and SWJ sequence fit b = 8.0
/// (4 cycles per delay-loop iteration) for N >= 2, and a is their mean, 24.7. c is 0, so
/// the path without delay is never used: there a read samples SWDIO right after pulling
/// SWCLK low, a dozen ns after the previous rising edge, before the target drives the new
/// bit (measured: the previous bit is read, on Cortex-M4 and M7 targets). N = 1 waits
/// about 25 ns more before sampling and reads correctly. The fastest step is N = 1,
/// about 5.2 MHz.
inline constexpr LibXR::Debug::SwdGpioTiming kSwdTiming{80, 247, 0};

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
