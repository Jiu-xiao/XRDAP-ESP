// SPDX-License-Identifier: Apache-2.0
#include "hold_button.hpp"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace XrDap
{
namespace
{
constexpr uint32_t kPollMs = 20U;
}  // namespace

HoldButton::HoldButton(int gpio, uint32_t hold_ms, HoldFn on_hold, void* ctx)
    : gpio_(gpio), detector_(hold_ms), on_hold_(on_hold), ctx_(ctx)
{
  const auto pin = static_cast<gpio_num_t>(gpio);
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

bool HoldButton::Start(int core)
{
  return xTaskCreatePinnedToCore(&HoldButton::TaskEntry, "button", 2048, this, 2, nullptr,
                                 core) == pdPASS;
}

void HoldButton::TaskEntry(void* arg) { static_cast<HoldButton*>(arg)->Run(); }

void HoldButton::Run()
{
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
    const bool pressed = gpio_get_level(static_cast<gpio_num_t>(gpio_)) == 0;
    const bool fire = detector_.Update(pressed, kPollMs);
    held_.store(detector_.Held(), std::memory_order_relaxed);
    if (fire)
    {
      on_hold_(ctx_);
    }
  }
}

}  // namespace XrDap
