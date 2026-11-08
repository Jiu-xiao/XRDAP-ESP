// SPDX-License-Identifier: Apache-2.0
#include "restart.hpp"

#include <cstdint>
#include <cstring>

#include "esp_attr.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace XrDap
{
namespace
{
constexpr uint64_t kRestartDelayUs = 500U * 1000U;

// RestartInit 在别的任务启动前写一次，之后只读 / Written once by RestartInit before other
// tasks start, read-only afterwards.
esp_timer_handle_t timer = nullptr;

// 任务看门狗超时时各核正在运行的任务。没有日志输出口（控制台关闭），所以在超时中断里
// 记进复位不清零的 RTC 内存，重启后由 wifi status 显示 / Task running on each core when
// the task watchdog times out. There is no log output (the console is off), so the
// timeout interrupt records it in RTC memory that survives the reset, and wifi status
// shows it after the reboot.
struct WatchdogRecord
{
  uint32_t magic;
  char task[2][16];
};
constexpr uint32_t kWatchdogMagic = 0x57445431U;
RTC_NOINIT_ATTR WatchdogRecord rtc_record;
// RestartInit 在别的任务启动前写一次，之后只读 / Written once by RestartInit before other
// tasks start, read-only afterwards.
WatchdogRecord last_record = {};
}  // namespace

}  // namespace XrDap

// IDF 在任务看门狗超时中断里、触发 panic 之前调用 / Called by IDF in the task watchdog
// timeout interrupt, before it triggers the panic.
extern "C" void IRAM_ATTR esp_task_wdt_isr_user_handler(void)
{
  using XrDap::rtc_record;
  for (int core = 0; core < 2; ++core)
  {
    const char* name = nullptr;
    if (core < portNUM_PROCESSORS)
    {
      name = pcTaskGetName(xTaskGetCurrentTaskHandleForCore(core));
    }
    size_t i = 0;
    for (; name != nullptr && name[i] != 0 && i + 1U < sizeof(rtc_record.task[core]); ++i)
    {
      rtc_record.task[core][i] = name[i];
    }
    rtc_record.task[core][i] = 0;
  }
  rtc_record.magic = XrDap::kWatchdogMagic;
}

namespace XrDap
{

esp_err_t RestartInit()
{
  if (rtc_record.magic == kWatchdogMagic && esp_reset_reason() == ESP_RST_TASK_WDT)
  {
    last_record = rtc_record;
  }
  rtc_record.magic = 0U;
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

void LastWatchdogTasks(char (&out)[2][16])
{
  std::memset(out, 0, sizeof(out));
  if (last_record.magic == kWatchdogMagic)
  {
    std::memcpy(out, last_record.task, sizeof(out));
    out[0][sizeof(out[0]) - 1U] = 0;
    out[1][sizeof(out[1]) - 1U] = 0;
  }
}

ResetReason LastResetReason()
{
  switch (esp_reset_reason())
  {
    case ESP_RST_POWERON:
      return ResetReason::POWER_ON;
    case ESP_RST_SW:
      return ResetReason::SOFTWARE;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return ResetReason::WATCHDOG;
    case ESP_RST_PANIC:
      return ResetReason::PANIC;
    case ESP_RST_BROWNOUT:
      return ResetReason::BROWNOUT;
    case ESP_RST_UNKNOWN:
      return ResetReason::UNKNOWN;
    default:
      return ResetReason::OTHER;
  }
}

}  // namespace XrDap
