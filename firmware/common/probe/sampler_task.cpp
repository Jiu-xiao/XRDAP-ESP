// SPDX-License-Identifier: Apache-2.0
#include "sampler_task.hpp"

#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace XrDap
{
namespace
{
constexpr uint32_t kTaskStack = 8192;

/// gptimer 按 1 MHz 计数，每个周期报警一次并自动重装 / A gptimer counting at 1 MHz that
/// alarms and reloads once per period.
class GptimerTick final : public Vendor::TickTimer
{
 public:
  explicit GptimerTick(DapVendor& vendor) : vendor_(vendor) {}

  bool Start(uint64_t start_us, uint32_t period_us) override
  {
    // 第一次在采样任务里创建，定时器中断就分配在采样所在的核上。
    if (timer_ == nullptr && !Create())
    {
      return false;
    }
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = period_us;
    alarm.reload_count = 0U;
    alarm.flags.auto_reload_on_alarm = true;
    gptimer_set_alarm_action(timer_, &alarm);
    const auto now = static_cast<uint64_t>(esp_timer_get_time());
    gptimer_set_raw_count(timer_, (now - start_us) % period_us);
    gptimer_start(timer_);
    return true;
  }

  void Stop() override { gptimer_stop(timer_); }

 private:
  static bool IRAM_ATTR OnAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t*,
                                void* ctx)
  {
    static_cast<GptimerTick*>(ctx)->vendor_.WakeSamplerFromIsr();
    return false;  // 唤醒时已在中断里切换任务 / the wake already yields from the ISR
  }

  bool Create()
  {
    gptimer_config_t cfg = {};
    cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    cfg.direction = GPTIMER_COUNT_UP;
    cfg.resolution_hz = 1000000U;
    gptimer_event_callbacks_t cbs = {};
    cbs.on_alarm = &GptimerTick::OnAlarm;
    gptimer_handle_t t = nullptr;
    if (gptimer_new_timer(&cfg, &t) != ESP_OK ||
        gptimer_register_event_callbacks(t, &cbs, this) != ESP_OK ||
        gptimer_enable(t) != ESP_OK)
    {
      ESP_LOGW("xrdap", "采样定时器创建失败，改为每 1 ms 查一次");
      return false;
    }
    timer_ = t;
    return true;
  }

  DapVendor& vendor_;
  gptimer_handle_t timer_ = nullptr;
};
}  // namespace

void StartSamplerTask(DapVendor& vendor, int core, unsigned priority)
{
  static GptimerTick tick(vendor);
  xTaskCreatePinnedToCore(
      +[](void* arg) { static_cast<DapVendor*>(arg)->RunSamplerTask(&tick); },
      "dap_sampler", kTaskStack, &vendor, priority, nullptr, core);
}

}  // namespace XrDap
