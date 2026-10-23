// SPDX-License-Identifier: Apache-2.0
#include "status_led.hpp"

#include <cstdint>

namespace XrDap
{
namespace
{
/// 峰值亮度，0 到 255；灯离眼睛近，低一点就够 / Peak level, 0 to 255; the LED sits
/// close to the eye, so a low level is enough.
constexpr uint32_t kPeak = 96;

// WS2812B-2020 规格书 p.4：T0H 220–380 ns，T1H、T0L、T1L 580 ns–1 µs，每位 ≥ 1.25 µs，
// 复位 ≥ 280 µs。led_strip 自带的 WS2812 时序 0 码只有 1.2 µs，所以自己给。
// WS2812B-2020 datasheet p.4: T0H 220-380 ns, T1H/T0L/T1L 580 ns-1 us, each bit
// >= 1.25 us, reset >= 280 us. led_strip's built-in WS2812 0-bit is only 1.2 us.
constexpr led_strip_timings_t kTimings = {300, 750, 1000, 600, 300};
}  // namespace

StatusLed::StatusLed(int gpio)
{
  led_strip_config_t strip = {};
  strip.strip_gpio_num = gpio;
  strip.max_leds = 1;
  strip.led_model = LED_MODEL_CUSTOM;
  strip.timings = kTimings;
  strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  led_strip_rmt_config_t rmt = {};
  rmt.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt.resolution_hz = 10 * 1000 * 1000;
  if (led_strip_new_rmt_device(&strip, &rmt, &strip_) != ESP_OK)
  {
    strip_ = nullptr;
  }
}

void StatusLed::Show(const Light::Frame& f)
{
  if (strip_ == nullptr)
  {
    return;
  }
  const uint32_t level = kPeak * f.level / 1000U;
  led_strip_set_pixel(strip_, 0, f.color.r * level / 255U, f.color.g * level / 255U,
                      f.color.b * level / 255U);
  led_strip_refresh(strip_);
}

}  // namespace XrDap
