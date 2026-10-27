// SPDX-License-Identifier: Apache-2.0
#pragma once

// 各芯片共用的固定参数。各芯片自己的 config.hpp 包含本文件，再加上引脚、核和优先级。
//
// Fixed parameters shared by every chip. Each chip's config.hpp includes this file and
// adds its pins, cores and priorities.

#include <cstdint>

#include "sdkconfig.h"

namespace XrDap::Config
{
/// 通告给上位机的 DAP 包长，也是 TCP 和 USB/IP 一个包的上限 / DAP packet size advertised
/// to the host; also the per-packet limit for TCP and USB/IP.
constexpr uint16_t kDapPacketSize = 1024;
/// DAP_Info 报的包数，客户端最多同时有这么多个请求在途 / Packet count in DAP_Info; a
/// client keeps at most this many requests in flight.
constexpr uint8_t kDapPacketCount = 4;

/// OpenOCD TCP 后端的端口 / Port of the OpenOCD TCP backend.
constexpr uint16_t kTcpPort = 3241;
/// TCP 服务任务的优先级 / Priority of the TCP server task.
constexpr unsigned kNetPriority = 10;
/// 开机时等 Wi-Fi 拿到 IP 的最长时间；超时后 Wi-Fi 在后台继续重连 / Longest wait for a
/// Wi-Fi IP at boot; after it Wi-Fi keeps reconnecting in the background.
constexpr uint32_t kWifiWaitMs = 20000;
/// Wi-Fi 命令行任务的优先级 / Priority of the Wi-Fi console task.
constexpr unsigned kConsolePriority = 4;

#if CONFIG_IDF_TARGET_ESP32S2
// ESP32-S2 的内部 RAM 只有 320 KB，任务栈取小一档；暂定值，要在硬件上看各任务栈的最高
// 水位后再定 / The ESP32-S2 has only 320 KB of internal RAM, so task stacks are one step
// smaller; provisional until each task's stack high-water mark is checked on hardware.
/// TCP 和 USB/IP 服务任务的栈 / Stack of the TCP and USB/IP server task.
constexpr uint32_t kNetStack = 6144;
/// 采样任务的栈 / Stack of the sampling task.
constexpr uint32_t kSamplerStack = 4096;
/// 命令行任务的栈 / Stack of the console task.
// The supplied long run reached 656 bytes free before the extra diagnostic report.
constexpr uint32_t kConsoleStack = 4096;
#else
/// TCP 和 USB/IP 服务任务的栈 / Stack of the TCP and USB/IP server task.
constexpr uint32_t kNetStack = 12288;
/// 采样任务的栈 / Stack of the sampling task.
constexpr uint32_t kSamplerStack = 8192;
/// 命令行任务的栈 / Stack of the console task.
constexpr uint32_t kConsoleStack = 4096;
#endif

/// SWD 引脚驱动强度，0 最弱，3 最强（IDF 默认 2）。降一档以减小边沿过冲和振铃；0 档的
/// 边沿太慢，高时钟下出错 / SWD pad drive strength, 0 weakest to 3 strongest (IDF
/// default 2). One step lower reduces edge overshoot and ringing; level 0 has edges too
/// slow for high clocks.
constexpr int kPadDriveStrength = 1;
}  // namespace XrDap::Config
