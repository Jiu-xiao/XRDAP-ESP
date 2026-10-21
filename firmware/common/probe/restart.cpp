// SPDX-License-Identifier: Apache-2.0
#include "restart.hpp"

#include <cstdint>

#include "esp_system.h"
#include "esp_timer.h"

namespace XrDap
{
namespace
{
constexpr uint64_t kRestartDelayUs = 500U * 1000U;

// RestartInit 在别的任务启动前写一次，之后只读 / Written once by RestartInit before other
// tasks start, read-only afterwards.
esp_timer_handle_t timer = nullptr;
}  // namespace

esp_err_t RestartInit()
{
  esp_timer_create_args_t args = {};
  args.callback = [](void*) { esp_restart(); };
  args.name = "restart";
  return esp_timer_create(&args, &timer);
}

void RestartLater()
{
  if (timer == nullptr)
  {
    esp_restart();
  }
  (void)esp_timer_stop(timer);
  (void)esp_timer_start_once(timer, kRestartDelayUs);
}

bool RestartPending() { return timer != nullptr && esp_timer_is_active(timer); }

}  // namespace XrDap
