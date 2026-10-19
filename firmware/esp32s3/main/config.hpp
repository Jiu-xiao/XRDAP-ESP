// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// ESP32-S3 版 XR-DAP 的固定参数。
//
// Fixed parameters of the ESP32-S3 XR-DAP firmware.

namespace XrDap::Config
{
/// SWCLK 引脚 / SWCLK pin.
constexpr int kSwclkGpio = 5;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 4;

/// SWD 位带、DAP 的 Step() 线程和 TCP 服务所在的核。专用 GPIO 只能在创建它的核上访问；
/// USB 中断、Wi-Fi、lwIP 都在核 0，所以用核 1 / Core for the SWD bit-banging, the DAP
/// Step() thread and the TCP server. Dedicated GPIO is only accessible from its creating
/// core; USB interrupts, Wi-Fi and lwIP live on core 0, hence core 1.
constexpr int kSwdCore = 1;
/// DAP Step() 线程优先级 / DAP Step() thread priority.
constexpr unsigned kStepPriority = 10;

/// 通告给上位机的 DAP 包长 / DAP packet size advertised to the host.
constexpr uint16_t kDapPacketSize = 1024;
/// TCP DAP 监听端口 / TCP DAP listening port.
constexpr uint16_t kTcpPort = 3241;

/// USB 厂商号，XRUSB 登记的 VID:PID 之 VID。同一 VID:PID 靠序列号区分设备，序列号规则见
/// probe_info.hpp 与 xrobot.work「VID/PID 与 Serial 使用约定」 / USB vendor ID from the
/// XRUSB-registered VID:PID. Devices sharing a VID:PID are told apart by serial number;
/// see probe_info.hpp and the xrobot.work VID/PID and serial convention.
constexpr uint16_t kUsbVid = 0x1D50;
/// USB 产品号 / USB product ID.
constexpr uint16_t kUsbPid = 0x6199;

/// 引脚驱动强度，0 最弱，3 最强（IDF 默认 2）。降一档以减小边沿过冲和振铃；0 档在 10 MHz
/// 以上错得更多 / Pad drive strength, 0 weakest to 3 strongest (IDF default 2). One step
/// lower reduces edge overshoot and ringing; level 0 fails more above 10 MHz.
constexpr int kPadDriveStrength = 1;

/// 厂商扩展采样任务优先级。该任务在 SWD 核上优先级最高，由节拍定时器唤醒、节拍之间阻塞，
/// DAP 命令、TCP、串口在节拍之间运行，USB/TCP 取数据不会挤掉节拍 / Priority of the vendor
/// sampling task. It is the highest-priority task on the SWD core, woken by the tick
/// timer and blocked between ticks; DAP commands, TCP and serial run in the gaps, so
/// USB/TCP readout does not displace ticks.
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
