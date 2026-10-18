// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// XR-DAP-MSPM0 的固定参数（LP-MSPM0G3507）。

#ifndef XRDAP_SERIAL_BAUD
#define XRDAP_SERIAL_BAUD 1000000
#endif

namespace XrDap::Config
{
// 通告给上位机的 DAP 包长。串口一次只有一条请求在途（xrdap-forward 发一条等一条），
// 所以包数通告为 1。
constexpr uint16_t kDapPacketSize = 1024;
constexpr uint8_t kDapPacketCount = 1;
// DAP_QueueCommands 的暂存区。OpenOCD 不用这条命令，给一个包长就够。
constexpr uint16_t kDapQueuedRequestBuffer = kDapPacketSize;

// 串口 DAP 链路：UART0（PA10 TX、PA11 RX）接 XDS110 的反向通道串口。
// XDS110 这一侧没有官方的最高波特率，默认用 1 Mbaud；CMake 里可以用
// -DXRDAP_SERIAL_BAUD=... 改，xrdap-forward 的 --baud 要和它一致。
constexpr uint32_t kSerialBaud = XRDAP_SERIAL_BAUD;

// 一帧最多 16 字节头 + 2 字节长度 + DAP 包 + 1 字节尾 CRC。
constexpr size_t kFrameMax = 16U + 2U + kDapPacketSize + 1U;
// 发送双缓冲每半区要放得下一整帧，并按 8 字节取整（驱动要求半区按 sizeof(size_t) 对齐）。
constexpr size_t kUartTxHalf = (kFrameMax + 7U) & ~static_cast<size_t>(7U);
// 接收 DMA 环：DMA 半满、满和线路空闲时把数据搬进软件接收队列。
constexpr size_t kUartRxRing = 512U;
// 软件接收队列：放得下一整帧再留些余量。
constexpr size_t kUartRxQueue = 1536U;
constexpr uint32_t kUartTxQueueOps = 4U;

// SwdGeneralGPIO 的延时校准：BusyLoop 每微秒的循环次数。
// Cortex-M0+ 上一次循环约 3 个周期，80 MHz 下约 26 次。没有上板标定过，实际 SWCLK
// 会比 OpenOCD 设的频率低（每个沿本身的开销没算进去）；请求 10 MHz 以上时走全速路径。
constexpr uint32_t kSwdLoopsPerUs = 26U;

// DAP_Info 字符串前缀；序列号 = 前缀 + 芯片 TRACEID（十六进制）。
constexpr char kSerialPrefix[] = "XRobot-XRDAP-";
}  // namespace XrDap::Config
