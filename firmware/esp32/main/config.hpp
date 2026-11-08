// SPDX-License-Identifier: Apache-2.0
#pragma once

// 传统 ESP32 版 XR-DAP 的固定参数；各芯片共用的在 probe_config.hpp。
//
// Fixed parameters of the classic ESP32 XR-DAP firmware; shared ones are in
// probe_config.hpp.

#include <cstdint>

#include "debug/swd_general_gpio.hpp"
#include "probe_config.hpp"

namespace XrDap::Config
{
/// DAP_Info 产品名 / DAP_Info product name.
inline constexpr char kProductName[] = "XR-DAP-ESP32";
/// mDNS TXT 里的芯片名 / Chip name in the mDNS TXT.
inline constexpr char kChipName[] = "esp32";

/// SWCLK 引脚。避开的引脚：GPIO0/2/5/12/15 是启动配置引脚（GPIO12 上电时为高会选错 Flash
/// 电压），GPIO6 到 11 接 Flash，GPIO34 到 39 只能输入，GPIO1/3 是 UART0。SWCLK 和 SWDIO
/// 都在 GPIO0 到 31 里，同一个输出寄存器 / SWCLK pin. Avoided pins: GPIO0/2/5/12/15 are
/// strapping pins (GPIO12 high at power-up selects the wrong flash voltage), GPIO6 to 11
/// connect the flash, GPIO34 to 39 are input-only, GPIO1/3 are UART0. SWCLK and SWDIO
/// both sit in GPIO0 to 31, the same output register.
constexpr int kSwclkGpio = 18;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 19;

/// 传统 ESP32 的 GPIO 读写走外设总线，比 S3 的专用 GPIO 慢得多 / GPIO access on the
/// classic ESP32 goes over the peripheral bus, much slower than the S3's dedicated GPIO.
/// libxr SwdGeneralGPIO 的时序（CPU 周期 ×10）：带延时时每个 SWCLK 周期 a + b·N，不带延时
/// c。b = 10.0 是一次延时循环的 5 个周期，a 是各路径的平均固定开销；c 取不带延时时最短的
/// 一条路径，所以选无延时路径时任何路径都不会比请求的时钟快 / Timing of libxr's
/// SwdGeneralGPIO (CPU cycles x10): a + b*N per SWCLK period with delay, c without. b =
/// 10.0 is the 5 cycles of one delay-loop iteration and a is the mean fixed cost of the
/// paths; c is the shortest path without delay, so the no-delay path is never faster than
/// the request on any path.
inline constexpr LibXR::Debug::SwdGpioTiming kSwdTiming{100, 289, 350};

/// 执行任务所在的核，核上只有它 / Core of the execution task, which runs alone there.
constexpr int kSwdCore = 1;
/// 串口 DAP 和 TCP 服务所在的核，与 Wi-Fi、lwIP 同核。传统 ESP32 经 GPIO 寄存器驱动
/// SWD，两个核都能访问，SWD 由执行器的轮次和 MemAp::Access 串行；串口和网络命令因此不必
/// 等采样两帧之间的空隙 / Core of the serial DAP and TCP services, shared with Wi-Fi and
/// lwIP. The classic ESP32 drives SWD through the GPIO registers, which both
/// cores reach, and SWD is serialised by the executor turn and MemAp::Access; serial and
/// network commands therefore do not wait for the gaps between sample frames.
constexpr int kTransportCore = 0;
/// 执行任务优先级，为 SWD 核上最高 / Execution task priority, the highest on the SWD
/// core.
constexpr unsigned kDapPriority = 14;
/// libxr 定时器任务优先级 / libxr timer task priority.
constexpr unsigned kLibxrTimerPriority = 3;
/// 串口 DAP 任务优先级 / Serial DAP task priority.
constexpr unsigned kSerialPriority = 12;

/// 串口 DAP 链路走 UART0，接板上的 USB 转串口芯片；同一个口也是 Wi-Fi 命令行 / The serial
/// DAP link uses UART0 wired to the on-board USB-UART chip; the same port also carries
/// the Wi-Fi console.
constexpr int kSerialUart = 0;
/// 串口 TX 引脚 / Serial TX pin.
constexpr int kSerialTxGpio = 1;
/// 串口 RX 引脚 / Serial RX pin.
constexpr int kSerialRxGpio = 3;
/// 串口波特率，受板上 USB 转串口芯片限制 / Serial baud rate, limited by the on-board
/// USB-UART chip.
constexpr uint32_t kSerialBaud = 921600;
}  // namespace XrDap::Config
