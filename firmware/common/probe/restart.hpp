// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace XrDap
{

/// @brief 过 delay_ms 后重启设备，立即返回；调用方趁这段时间把回复或提示发出去 /
///        Restart the device after delay_ms and return at once, so the caller can still
///        send its reply or message.
/// @note 再次调用会重新计时 / Calling again restarts the countdown.
void RestartLater(uint32_t delay_ms);

}  // namespace XrDap
