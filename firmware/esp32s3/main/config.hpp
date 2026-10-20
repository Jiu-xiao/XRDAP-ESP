// SPDX-License-Identifier: Apache-2.0
#pragma once

// ESP32-S3 版 XR-DAP 的固定参数；各芯片共用的在 probe_config.hpp。引脚按板型（Kconfig 的
// XRDAP_BOARD）选：
//   默认：ESP32-S3 开发板，SWCLK GPIO5、SWDIO GPIO4，串口链路走板上 USB 转串口芯片。
//   Lite：“高速无线 DAP 调试器 Lite”，SWCLK GPIO37；SWDIO 经 SGM3157 模拟开关接到目标，
//         开关的 IN（GPIO36）固定为低，NC 端的 GPIO47 当双向 SWDIO 用；板上没有 USB 转
//         串口芯片，不开串口链路。
//
// Fixed parameters of the ESP32-S3 XR-DAP firmware; shared ones are in probe_config.hpp.
// Pins depend on the board (Kconfig XRDAP_BOARD):
//   Default: ESP32-S3 dev board, SWCLK GPIO5, SWDIO GPIO4, serial link through the
//   on-board
//            USB-UART chip.
//   Lite:    "Wireless DAP Lite", SWCLK GPIO37; SWDIO reaches the target through an
//            SGM3157 analog switch whose IN (GPIO36) is held low, so GPIO47 on its NC
//            side serves as the bidirectional SWDIO; no USB-UART chip, so no serial link.

#include <cstdint>

#include "probe_config.hpp"
#include "sdkconfig.h"

namespace XrDap::Config
{
/// DAP_Info 产品名 / DAP_Info product name.
inline constexpr char kProductName[] = "XR-DAP-S3";
/// USB 产品名；probe-rs、pyOCD 按其中的 "CMSIS-DAP" 认设备 / USB product name; probe-rs
/// and pyOCD recognise the device by "CMSIS-DAP" in it.
inline constexpr char kUsbProductName[] = "XR-DAP-S3 CMSIS-DAP";
/// mDNS TXT 里的芯片名 / Chip name in the mDNS TXT.
inline constexpr char kChipName[] = "esp32s3";

/// USB 设备版本号；USB/IP 虚拟设备用 0x0200，Windows 按 VID、PID、版本号缓存描述符，两者
/// 不能相同 / USB device release; the USB/IP virtual device uses 0x0200. Windows caches
/// descriptors by VID, PID and release, so the two must differ.
constexpr uint16_t kUsbBcdDevice = 0x0100;

#if CONFIG_XRDAP_BOARD_LITE
/// SWCLK 引脚 / SWCLK pin.
constexpr int kSwclkGpio = 37;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 47;
/// SWDIO 模拟开关的选择脚，启动时拉低让 kSwdioGpio 接通目标；-1 表示没有 / Select pin of
/// the SWDIO analog switch, driven low at start-up to connect kSwdioGpio to the target;
/// -1 when there is none.
constexpr int kSwdioSelectGpio = 36;
/// 板上有没有接 UART0 的 USB 转串口芯片 / Whether a USB-UART chip is wired to UART0.
constexpr bool kUsbUartOnBoard = false;
#else
/// SWCLK 引脚 / SWCLK pin.
constexpr int kSwclkGpio = 5;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 4;
/// SWDIO 模拟开关的选择脚，-1 表示没有 / Select pin of the SWDIO analog switch; -1 when
/// there is none.
constexpr int kSwdioSelectGpio = -1;
/// 板上有没有接 UART0 的 USB 转串口芯片 / Whether a USB-UART chip is wired to UART0.
constexpr bool kUsbUartOnBoard = true;
#endif

/// SWD 位带、DAP 的 Step() 线程、采样和网络服务所在的核。专用 GPIO 只能在创建它的核上
/// 访问；USB 中断、Wi-Fi、lwIP 在核 0，所以用核 1 / Core for SWD bit-banging, the DAP
/// Step() thread, sampling and the network server. Dedicated GPIO is only accessible
/// from its creating core; USB interrupts, Wi-Fi and lwIP live on core 0, hence core 1.
constexpr int kSwdCore = 1;
/// libxr 定时器任务优先级 / libxr timer task priority.
constexpr unsigned kLibxrTimerPriority = 3;
/// DAP Step() 线程优先级 / DAP Step() thread priority.
constexpr unsigned kStepPriority = 10;
/// 采样任务优先级，为 SWD 核上最高 / Sampling task priority, the highest on the SWD core.
constexpr unsigned kSamplingPriority = 14;

/// 是否启用串口 DAP 链路（libxr Topic 帧）。它占用板上 USB 转串口芯片接的 UART0，因此
/// 系统日志控制台已关闭 / Enable the serial DAP link (libxr Topic frames). It uses UART0
/// wired to the on-board USB-UART chip, so the system log console is disabled.
constexpr bool kSerialLink = kUsbUartOnBoard;
/// 串口 UART 号 / Serial UART number.
constexpr int kSerialUart = 0;
/// 串口 TX 引脚 / Serial TX pin.
constexpr int kSerialTxGpio = 43;
/// 串口 RX 引脚 / Serial RX pin.
constexpr int kSerialRxGpio = 44;
/// 串口波特率。板上 CH343 在 4 Mbaud 下可靠，5 Mbaud 不行 / Serial baud rate. The
/// on-board CH343 is reliable at 4 Mbaud but not at 5 Mbaud.
constexpr uint32_t kSerialBaud = 4000000;
/// 串口 DAP 任务优先级 / Serial DAP task priority.
constexpr unsigned kSerialPriority = 12;
}  // namespace XrDap::Config
