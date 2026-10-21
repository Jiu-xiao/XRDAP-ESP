// SPDX-License-Identifier: Apache-2.0
#pragma once

// 厂商扩展的采样任务：固定在 SWD 所在的核上，由 gptimer 每拍唤醒（XRDAP-Core 的
// Vendor::TickTimer 的 ESP 实现）。
//
// Sampling task of the vendor extension: pinned to the core that drives SWD and woken on
// every tick by a gptimer (the ESP implementation of XRDAP-Core's Vendor::TickTimer).

#include "dap_vendor.hpp"

namespace XrDap
{

/// @brief 创建采样任务 / Create the sampling task.
/// @param core 必须是 SWD 所在的核 / Must be the core that drives SWD.
/// @param priority 应为该核上最高 / Should be the highest on that core.
void StartSamplerTask(DapVendor& vendor, int core, unsigned priority);

}  // namespace XrDap
