// SPDX-License-Identifier: Apache-2.0
#include "status_panel.hpp"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "status_light.hpp"

namespace XrDap
{
namespace
{
/// 刷新周期 / Refresh period.
constexpr uint32_t kTickMs = 20;

uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
}  // namespace

bool StatusPanel::Start(int core)
{
  return xTaskCreatePinnedToCore(&StatusPanel::TaskEntry, "panel", 3072, this, 2, nullptr,
                                 core) == pdPASS;
}

void StatusPanel::TaskEntry(void* arg) { static_cast<StatusPanel*>(arg)->Run(); }

void StatusPanel::Run()
{
  Light::StatusLight rule(NowMs());
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(kTickMs));
    const uint32_t now = NowMs();
    const LinkInputs in = inputs_(ctx_);
    const Vendor::DapActivity::State dap = dap_.Read(now);
    led_.Show(rule.Update(ClassifyLink(in, dap.wired), ClassifySetting(in), dap, now));
  }
}

}  // namespace XrDap
