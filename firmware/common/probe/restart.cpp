// SPDX-License-Identifier: Apache-2.0
#include "restart.hpp"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace
{
esp_timer_handle_t timer = nullptr;
}  // namespace

namespace XrDap
{

void RestartLater(uint32_t delay_ms)
{
  if (timer == nullptr)
  {
    esp_timer_create_args_t args = {};
    args.callback = [](void*) { esp_restart(); };
    args.name = "restart";
    if (esp_timer_create(&args, &timer) != ESP_OK)
    {
      ESP_LOGE("xrdap", "创建重启定时器失败，立即重启");
      esp_restart();
    }
  }
  (void)esp_timer_stop(timer);
  (void)esp_timer_start_once(timer, static_cast<uint64_t>(delay_ms) * 1000U);
}

bool RestartPending() { return timer != nullptr && esp_timer_is_active(timer); }

}  // namespace XrDap
