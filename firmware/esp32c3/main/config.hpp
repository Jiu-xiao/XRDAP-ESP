// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "sdkconfig.h"

// ESP32-C3 版 XR-DAP 的固定参数。
//
// Fixed parameters of the ESP32-C3 XR-DAP firmware.

namespace XrDap::Config
{
/// SWCLK 引脚，与 S3 版相同。避开的引脚：GPIO2/8/9 是启动配置引脚（不少开发板 GPIO8 接
/// RGB 灯、GPIO9 是 BOOT 键），GPIO12 到 17 接 Flash，GPIO18/19 是 USB，GPIO20/21 是
/// UART0。GPIO4 到 7 也是 JTAG 引脚，仅在烧写 eFuse 改用引脚 JTAG 时才被占用，默认空闲 /
/// SWCLK pin, same as the S3. Avoided pins: GPIO2/8/9 are strapping pins (GPIO8 drives
/// an RGB LED and GPIO9 is BOOT on many boards), GPIO12 to 17 connect the flash,
/// GPIO18/19 are USB, GPIO20/21 are UART0. GPIO4 to 7 are also JTAG pins, used only if an
/// eFuse selects pin JTAG, so they are free by default.
constexpr int kSwclkGpio = 5;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 4;

/// 引脚驱动强度，0 最弱，3 最强；沿用 S3 版的 1 / Pad drive strength, 0 weakest to 3
/// strongest; 1, the same as the S3.
constexpr int kPadDriveStrength = 1;

/// 写一次 SWCLK 沿本身在每个时钟周期多出的 CPU 周期数，用来校正 SWCLK 频率。S3 实测为
/// 13；RISC-V 上一次写沿是两条 CSR 指令加循环，尚未实测，暂用同一值。用逻辑分析仪量出
/// 实际频率后调整：偏低就加大，偏高就减小 / Extra CPU cycles one SWCLK edge write adds to
/// each clock period, used to calibrate the SWCLK frequency. Measured as 13 on the S3; a
/// RISC-V edge write is two CSR instructions plus a loop and is not yet measured, so the
/// same value is used. Tune it against a logic analyzer: raise it if the frequency is
/// low, lower it if high.
constexpr uint32_t kEdgeWriteCycles = 13U;

/// 通告给上位机的 DAP 包长 / DAP packet size advertised to the host.
constexpr uint16_t kDapPacketSize = 1024;
/// TCP DAP 监听端口 / TCP DAP listening port.
constexpr uint16_t kTcpPort = 3241;
/// USB/IP 虚拟 USB 探针的监听端口 / USB/IP virtual USB probe listening port.
constexpr uint16_t kUsbipPort = 3240;

/// 所有任务所在的核（C3 为单核）/ Core running every task (the C3 is single-core).
/// 优先级从低到高：空闲 0 < libxr 定时器 3 < Wi-Fi 命令行 4 < 串口 DAP 8 < TCP DAP 10 <
/// lwIP 18 < 采样 19 < esp_timer 22 < Wi-Fi 23。采样任务由节拍定时器唤醒、节拍之间阻塞，
/// 每周期至少留 1/8 给其他任务，因此可排在串口、TCP、lwIP 之上：取数据不再挤掉节拍，
/// lwIP 最多晚一拍。它低于 esp_timer 和 Wi-Fi 驱动，不拖住无线协议栈；Wi-Fi 收发仍偶尔
/// 挤掉节拍，这是单核的限制 / Priorities low to high: idle 0 < libxr timer 3 < Wi-Fi
/// console 4 < serial DAP 8 < TCP DAP 10 < lwIP 18 < sampling 19 < esp_timer 22 < Wi-Fi
/// 23. The sampling task wakes on the tick timer, blocks between ticks and leaves at
/// least 1/8 of each period to others, so it can sit above serial, TCP and lwIP: readout
/// no longer displaces ticks and lwIP lags at most one tick. It stays below esp_timer and
/// the Wi-Fi driver so the radio stack is not starved; Wi-Fi traffic can still displace
/// ticks, a single-core limit.
constexpr int kCore = 0;
/// 采样任务优先级 / Sampling task priority.
constexpr unsigned kSamplingPriority = 19;
/// libxr 定时器任务优先级 / libxr timer task priority.
constexpr unsigned kLibxrTimerPriority = 3;
/// 串口 DAP 任务优先级 / Serial DAP task priority.
constexpr unsigned kSerialPriority = 8;

/// 串口 DAP 链路走芯片自带的 USB Serial/JTAG（电脑上是 CDC 串口），无需 USB 转串口芯片。
/// 波特率对它没有意义，xrdap-forward 的 --baud 填什么都行 / The serial DAP link uses the
/// built-in USB Serial/JTAG (a CDC port on the PC), no USB-UART chip needed. The baud
/// rate is meaningless for it; any xrdap-forward --baud works.
constexpr uint32_t kSerialBaud = 4000000;

/// UART0 上也有一个 Wi-Fi 命令行，给引出了 UART0 的开发板用，因此系统日志控制台已关闭
/// （libxr 的 UART 驱动不能与控制台共用 UART）/ UART0 also carries a Wi-Fi console for
/// boards that expose it, so the system log console is disabled (the libxr UART driver
/// cannot share a UART with the console).
constexpr int kConsoleUart = 0;
/// 命令行 TX 引脚 / Console TX pin.
constexpr int kConsoleTxGpio = 21;
/// 命令行 RX 引脚 / Console RX pin.
constexpr int kConsoleRxGpio = 20;
/// 命令行波特率 / Console baud rate.
constexpr uint32_t kConsoleBaud = 115200;
}  // namespace XrDap::Config
