// SPDX-License-Identifier: Apache-2.0
#pragma once

// ESP32-S3 版 XR-DAP 的固定参数；两种芯片共用的在 probe_config.hpp。
//
// Fixed parameters of the ESP32-S3 XR-DAP firmware; shared ones are in probe_config.hpp.

#include <cstdint>

#include "probe_config.hpp"

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

/// SWCLK 引脚 / SWCLK pin.
constexpr int kSwclkGpio = 5;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 4;

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
constexpr bool kSerialLink = true;
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
