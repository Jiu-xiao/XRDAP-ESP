// SPDX-License-Identifier: Apache-2.0
#pragma once

// 改了设置（Wi-Fi 热点参数）之后的延时重启。定时器在启动时创建一次，之后任何任务都可以
// 安排重启。
//
// Delayed restart after a setting (the Wi-Fi credentials) changed. The timer is created
// once at start-up; after that any task may schedule the restart.

#include "esp_err.h"
#include "wifi_settings.hpp"

namespace XrDap
{

/// @brief 创建重启定时器；app_main 在启动别的任务之前调用一次 / Create the restart timer;
///        app_main calls this once before it starts other tasks.
/// @return ESP_OK，或 esp_timer_create 的错误（内存不足）/ ESP_OK, or the error of
///         esp_timer_create (out of memory).
esp_err_t RestartInit();

/// @brief 约 0.5 s 后重启设备，立即返回；调用方趁这段时间把回复或提示发出去，状态灯也
///        能显示出来 / Restart the device in about 0.5 s and return at once, so the
///        caller can still send its reply or message and the status light can show it.
/// @note 可在任意任务调用，不能在中断里调用；再次调用会重新计时。RestartInit 失败时立即
///       重启 / Callable from any task, not from an ISR; calling again restarts the
///       countdown. Restarts at once if RestartInit failed.
void RestartLater();

/// @brief 是否已经安排了重启 / Whether a restart is scheduled.
/// @note 可在任意任务调用 / Callable from any task.
bool RestartPending();

/// @brief 上次复位的原因（esp_reset_reason，读 RTC 寄存器）/ Cause of the last reset
///        (esp_reset_reason, read from the RTC registers).
/// @note 可在任意任务调用 / Callable from any task.
ResetReason LastResetReason();

}  // namespace XrDap
