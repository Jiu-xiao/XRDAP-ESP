// SPDX-License-Identifier: Apache-2.0
#include "status_panel.hpp"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "restart.hpp"
#include "status_light.hpp"
#include "wifi_sta.hpp"

namespace
{
constexpr const char* TAG = "xrdap_panel";
/// 刷新周期 / Refresh period.
constexpr uint32_t kTickMs = 20;
/// 按住按键多久清除热点参数 / How long the button must be held to clear the credentials.
constexpr uint32_t kWifiClearHoldMs = 5000;
/// 峰值亮度，0 到 255；灯离眼睛近，低一点就够 / Peak level, 0 to 255; the LED sits
/// close to the eye, so a low level is enough.
constexpr uint32_t kPeak = 96;

// WS2812B-2020 规格书 p.4：T0H 220–380 ns，T1H、T0L、T1L 580 ns–1 µs，每位 ≥ 1.25 µs，
// 复位 ≥ 280 µs。led_strip 自带的 WS2812 时序 0 码只有 1.2 µs，所以自己给。
// WS2812B-2020 datasheet p.4: T0H 220-380 ns, T1H/T0L/T1L 580 ns-1 us, each bit
// >= 1.25 us, reset >= 280 us. led_strip's built-in WS2812 0-bit is only 1.2 us.
constexpr led_strip_timings_t kTimings = {300, 750, 1000, 600, 300};

uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// 按规则给的亮度显示一颗灯 / Show one LED at the level the rule gives.
void Show(led_strip_handle_t strip, const XrDap::Light::Frame& f)
{
  const uint32_t level = kPeak * f.level / 1000U;
  led_strip_set_pixel(strip, 0, f.color.r * level / 255U, f.color.g * level / 255U,
                      f.color.b * level / 255U);
  led_strip_refresh(strip);
}
}  // namespace

namespace XrDap
{

StatusPanel::StatusPanel(int led_gpio, int button_gpio, const Vendor::DapActivity& dap,
                         UsbConfiguredFn usb_configured, void* usb_ctx)
    : button_gpio_(button_gpio),
      dap_(dap),
      usb_configured_(usb_configured),
      usb_ctx_(usb_ctx)
{
  led_strip_config_t strip = {};
  strip.strip_gpio_num = led_gpio;
  strip.max_leds = 1;
  strip.led_model = LED_MODEL_CUSTOM;
  strip.timings = kTimings;
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

void StatusPanel::Run()
{
  using namespace Light;
  StatusLight rule(NowMs());
  uint32_t held_ms = 0;
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(kTickMs));
    const uint32_t now = NowMs();

    const bool pressed = gpio_get_level(static_cast<gpio_num_t>(button_gpio_)) == 0;
    held_ms = pressed ? held_ms + kTickMs : 0;
    if (held_ms >= kWifiClearHoldMs && !RestartPending())
    {
      ESP_LOGW(TAG, "按键按住 %u ms，清除热点参数并重启", (unsigned)kWifiClearHoldMs);
      (void)WifiStaSettings().Clear();
      RestartLater();
    }

    // 网络客户端在用就是无线；否则 USB 已配置或最近有串口命令是有线；只连着 Wi-Fi 是
    // 无线。
    const Vendor::DapActivity::State dap = dap_.Read(now);
    const WifiLink wifi = WifiStaGetLink();
    Link link = Link::NONE;
    if (client_.load())
    {
      link = Link::WIRELESS;
    }
    else if (dap.wired || usb_configured_(usb_ctx_))
    {
      link = Link::WIRED;
    }
    else if (wifi == WifiLink::UP)
    {
      link = Link::WIRELESS;
    }
    else if (wifi == WifiLink::JOINING)
    {
      link = Link::JOINING;
    }
    const Setting setting = RestartPending() ? Setting::APPLIED
                            : pressed        ? Setting::CHANGING
                                             : Setting::NONE;
    Show(strip_, rule.Update(link, setting, dap, now));
  }
}

}  // namespace XrDap
