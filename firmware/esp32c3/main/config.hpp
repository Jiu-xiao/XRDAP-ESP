// SPDX-License-Identifier: Apache-2.0
#pragma once

// ESP32-C3 版 XR-DAP 的固定参数；各芯片共用的在 probe_config.hpp。
//
// Fixed parameters of the ESP32-C3 XR-DAP firmware; shared ones are in probe_config.hpp.

#include <cstdint>

#include "debug/swd_general_gpio.hpp"
#include "freertos/FreeRTOS.h"
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
// 优先级从低到高：libxr 定时器 3 < 命令行 4 < 串口 DAP 8 < 网络服务 10 < lwIP 18 <
// esp_timer 22 < Wi-Fi 23 < 执行任务 24（与单核 S2 相同），中断仍可抢占。执行任务没有事做
// 时阻塞，每帧之后仍留 max(15 us, 周期/8, 帧耗时/4) 给其他任务。排在 Wi-Fi 之下时，经
// Wi-Fi 收发时 Wi-Fi 任务偶尔占住 CPU 超过一个 1 ms 节拍，采样跳拍（实测 TCP 1 ms 单变量
// 40 次 10 s 采样有 4 次跳 1 拍，改为 24 后 30 次 0 次）。
// Priorities low to high: libxr timer 3 < console 4 < serial DAP 8 < network server 10
// < lwIP 18 < esp_timer 22 < Wi-Fi 23 < execution task 24 (as on the single-core S2);
// interrupts still preempt it. The execution task blocks when it has nothing to do and
// keeps max(15 us, period/8, frame time/4) after each frame for other tasks. Below Wi-Fi,
// the Wi-Fi task sometimes held the CPU across a 1 ms tick while traffic went over Wi-Fi
// and sampling skipped it (measured over TCP, 1 variable at 1 ms: 4 of 40 10 s runs
// skipped one tick; at 24, 0 of 30).
/// 执行任务优先级 / Execution task priority.
constexpr unsigned kDapPriority = 24;
static_assert(kDapPriority < configMAX_PRIORITIES,
              "DAP task priority must fit the FreeRTOS priority range");
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
