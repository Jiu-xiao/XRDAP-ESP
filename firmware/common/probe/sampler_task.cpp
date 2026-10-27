// SPDX-License-Identifier: Apache-2.0
#include "sampler_task.hpp"

#include "driver/gptimer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "probe_config.hpp"
#include "timer_deadline.hpp"

namespace XrDap
{
namespace
{

/// Free-running 1 MHz counter with absolute alarms; delayed callbacks retain the
/// elapsed periods instead of losing them through hardware auto-reload.
class GptimerTick final : public Vendor::TickTimer
{
 public:
  explicit GptimerTick(DapVendor& vendor) : vendor_(vendor) {}

  /// 在采样任务里调用一次 / Called once in the sampling task.
  void Create()
  {
    gptimer_config_t cfg = {};
    cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    cfg.direction = GPTIMER_COUNT_UP;
    cfg.resolution_hz = 1000000U;
    // C 语言中断能用的最高级别，核 1 上别的 1、2 级中断推迟不了节拍 / The highest level
    // C interrupt handlers can use, so other level 1 and 2 interrupts on the core cannot
    // delay the tick.
    cfg.intr_priority = 3;
    gptimer_event_callbacks_t cbs = {};
    cbs.on_alarm = &GptimerTick::OnAlarm;
    gptimer_handle_t t = nullptr;
    if (gptimer_new_timer(&cfg, &t) == ESP_OK &&
        gptimer_register_event_callbacks(t, &cbs, this) == ESP_OK &&
        gptimer_enable(t) == ESP_OK)
    {
      timer_ = t;
    }
  }

  void Start(uint32_t period_us) override
  {
    if (timer_ == nullptr)
    {
      return;
    }
    gptimer_alarm_config_t alarm = {};
    period_us_ = period_us;
    alarm.alarm_count = period_us;
    alarm.reload_count = 0U;
    gptimer_set_raw_count(timer_, 0U);
    gptimer_set_alarm_action(timer_, &alarm);
    gptimer_start(timer_);
  }

  void Stop() override
  {
    if (timer_ != nullptr)
    {
      gptimer_stop(timer_);
    }
  }

 private:
  static bool IRAM_ATTR OnAlarm(gptimer_handle_t timer,
                                const gptimer_alarm_event_data_t* event, void* ctx)
  {
    auto& self = *static_cast<GptimerTick*>(ctx);
    const auto deadline =
        AdvanceTimerDeadline(event->alarm_value, event->count_value, self.period_us_);
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = deadline.next_alarm;
    // Rearm before waking: PostFromCallback may yield from this ISR. GPTimer
    // retriggers an alarm that becomes overdue while it is being programmed.
    gptimer_set_alarm_action(timer, &alarm);
    self.vendor_.OnTickIsr(deadline.elapsed_ticks);
    return false;  // 唤醒时已在中断里切换任务 / the wake already yields from the ISR
  }

  DapVendor& vendor_;
  gptimer_handle_t timer_ = nullptr;
  uint32_t period_us_ = 0U;
};
}  // namespace

bool StartSamplerTask(DapVendor& vendor, int core, unsigned priority)
{
  static GptimerTick tick(vendor);
  return xTaskCreatePinnedToCore(
             +[](void* arg)
             {
               tick.Create();
               static_cast<DapVendor*>(arg)->RunSamplerTask(tick);
             },
             "dap_sampler", Config::kSamplerStack, &vendor, priority, nullptr,
             core) == pdPASS;
}

}  // namespace XrDap
