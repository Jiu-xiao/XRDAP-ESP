// SPDX-License-Identifier: Apache-2.0
#pragma once

// XRDAP-Core 的平台部分：采样帧缓冲用的自旋锁。ESP32 是双核，用 portMUX：关本核中断并
// 占住自旋锁，另一个核上的持有者放手前在这里自旋。
//
// Platform part of XRDAP-Core: the spinlock of the sample frame buffer. The ESP32 parts
// are dual-core, so it is a portMUX: interrupts off on this core plus a spinlock the
// other core spins on until the holder lets go.

#include "freertos/FreeRTOS.h"

namespace XrDap::Port
{

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
