// SPDX-License-Identifier: Apache-2.0
#pragma once

// XRDAP-Core 的平台部分：采样帧缓冲的大小和它的自旋锁。ESP32 是双核，用 portMUX：关本核
// 中断并占住自旋锁，另一个核上的持有者放手前在这里自旋。
//
// Platform part of XRDAP-Core: the size of the sample frame buffer and its spinlock. The
// ESP32 parts are dual-core, so it is a portMUX: interrupts off on this core plus a
// spinlock the other core spins on until the holder lets go.

#include <cstdint>

#include "freertos/FreeRTOS.h"

namespace XrDap::Port
{

/// 采样帧缓冲字节数，2 的幂；ESP 的 RAM 够大，取 16 KB / Sample frame buffer size, a
/// power of two; ESP has enough RAM for 16 KB.
inline constexpr uint32_t kRingBytes = 16384U;

/// 短临界区，持有时间不超过拷贝 1 KB / Short critical section, held for at most a 1 KB
/// copy.
class SpinLock
{
 public:
  void Lock() { taskENTER_CRITICAL(&mux_); }
  void Unlock() { taskEXIT_CRITICAL(&mux_); }

 private:
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace XrDap::Port
