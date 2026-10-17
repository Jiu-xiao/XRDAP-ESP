// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// 固件的固定参数。

namespace XrDap::Config
{
constexpr int kSwclkGpio = 5;  // SWCLK 接 GPIO5
constexpr int kSwdioGpio = 4;  // SWDIO 接 GPIO4

// SWD 位带、DAP 的 Step() 线程和 TCP 服务都放在这个核上。
// 专用 GPIO 只能在创建它的核上使用；USB 中断、Wi-Fi、lwIP 都在核 0，所以用核 1。
constexpr int kSwdCore = 1;
constexpr unsigned kStepPriority = 10;

constexpr uint16_t kDapPacketSize = 1024;  // 通告给上位机的 DAP 包长
constexpr uint16_t kTcpPort = 3241;

// USB 标识：使用 XRUSB 登记的 VID:PID（1D50:6199）。同一组 VID:PID 靠序列号区分设备，
// 序列号 = 前缀 + 芯片唯一 ID（十六进制），前缀规则见 xrobot.work 的「VID/PID 与 Serial 使用约定」。
constexpr uint16_t kUsbVid = 0x1D50;
constexpr uint16_t kUsbPid = 0x6199;
constexpr char kUsbSerialPrefix[] = "XRobot-XRDAP-";

// 引脚驱动强度，0 最弱，3 最强（IDF 默认 2）。降一档是为了减小边沿过冲和振铃；0 档在 10 MHz 以上错得更多。
constexpr int kPadDriveStrength = 1;
}  // namespace XrDap::Config
