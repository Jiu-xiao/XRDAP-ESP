// SPDX-License-Identifier: Apache-2.0
#pragma once

// 各芯片共用的固定参数。各芯片自己的 config.hpp 包含本文件，再加上引脚、核和优先级。
//
// Fixed parameters shared by every chip. Each chip's config.hpp includes this file and
// adds its pins, cores and priorities.

#include <cstdint>

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
/// USB/IP 虚拟 USB 探针的端口 / Port of the USB/IP virtual USB probe.
constexpr uint16_t kUsbipPort = 3240;
/// TCP 和 USB/IP 服务任务的优先级 / Priority of the TCP and USB/IP server task.
constexpr unsigned kNetPriority = 10;
/// 开机时等 Wi-Fi 拿到 IP 的最长时间；超时后 Wi-Fi 在后台继续重连 / Longest wait for a
/// Wi-Fi IP at boot; after it Wi-Fi keeps reconnecting in the background.
constexpr uint32_t kWifiWaitMs = 20000;
/// Wi-Fi 命令行任务的优先级 / Priority of the Wi-Fi console task.
constexpr unsigned kConsolePriority = 4;

/// SWD 引脚驱动强度，0 最弱，3 最强（IDF 默认 2）。降一档以减小边沿过冲和振铃；0 档的
/// 边沿太慢，高时钟下出错 / SWD pad drive strength, 0 weakest to 3 strongest (IDF
/// default 2). One step lower reduces edge overshoot and ringing; level 0 has edges too
/// slow for high clocks.
constexpr int kPadDriveStrength = 1;
}  // namespace XrDap::Config
