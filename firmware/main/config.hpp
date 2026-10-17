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
constexpr uint16_t kUsbPid = 0x4A01;  // VID 使用乐鑫的 0x303A

// 引脚驱动强度，0 最弱，3 最强（IDF 默认 2）。默认强度在杜邦线上会振铃，导致 SWD 应答出错，所以降一档。
constexpr int kPadDriveStrength = 1;
}  // namespace XrDap::Config
