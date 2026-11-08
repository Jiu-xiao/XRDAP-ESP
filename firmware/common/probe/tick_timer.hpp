// SPDX-License-Identifier: Apache-2.0
#pragma once

// 采样的节拍定时器（XRDAP-Core 的 Vendor::TickTimer 的 ESP 实现）：1 MHz 自由计数的
// gptimer，用绝对值报警；中断晚到时按实际跨过的周期数计拍，不因硬件自动重载丢拍。中断里
// 调用 DapVendor::OnTickIsr()，由它叫醒执行任务。
//
// Sampling tick timer (the ESP implementation of XRDAP-Core's Vendor::TickTimer): a
// gptimer that counts freely at 1 MHz with absolute alarms; a late interrupt counts the
// periods that really elapsed instead of losing them to hardware auto-reload. The
// interrupt calls DapVendor::OnTickIsr(), which wakes the execution task.

#include <cstdint>

#include "driver/gptimer.h"
#include "xrdap/vendor/dap_vendor.hpp"

namespace XrDap
{

class GptimerTick final : public Vendor::TickTimer
{
 public:
  explicit GptimerTick(DapVendor& vendor) : vendor_(vendor) {}

  /// 在执行任务里调用一次，中断因此分配在这个任务所在的核上 / Called once in the
  /// execution task, so the interrupt is allocated on the core this task runs on.
  /// @note 建不起来时采样收不到节拍，其余功能不受影响 / If the timer cannot be created,
  ///       sampling gets no ticks; nothing else is affected.
  void Create();

  void Start(uint32_t period_us) override;
  void Stop() override;

 private:
  static bool OnAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* event,
                      void* ctx);

  DapVendor& vendor_;
  gptimer_handle_t timer_ = nullptr;
  uint32_t period_us_ = 0U;
};

}  // namespace XrDap
