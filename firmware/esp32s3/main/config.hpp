// SPDX-License-Identifier: Apache-2.0
#pragma once

// ESP32-S3 版 XR-DAP 的固定参数；各芯片共用的在 probe_config.hpp。引脚按板型（Kconfig 的
// XRDAP_BOARD）选：
//   默认：ESP32-S3 开发板，SWCLK GPIO5、SWDIO GPIO4，串口链路走板上 USB 转串口芯片；
//         GPIO48 的 RGB 灯（WS2812）显示状态。BOOT 键（GPIO0）只用于进下载模式：它接在
//         USB 转串口芯片的自动下载电路上，串口线的 DTR/RTS 能把它拉低，所以不当运行时
//         按键；清除 Wi-Fi 用电脑端（命令行 wifi clear 或 0x86）。
//   Lite：“高速无线 DAP 调试器 Lite”，SWCLK GPIO37；SWDIO 经 SGM3157 模拟开关接到目标，
//         开关的 IN（GPIO36）固定为低，NC 端的 GPIO47 当双向 SWDIO 用；没有 USB 转串口
//         芯片，不开串口链路；GPIO48 的 WS2812 显示状态，GPIO21 的按键长按 5 s 清除
//         Wi-Fi（hold_button.hpp）。
//
// Fixed parameters of the ESP32-S3 XR-DAP firmware; shared ones are in probe_config.hpp.
// Pins depend on the board (Kconfig XRDAP_BOARD):
//   Default: ESP32-S3 dev board, SWCLK GPIO5, SWDIO GPIO4, serial link through the
//            on-board USB-UART chip; the RGB LED (WS2812) on GPIO48 shows the state. The
//            BOOT key (GPIO0) is for download mode only: it sits on the USB-UART
//            auto-download circuit, where the serial DTR/RTS lines can pull it low, so it
//            is not a runtime button; Wi-Fi is cleared from the PC (console wifi clear or
//            0x86).
//   Lite:    "Wireless DAP Lite", SWCLK GPIO37; SWDIO reaches the target through an
//            SGM3157 analog switch whose IN (GPIO36) is held low, so GPIO47 on its NC
//            side serves as the bidirectional SWDIO; no USB-UART chip, so no serial
//            link; the WS2812 on GPIO48 shows the state and holding the GPIO21 button
//            for 5 s clears Wi-Fi (hold_button.hpp).

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

#if CONFIG_XRDAP_BOARD_LITE
/// SWCLK 引脚 / SWCLK pin.
constexpr int kSwclkGpio = 37;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 47;
/// SWDIO 模拟开关的选择脚，启动时拉低让 kSwdioGpio 接通目标；-1 表示没有 / Select pin of
/// the SWDIO analog switch, driven low at start-up to connect kSwdioGpio to the target;
/// -1 when there is none.
constexpr int kSwdioSelectGpio = 36;
/// 是否启用串口 DAP 链路；Lite 板没有 USB 转串口芯片 / Enable the serial DAP link; the
/// Lite board has no USB-UART chip.
constexpr bool kSerialLink = false;
/// 状态灯（WS2812）数据脚 / Status light (WS2812) data pin.
constexpr int kStatusLedGpio = 48;
/// Wi-Fi 清除按键（按下接地）/ Wi-Fi clear button (shorts to ground).
constexpr int kWifiButtonGpio = 21;
#else
/// SWCLK 引脚 / SWCLK pin.
constexpr int kSwclkGpio = 5;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 4;
/// SWDIO 模拟开关的选择脚，-1 表示没有 / Select pin of the SWDIO analog switch; -1 when
/// there is none.
constexpr int kSwdioSelectGpio = -1;
/// 是否启用串口 DAP 链路（libxr Topic 帧），走板上 USB 转串口芯片接的 UART0，因此系统
/// 日志控制台已关闭 / Enable the serial DAP link (libxr Topic frames) on UART0, wired to
/// the on-board USB-UART chip, so the system log console is disabled.
constexpr bool kSerialLink = true;
/// 状态灯（WS2812）数据脚；DevKitC-1 v1.0 一类的板子在 GPIO48，没有灯的板子上它只是
/// 一个空闲引脚 / Status light (WS2812) data pin; GPIO48 on DevKitC-1 v1.0 style boards,
/// and just an unused pin on boards without the LED.
constexpr int kStatusLedGpio = 48;
/// 没有 Wi-Fi 清除按键，BOOT 键只用于下载模式（见文件开头）/ No Wi-Fi clear button; the
/// BOOT key is for download mode only (see the top of this file).
constexpr int kWifiButtonGpio = -1;
#endif

/// 按住 Wi-Fi 清除按键多久才清除 / How long the Wi-Fi clear button must be held.
constexpr uint32_t kWifiClearHoldMs = 5000;

/// SWD 位带、DAP 的 Step() 线程、采样和网络服务所在的核。专用 GPIO 只能在创建它的核上
/// 访问；USB 中断、Wi-Fi、lwIP 在核 0，所以用核 1 / Core for SWD bit-banging, the DAP
/// Step() thread, sampling and the network server. Dedicated GPIO is only accessible
/// from its creating core; USB interrupts, Wi-Fi and lwIP live on core 0, hence core 1.
constexpr int kSwdCore = 1;
/// libxr 定时器任务优先级 / libxr timer task priority.
constexpr unsigned kLibxrTimerPriority = 3;
/// DAP Step() 线程优先级 / DAP Step() thread priority.
constexpr unsigned kStepPriority = 10;
/// USB 线程等一条 USB 命令的最长时间。执行器在等待时持锁，没有主人时第一条 TCP 或串口
/// 命令最多多等这么久 / Longest wait of the USB thread for one USB command. The executor
/// holds its lock while waiting, so without an owner the first TCP or serial command
/// waits at most this much longer.
constexpr uint32_t kUsbWaitMs = 10;
/// 采样任务优先级，为 SWD 核上最高 / Sampling task priority, the highest on the SWD core.
constexpr unsigned kSamplingPriority = 14;

/// 串口 UART 号 / Serial UART number.
constexpr int kSerialUart = 0;
/// 串口 TX 引脚 / Serial TX pin.
constexpr int kSerialTxGpio = 43;
/// 串口 RX 引脚 / Serial RX pin.
constexpr int kSerialRxGpio = 44;
/// 串口波特率，取板上 USB 转串口芯片（CH343）能稳定工作的最高速率 / Serial baud rate,
/// the highest the on-board USB-UART chip (CH343) runs reliably.
constexpr uint32_t kSerialBaud = 4000000;
/// 串口 DAP 任务优先级 / Serial DAP task priority.
constexpr unsigned kSerialPriority = 12;
}  // namespace XrDap::Config
