// SPDX-License-Identifier: Apache-2.0
#pragma once

// 厂商扩展的采样任务：固定在 SWD 所在的核上，由 gptimer 每拍唤醒（XRDAP-Core 的
// Vendor::TickTimer 的 ESP 实现）。定时器在采样任务里创建，中断因此分配在同一个核上。
//
// Sampling task of the vendor extension: pinned to the core that drives SWD and woken on
// every tick by a gptimer (the ESP implementation of XRDAP-Core's Vendor::TickTimer). The
// timer is created in the sampling task, so its interrupt lands on the same core.

#include "xrdap/vendor/dap_vendor.hpp"

namespace XrDap
{

/// @brief 创建采样任务 / Create the sampling task.
/// @param core 必须是 SWD 所在的核 / Must be the core that drives SWD.
/// @param priority 应为该核上最高 / Should be the highest on that core.
/// @return 任务创建失败（内存不足）时返回 false / False when the task could not be
///         created (out of memory).
/// @note 在 app_main 里、DAP 命令开始处理之前调用一次。gptimer 建不起来时采样收不到节拍，
///       其余功能不受影响 / Call once from app_main before DAP commands are processed. If
///       the gptimer cannot be created, sampling gets no ticks; nothing else is affected.
bool StartSamplerTask(DapVendor& vendor, int core, unsigned priority);

}  // namespace XrDap
