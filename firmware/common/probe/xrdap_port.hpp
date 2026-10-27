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
#include "sdkconfig.h"

namespace XrDap::Vendor
{
class TaskSampling;
}  // namespace XrDap::Vendor

namespace XrDap::Port
{
/// 采样在 RTOS 任务里 / Sampling runs in an RTOS task.
using SamplerPolicy = Vendor::TaskSampling;


/// 采样帧缓冲字节数，2 的幂。ESP32-S2 的内部 RAM 只有 320 KB，取 Core 默认的 4 KB；别的
/// ESP 芯片 RAM 够大，取 16 KB / Sample frame buffer size, a power of two. The ESP32-S2
/// has only 320 KB of internal RAM and keeps Core's default 4 KB; the other ESP chips
/// have enough RAM for 16 KB.
#if CONFIG_IDF_TARGET_ESP32S2
inline constexpr uint32_t kRingBytes = 4096U;
#else
inline constexpr uint32_t kRingBytes = 16384U;
#endif

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
