// SPDX-License-Identifier: Apache-2.0
#include "status_panel.hpp"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "restart.hpp"
#include "wifi_sta.hpp"

namespace
{
constexpr const char* TAG = "xrdap_panel";
constexpr uint32_t kTickMs = 50;
/// 按住按键多久清除热点参数 / How long the button must be held to clear the credentials.
constexpr uint32_t kWifiClearHoldMs = 5000;
/// 清除后多久重启，让红色常亮能被看到 / Delay before the restart, so solid red is seen.
constexpr uint32_t kRestartDelayMs = 500;
/// 灯的亮度，0 到 255；板上的灯离眼睛近，低一点就够 / Light level, 0 to 255; the LED sits
/// close to the eye, so a low level is enough.
constexpr uint8_t kLevel = 24;
}  // namespace

namespace XrDap
{

StatusPanel::StatusPanel(int led_gpio, int button_gpio) : button_gpio_(button_gpio)
{
  led_strip_config_t strip = {};
  strip.strip_gpio_num = led_gpio;
  strip.max_leds = 1;
  strip.led_model = LED_MODEL_WS2812;
  strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  led_strip_rmt_config_t rmt = {};
  rmt.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt.resolution_hz = 10 * 1000 * 1000;
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip, &rmt, &strip_));

  const auto btn = static_cast<gpio_num_t>(button_gpio);
  gpio_reset_pin(btn);
  gpio_set_direction(btn, GPIO_MODE_INPUT);
  gpio_set_pull_mode(btn, GPIO_PULLUP_ONLY);
}

void StatusPanel::Start(int core)
{
  xTaskCreatePinnedToCore(&StatusPanel::TaskEntry, "panel", 3072, this, 2, nullptr, core);
}

void StatusPanel::TaskEntry(void* arg) { static_cast<StatusPanel*>(arg)->Run(); }

void StatusPanel::Show(uint8_t r, uint8_t g, uint8_t b)
{
  led_strip_set_pixel(strip_, 0, r, g, b);
  led_strip_refresh(strip_);
}

void StatusPanel::Run()
{
  uint32_t held_ms = 0;
  for (uint32_t tick = 0;; ++tick)
  {
    vTaskDelay(pdMS_TO_TICKS(kTickMs));
    const bool pressed = gpio_get_level(static_cast<gpio_num_t>(button_gpio_)) == 0;
    held_ms = pressed ? held_ms + kTickMs : 0;
    if (held_ms >= kWifiClearHoldMs)
    {
      Show(kLevel, 0, 0);
      ESP_LOGW(TAG, "按键按住 %u ms，清除热点参数并重启", (unsigned)kWifiClearHoldMs);
      (void)WifiStaClear();
      RestartLater(kRestartDelayMs);
      vTaskDelete(nullptr);
    }

    if (pressed)
    {
      const bool on = (tick / 2U) % 2U == 0U;  // 100 ms 亮、100 ms 灭
      Show(on ? kLevel : 0, 0, 0);
      continue;
    }
    switch (WifiStaGetLink())
    {
      case WifiLink::OFF:
        Show(0, 0, kLevel);
        break;
      case WifiLink::JOINING:
      {
        const bool on = (tick / 10U) % 2U == 0U;  // 500 ms 亮、500 ms 灭
        Show(on ? kLevel : 0, on ? kLevel : 0, 0);
        break;
      }
      case WifiLink::UP:
        Show(0, kLevel, client_.load() ? kLevel : 0);
        break;
    }
  }
}

}  // namespace XrDap
