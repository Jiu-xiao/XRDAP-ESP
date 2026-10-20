// SPDX-License-Identifier: Apache-2.0
#pragma once

// 传统 ESP32 版 XR-DAP 的固定参数；三种芯片共用的在 probe_config.hpp。
//
// Fixed parameters of the classic ESP32 XR-DAP firmware; shared ones are in
// probe_config.hpp.

#include <cstdint>

#include "probe_config.hpp"

namespace XrDap::Config
{
/// DAP_Info 产品名 / DAP_Info product name.
inline constexpr char kProductName[] = "XR-DAP-ESP32";
/// USB/IP 虚拟设备的产品名 / Product name of the USB/IP virtual device.
inline constexpr char kUsbProductName[] = "XR-DAP-ESP32 CMSIS-DAP";
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

/// 写一次 SWCLK 沿在每个时钟周期多出的 CPU 周期数，用来校正 SWCLK 频率；偏低就加大，
/// 偏高就减小。传统 ESP32 的 GPIO 读写走外设总线（写约 12、读约 17 个 CPU 周期），比 S3
/// 的专用 GPIO 慢得多；28 是按不同时钟下 MemAp 标定一轮的耗时推算的，没有用逻辑分析仪校准
/// / Extra CPU cycles one SWCLK edge write adds to each clock period, used to calibrate
/// the SWCLK frequency; raise it if the frequency is low, lower it if high. GPIO access
/// on the classic ESP32 goes over the peripheral bus (about 12 CPU cycles per write, 17
/// per read), much slower than the S3's dedicated GPIO; 28 was derived from MemAp
/// calibration round times at several clocks, not measured with a logic analyzer.
constexpr uint32_t kEdgeWriteCycles = 28U;

/// SWD、DAP 命令、采样和网络服务所在的核；Wi-Fi 和 lwIP 在核 0 / Core for SWD, DAP
/// commands, sampling and the network server; Wi-Fi and lwIP live on core 0.
constexpr int kSwdCore = 1;
/// 采样任务优先级，为 SWD 核上最高 / Sampling task priority, the highest on the SWD core.
constexpr unsigned kSamplingPriority = 14;
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
