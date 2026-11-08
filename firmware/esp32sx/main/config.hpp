// SPDX-License-Identifier: Apache-2.0
#pragma once

// 带原生 USB OTG 的 ESP32-S2、ESP32-S3 版 XR-DAP 的固定参数；各芯片共用的在
// probe_config.hpp。板型由 Kconfig 的 XRDAP_BOARD 选（S2 目标只有 S2 板）：
//   默认：ESP32-S3 开发板，SWCLK GPIO5、SWDIO GPIO4，串口链路走板上 USB 转串口芯片；
//         GPIO48 的 RGB 灯（WS2812）显示状态。BOOT 键（GPIO0）只用于进下载模式：它接在
//         USB 转串口芯片的自动下载电路上，串口线的 DTR/RTS 能把它拉低，所以不当运行时
//         按键；清除 Wi-Fi 用电脑端（命令行 wifi clear 或 0x86）。
//   Lite：“高速无线 DAP 调试器 Lite”（ESP32-S3），SWCLK GPIO37；SWDIO 经 SGM3157 模拟
//         开关接到目标，开关的 IN（GPIO36）固定为低，NC 端的 GPIO47 当双向 SWDIO 用；
//         没有 USB 转串口芯片，不开串口链路；GPIO48 的 WS2812 显示状态，GPIO21 的按键
//         长按 5 s 清除 Wi-Fi（hold_button.hpp）。
//   S2：ESP32-S2FH4 板（芯片内 4 MB Flash，没有 PSRAM），SWCLK GPIO37，SWDIO GPIO35
//         （经 0 Ω 电阻，模拟开关不焊），nRESET GPIO40；没有 USB 转串口芯片，不开排针
//         串口；GPIO17 的 WS2812 显示状态，GPIO21 的按键长按 5 s 清除 Wi-Fi。
//         BOOT 键只用于下载。排针上的 TDI（GPIO38）、TDO（GPIO36）留给 JTAG，串口（GPIO42
//         TXD、GPIO41 RXD）固件不使用。单核：所有任务都在核 0。
//
// Fixed parameters of the XR-DAP firmware for the ESP32-S2 and ESP32-S3 with native USB
// OTG; shared ones are in probe_config.hpp. The board comes from Kconfig XRDAP_BOARD
// (the S2 target has only the S2 board):
//   Default: ESP32-S3 dev board, SWCLK GPIO5, SWDIO GPIO4, serial link through the
//            on-board USB-UART chip; the RGB LED (WS2812) on GPIO48 shows the state. The
//            BOOT key (GPIO0) is for download mode only: it sits on the USB-UART
//            auto-download circuit, where the serial DTR/RTS lines can pull it low, so it
//            is not a runtime button; Wi-Fi is cleared from the PC (console wifi clear or
//            0x86).
//   Lite:    "Wireless DAP Lite" (ESP32-S3), SWCLK GPIO37; SWDIO reaches the target
//            through an SGM3157 analog switch whose IN (GPIO36) is held low, so GPIO47 on
//            its NC side serves as the bidirectional SWDIO; no USB-UART chip, so no
//            serial link; the WS2812 on GPIO48 shows the state and holding the GPIO21
//            button for 5 s clears Wi-Fi (hold_button.hpp).
//   S2:      ESP32-S2FH4 board (4 MB flash in the package, no PSRAM), SWCLK GPIO37,
//            SWDIO GPIO35 (through a 0 ohm resistor, the analog switch not fitted),
//            nRESET GPIO40; no USB-UART chip, so no serial link;
//            the WS2812 on GPIO17 shows the state, holding the GPIO21 button for 5 s
//            clears Wi-Fi, and BOOT is
//            for download mode only. TDI (GPIO38) and TDO (GPIO36) on the header are kept
//            for JTAG; the header UART (GPIO42 TXD, GPIO41 RXD) is not used by the
//            firmware. Single core: every task runs on core 0.

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "probe_config.hpp"
#include "sdkconfig.h"
#include "xrdap/probe/family.hpp"

namespace XrDap::Config
{
#if CONFIG_IDF_TARGET_ESP32S2
/// DAP_Info 产品名 / DAP_Info product name.
inline constexpr char kProductName[] = "XR-DAP-S2";
/// USB 产品名；probe-rs、pyOCD 按其中的 "CMSIS-DAP" 认设备 / USB product name; probe-rs
/// and pyOCD recognise the device by "CMSIS-DAP" in it.
inline constexpr char kUsbProductName[] = "XR-DAP-S2 CMSIS-DAP";
/// mDNS TXT 里的芯片名 / Chip name in the mDNS TXT.
inline constexpr char kChipName[] = "esp32s2";
/// USB 设备版本号（XRDAP-Core family.hpp）/ USB device release (XRDAP-Core family.hpp).
inline constexpr uint16_t kUsbBcdDevice = Family::kBcdEsp32S2;
#else
/// DAP_Info 产品名 / DAP_Info product name.
inline constexpr char kProductName[] = "XR-DAP-S3";
/// USB 产品名；probe-rs、pyOCD 按其中的 "CMSIS-DAP" 认设备 / USB product name; probe-rs
/// and pyOCD recognise the device by "CMSIS-DAP" in it.
inline constexpr char kUsbProductName[] = "XR-DAP-S3 CMSIS-DAP";
/// mDNS TXT 里的芯片名 / Chip name in the mDNS TXT.
inline constexpr char kChipName[] = "esp32s3";
/// USB 设备版本号（XRDAP-Core family.hpp）/ USB device release (XRDAP-Core family.hpp).
inline constexpr uint16_t kUsbBcdDevice = Family::kBcdEsp32S3;
#endif

#if CONFIG_XRDAP_BOARD_S2
/// SWCLK 引脚 / SWCLK pin.
constexpr int kSwclkGpio = 37;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 35;
/// SWDIO 模拟开关的选择脚，-1 表示没有 / Select pin of the SWDIO analog switch; -1 when
/// there is none.
constexpr int kSwdioSelectGpio = -1;
/// 目标 nRESET 引脚（开漏，上电释放），-1 表示没有 / Target nRESET pin (open drain,
/// released at power-up); -1 when there is none.
constexpr int kNresetGpio = 40;
/// 排针没有串口链路 / No serial link on the header.
constexpr bool kSerialLink = false;
/// 状态灯（WS2812，3.3 V 供电）数据脚 / Status light (WS2812, 3.3 V supply) data pin.
constexpr int kStatusLedGpio = 17;
/// Wi-Fi 清除按键（按下接地）/ Wi-Fi clear button (shorts to ground).
constexpr int kWifiButtonGpio = 21;
#elif CONFIG_XRDAP_BOARD_LITE
/// SWCLK 引脚 / SWCLK pin.
constexpr int kSwclkGpio = 37;
/// SWDIO 引脚 / SWDIO pin.
constexpr int kSwdioGpio = 47;
/// SWDIO 模拟开关的选择脚，启动时拉低让 kSwdioGpio 接通目标；-1 表示没有 / Select pin of
/// the SWDIO analog switch, driven low at start-up to connect kSwdioGpio to the target;
/// -1 when there is none.
constexpr int kSwdioSelectGpio = 36;
/// 目标 nRESET 引脚，-1 表示没有 / Target nRESET pin; -1 when there is none.
constexpr int kNresetGpio = -1;
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
/// 目标 nRESET 引脚，-1 表示没有 / Target nRESET pin; -1 when there is none.
constexpr int kNresetGpio = -1;
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

/// libxr 定时器任务优先级 / libxr timer task priority.
constexpr unsigned kLibxrTimerPriority = 3;

#if CONFIG_IDF_TARGET_ESP32S2
/// 所有任务所在的核（S2 为单核）/ Core running every task (the S2 is single-core).
constexpr int kSwdCore = 0;
// 单核 S2：执行任务高于 Wi-Fi(23) / lwIP(18)，中断仍可抢占。它在没有事做时阻塞，
// 每帧之后仍留 max(15 us, 周期/8, 帧耗时/4) 给其他任务。
// Single-core S2: the execution task preempts Wi-Fi (23) / lwIP (18), but not interrupts.
// It blocks when it has nothing to do and retains the Core's bounded inter-frame gap.
/// 执行任务优先级 / Execution task priority.
constexpr unsigned kDapPriority = 24;
static_assert(kDapPriority < configMAX_PRIORITIES,
              "DAP task priority must fit the FreeRTOS priority range");
/// libxr 定时器任务的栈 / Stack of the libxr timer task.
constexpr uint32_t kLibxrTimerStack = 4096;
#else
/// SWD 位带、执行任务（DAP 命令和采样）和网络服务所在的核。专用 GPIO 只能在创建它的核
/// 上访问；USB 中断、Wi-Fi、lwIP 在核 0，所以用核 1 / Core for SWD bit-banging, the
/// execution task (DAP commands and sampling) and the network server. Dedicated GPIO is
/// only accessible from its creating core; USB interrupts, Wi-Fi and lwIP live on core
/// 0, hence core 1.
constexpr int kSwdCore = 1;
/// 执行任务优先级，为 SWD 核上最高 / Execution task priority, the highest on the SWD
/// core.
constexpr unsigned kDapPriority = 14;
/// libxr 定时器任务的栈 / Stack of the libxr timer task.
constexpr uint32_t kLibxrTimerStack = 8192;
#endif

/// 串口 UART 号 / Serial UART number.
constexpr int kSerialUart = 0;
#if CONFIG_IDF_TARGET_ESP32S2
/// 排针 TXD（接 USB 转串口的 RX）/ Header TXD (connect to USB-UART RX).
constexpr int kSerialTxGpio = 42;
/// 排针 RXD（接 USB 转串口的 TX）/ Header RXD (connect to USB-UART TX).
constexpr int kSerialRxGpio = 41;
/// 排针串口速率 / Header UART baud rate.
constexpr uint32_t kSerialBaud = 115200;
#else
/// 串口 TX 引脚 / Serial TX pin.
constexpr int kSerialTxGpio = 43;
/// 串口 RX 引脚 / Serial RX pin.
constexpr int kSerialRxGpio = 44;
/// 串口波特率，取板上 USB 转串口芯片（CH343）能稳定工作的最高速率 / Serial baud rate,
/// the highest the on-board USB-UART chip (CH343) runs reliably.
constexpr uint32_t kSerialBaud = 4000000;
#endif
/// 串口 DAP 任务优先级 / Serial DAP task priority.
constexpr unsigned kSerialPriority = 12;
}  // namespace XrDap::Config
