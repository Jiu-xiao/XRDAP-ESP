// SPDX-License-Identifier: Apache-2.0
#include "status_panel.hpp"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "restart.hpp"
#include "wifi_sta.hpp"

namespace
{
constexpr const char* TAG = "xrdap_panel";
/// 刷新周期 / Refresh period.
constexpr uint32_t kTickMs = 20;
/// 按住按键多久清除热点参数 / How long the button must be held to clear the credentials.
constexpr uint32_t kWifiClearHoldMs = 5000;
/// 清除后多久重启，让红色常亮能被看到 / Delay before the restart, so solid red is seen.
constexpr uint32_t kRestartDelayMs = 500;
/// 上电时彩虹扫过一圈的时间 / Duration of the rainbow sweep at power-up.
constexpr uint32_t kBootSweepMs = 800;
/// 底色亮度与活动闪光的峰值，0 到 255；灯离眼睛近，低一点就够 / Base level and activity
/// peak, 0 to 255; the LED sits close to the eye, so low levels are enough.
constexpr uint32_t kBase = 20;
constexpr uint32_t kPeak = 96;
/// 活动闪光每拍衰减到原来的 7/10，约 100 ms 降到两成；降到 3/10 以下才接受下一次触发，
/// 命令连续不断时约 12 Hz 闪烁 / The activity flash decays to 7/10 per tick (about a
/// fifth after 100 ms) and retriggers only below 3/10, so steady traffic flickers at
/// about 12 Hz.
constexpr uint32_t kFlashRetrigger = 300;

// WS2812B-2020 规格书 p.4：T0H 220–380 ns，T1H、T0L、T1L 580 ns–1 µs，每位 ≥ 1.25 µs，
// 复位 ≥ 280 µs。led_strip 自带的 WS2812 时序 0 码只有 1.2 µs，所以自己给。
// WS2812B-2020 datasheet p.4: T0H 220-380 ns, T1H/T0L/T1L 580 ns-1 us, each bit
// >= 1.25 us, reset >= 280 us. led_strip's built-in WS2812 0-bit is only 1.2 us.
constexpr led_strip_timings_t kTimings = {300, 750, 1000, 600, 300};

struct Rgb
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
};
constexpr Rgb kBlue{0, 0, 255};
constexpr Rgb kYellow{255, 150, 0};
constexpr Rgb kGreen{0, 255, 0};
constexpr Rgb kCyan{0, 255, 255};
constexpr Rgb kWhite{255, 255, 255};
constexpr Rgb kRed{255, 0, 0};

uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// 三角波“呼吸”，返回千分比，在 low 与 1000 之间往返一次用 period_ms /
// Triangle "breathing" in permille, low to 1000 and back once per period_ms.
uint32_t Breath(uint32_t ms, uint32_t period_ms, uint32_t low)
{
  const uint32_t phase = ms % period_ms;
  const uint32_t half = period_ms / 2U;
  const uint32_t up = (phase < half) ? phase : period_ms - phase;
  return low + (1000U - low) * up / half;
}

// 一颗灯按 level（0–255）缩放后显示 / Show one LED scaled to level (0-255).
void Show(led_strip_handle_t strip, Rgb color, uint32_t level)
{
  led_strip_set_pixel(strip, 0, color.r * level / 255U, color.g * level / 255U,
                      color.b * level / 255U);
  led_strip_refresh(strip);
}

// 色相 0–1535 的饱和色 / Saturated colour for hue 0-1535.
Rgb Hue(uint32_t h)
{
  const auto x = static_cast<uint8_t>(h & 0xFFU);
  switch ((h >> 8) % 6U)
  {
    case 0:
      return {255, x, 0};
    case 1:
      return {static_cast<uint8_t>(255U - x), 255, 0};
    case 2:
      return {0, 255, x};
    case 3:
      return {0, static_cast<uint8_t>(255U - x), 255};
    case 4:
      return {x, 0, 255};
    default:
      return {255, 0, static_cast<uint8_t>(255U - x)};
  }
}
}  // namespace

namespace XrDap
{

StatusPanel::StatusPanel(int led_gpio, int button_gpio, const Vendor::DapActivity& dap)
    : button_gpio_(button_gpio), dap_(dap)
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
  const uint32_t start = NowMs();
  uint32_t held_ms = 0;
  uint32_t flash = 0;  // 活动闪光，千分比 / Activity flash in permille.
  uint32_t seen = dap_.Read(start).commands;
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(kTickMs));
    const uint32_t now = NowMs();
    const uint32_t t = now - start;

    const bool pressed = gpio_get_level(static_cast<gpio_num_t>(button_gpio_)) == 0;
    held_ms = pressed ? held_ms + kTickMs : 0;
    if (held_ms >= kWifiClearHoldMs)
    {
      Show(strip_, kRed, kPeak);
      ESP_LOGW(TAG, "按键按住 %u ms，清除热点参数并重启", (unsigned)kWifiClearHoldMs);
      (void)WifiStaClear();
      RestartLater(kRestartDelayMs);
      vTaskDelete(nullptr);
    }
    if (pressed)
    {
      Show(strip_, kRed, (t / 100U) % 2U == 0U ? kPeak : 0U);  // 100 ms 亮、100 ms 灭
      continue;
    }
    if (t < kBootSweepMs)
    {
      Show(strip_, Hue(t * 1536U / kBootSweepMs), kPeak);
      continue;
    }

    const Vendor::DapActivity::State dap = dap_.Read(now);
    flash = flash * 7U / 10U;
    if (dap.commands != seen && flash < kFlashRetrigger)
    {
      seen = dap.commands;
      flash = 1000U;
    }

    Rgb color = kWhite;
    uint32_t permille = 1000U;
    if (dap.connected)
    {
      // 上位机报告已连接：白色；目标运行中时 1 s 一次呼吸
      permille = dap.running ? Breath(t, 1000U, 250U) : 1000U;
    }
    else
    {
      switch (WifiStaGetLink())
      {
        case WifiLink::OFF:
          color = kBlue;
          break;
        case WifiLink::JOINING:
          color = kYellow;
          permille = Breath(t, 2000U, 100U);
          break;
        case WifiLink::UP:
          color = client_.load() ? kCyan : kGreen;
          break;
      }
    }
    Show(strip_, color, kBase * permille / 1000U + (kPeak - kBase) * flash / 1000U);
  }
}

}  // namespace XrDap
