// SPDX-License-Identifier: Apache-2.0
#include "sampler.hpp"

#include <cstring>

#include "esp_attr.h"
#include "esp_timer.h"

namespace XrDap::Vendor
{
namespace
{
constexpr uint32_t kTaskStack = 8192;

uint64_t NowUs() { return static_cast<uint64_t>(esp_timer_get_time()); }

void MarkFailed(uint8_t* marks, uint16_t i)
{
  marks[i >> 3] |= static_cast<uint8_t>(1U << (i & 7U));
}
}  // namespace

void Sampler::Begin(int core, uint32_t task_prio, KeepFn keep, void* ctx)
{
  keep_ = keep;
  keep_ctx_ = ctx;
  wake_ = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(&Sampler::TaskEntry, "dap_sampler", kTaskStack, this, task_prio,
                          &task_, core);
}

uint8_t Sampler::Configure(const SubItem* items, uint16_t count, uint32_t period_us,
                           bool stop_on_full, Estimate& est)
{
  uint32_t max_frame = 10U + (count + 7U) / 8U;
  for (uint16_t i = 0; i < count; ++i)
  {
    max_frame += items[i].item.size;
  }
  est.max_frame = static_cast<uint16_t>(max_frame);

  // 按当前 SWD 时钟实测：先标定单次访问耗时（INFO 会报告），再把全部条目试读一遍，得到
  // 所有条目都到期时一帧的耗时。
  uint32_t frame_us = 0U;
  {
    MemAp::Lock lock(mem_, kConfigLockMs);
    uint32_t access_us = 0U;
    if (!lock || !mem_.Calibrate(items[0].item.ap, access_us))
    {
      return kStatusNoTarget;
    }
    access_us_ = access_us;
  }
  if (!MeasureFrame(items, count, frame_us))
  {
    return kStatusNoTarget;
  }

  // 乘 1.6：采样时同一个核上的 DAP 线程还要处理 SUB_READ，会抢占采样任务，每帧实际耗时
  // 约为试读的 1.5 倍，周期再短就开始跳拍。
  est.frame_us = 10U + (frame_us * 8U) / 5U;
  if (max_frame + 2U > kRingBytes || est.frame_us > period_us)
  {
    return kStatusResource;
  }

  std::memcpy(items_, items, sizeof(SubItem) * count);
  count_ = count;
  period_us_ = period_us;
  stop_on_full_ = stop_on_full;
  ring_.Reset();
  configured_ = true;
  return kStatusOk;
}

// 按采样时的访问序列试读两遍，返回第二遍的耗时：第一遍在预热指令和数据缓存，偶尔慢很多，
// 会让估计偏大而误拒订阅。各 AP 的 CSW 先读好，不计入耗时。试读和采样访问同一批地址，
// 副作用相同。
bool Sampler::MeasureFrame(const SubItem* items, uint16_t count, uint32_t& us)
{
  MemAp::Lock lock(mem_, kConfigLockMs);
  if (!lock)
  {
    return false;
  }
  mem_.ResetCache();
  bool ok = true;
  for (uint16_t i = 0; ok && i < count; ++i)
  {
    ok = mem_.EnsureAp(items[i].item.ap) != nullptr;
  }
  for (int pass = 0; ok && pass < 2; ++pass)
  {
    MemAp::ApState ap{};
    const uint64_t t0 = NowUs();
    for (uint16_t i = 0; i < count; ++i)
    {
      const Item& it = items[i].item;
      if (ap.ap != it.ap)
      {
        ap = *mem_.EnsureAp(it.ap);
        ap.written = false;
      }
      uint8_t value[8];
      (void)mem_.ReadItem(ap, it, value);
    }
    us = static_cast<uint32_t>(NowUs() - t0);
  }
  return ok;
}

const Sampler::ApCsw* Sampler::FindCsw(uint8_t ap) const
{
  for (uint8_t i = 0; i < csw_count_; ++i)
  {
    if (csw_[i].ap == ap)
    {
      return &csw_[i];
    }
  }
  return nullptr;
}

bool Sampler::Start()
{
  // 开始时读一次各 AP 的 CSW，采样期间不再重读。
  {
    MemAp::Lock lock(mem_, kConfigLockMs);
    if (!lock)
    {
      return false;
    }
    bool ok = true;
    csw_count_ = 0U;
    for (uint16_t i = 0; i < count_; ++i)
    {
      const uint8_t ap = items_[i].item.ap;
      if (FindCsw(ap) != nullptr)
      {
        continue;
      }
      ApCsw& dst = csw_[csw_count_++];
      dst.ap = ap;
      dst.valid = mem_.ReadCsw(ap, dst.base_csw);
      ok = ok && dst.valid;
    }
    if (!ok)
    {
      return false;
    }
  }

  ring_.Reset();
  start_us_ = NowUs();
  gen_.fetch_add(1, std::memory_order_acq_rel);
  active_.store(true, std::memory_order_release);
  xSemaphoreGive(wake_);
  return true;
}

void Sampler::WaitTickDone()
{
  for (int i = 0; i < 50 && in_tick_.load(std::memory_order_acquire); ++i)
  {
    vTaskDelay(1);
  }
}

void Sampler::TaskEntry(void* arg) { static_cast<Sampler*>(arg)->TaskLoop(); }

void Sampler::TaskLoop()
{
  // 节拍定时器在采样任务里创建，它的中断就分配在采样所在的核上。
  CreateTickTimer();
  for (;;)
  {
    if (!active_.load(std::memory_order_acquire))
    {
      xSemaphoreTake(wake_, pdMS_TO_TICKS(100));
      continue;
    }
    Run(gen_.load(std::memory_order_acquire));
  }
}

bool IRAM_ATTR Sampler::OnTickAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t*,
                                    void* ctx)
{
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(static_cast<Sampler*>(ctx)->task_, &woken);
  return woken == pdTRUE;
}

void Sampler::CreateTickTimer()
{
  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = 1000000U;
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = &Sampler::OnTickAlarm;
  if (gptimer_new_timer(&cfg, &tick_timer_) != ESP_OK ||
      gptimer_register_event_callbacks(tick_timer_, &cbs, this) != ESP_OK ||
      gptimer_enable(tick_timer_) != ESP_OK)
  {
    tick_timer_ = nullptr;  // 退回到每 1 ms 醒一次再忙等
  }
}

// 让定时器从现在起在 start + k × period 时刻发中断。
void Sampler::StartTickTimer(uint64_t start, uint32_t period)
{
  if (tick_timer_ == nullptr)
  {
    return;
  }
  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = period;
  alarm.reload_count = 0U;
  alarm.flags.auto_reload_on_alarm = true;
  gptimer_set_alarm_action(tick_timer_, &alarm);
  gptimer_set_raw_count(tick_timer_, (NowUs() - start) % period);
  gptimer_start(tick_timer_);
}

void Sampler::StopTickTimer()
{
  if (tick_timer_ != nullptr)
  {
    gptimer_stop(tick_timer_);
  }
}

// 阻塞等节拍中断。被中断唤醒后离 deadline 还差不到 kSpinUs（定时器与 esp_timer 之间的
// 偏差）时忙等补上。没被唤醒过之前一律阻塞、不忙等：采样任务优先级最高，一拍做完离下一拍
// 很近时忙等会占满这个核，别的任务连停止命令都收不到。
void Sampler::SleepUntil(uint64_t deadline_us)
{
  // 上一拍做得慢时，中途经过的节拍边界留下了通知；先清掉，只认这次阻塞之后的唤醒。
  (void)ulTaskNotifyTake(pdTRUE, 0);
  bool woke = false;
  for (;;)
  {
    if (!active_.load(std::memory_order_relaxed))
    {
      return;
    }
    const uint64_t now = NowUs();
    if (now >= deadline_us)
    {
      return;
    }
    if (woke && deadline_us - now <= kSpinUs)
    {
      continue;
    }
    // 超时只是为了定期检查 active_；没有定时器时就是每 1 ms 醒一次
    woke =
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(tick_timer_ != nullptr ? 20U : 1U)) != 0U;
  }
}

// 读一拍里到期的条目，失败的在 marks 里置位。
void Sampler::SampleTick(const uint16_t* order, uint16_t due, uint8_t* values,
                         uint8_t* marks)
{
  MemAp::Lock lock(mem_, kTickLockMs);
  if (!lock)
  {
    for (uint16_t i = 0; i < due; ++i)
    {
      MarkFailed(marks, i);
    }
    return;
  }
  in_tick_.store(true, std::memory_order_release);
  MemAp::ApState ap{};
  for (uint16_t i = 0; i < due; ++i)
  {
    const Item& it = items_[order[i]].item;
    if (ap.ap != it.ap)
    {
      const ApCsw* csw = FindCsw(it.ap);
      ap.ap = it.ap;
      ap.base_csw = (csw != nullptr) ? csw->base_csw : 0U;
      ap.valid = (csw != nullptr) && csw->valid;
      ap.written = false;
    }
    if (mem_.ReadItem(ap, it, values) != kItemOk)
    {
      MarkFailed(marks, i);
    }
    values += it.size;
  }
  in_tick_.store(false, std::memory_order_release);
}

void Sampler::Run(uint32_t gen)
{
  uint64_t k = 0U;
  const uint64_t start = start_us_;
  const uint32_t period = period_us_;
  const uint16_t count = count_;
  uint8_t frame[kMaxFrameBytes];
  uint16_t order[kMaxItems];
  StartTickTimer(start, period);

  auto running = [&]
  {
    return active_.load(std::memory_order_acquire) &&
           gen_.load(std::memory_order_acquire) == gen;
  };
  while (running())
  {
    SleepUntil(start + k * period);
    if (!running())
    {
      break;
    }

    const uint64_t tick_us = NowUs();
    uint16_t due = 0U;
    uint16_t values_size = 0U;
    for (uint16_t i = 0; i < count; ++i)
    {
      if ((k + items_[i].phase) % items_[i].div == 0U)
      {
        order[due++] = i;
        values_size += items_[i].item.size;
      }
    }

    if (due != 0U)
    {
      // 帧：节拍序号 u32 + 时间戳 u48 + 状态位图 + 各到期条目的值。
      const uint16_t bitmap = static_cast<uint16_t>((due + 7U) / 8U);
      const uint16_t base = 10U + bitmap;
      Put32(frame, static_cast<uint32_t>(k));
      Put48(frame + 4, tick_us);
      std::memset(frame + 10, 0, bitmap + values_size);
      SampleTick(order, due, frame + base, frame + 10);
      ring_.Push(frame, static_cast<uint16_t>(base + values_size), stop_on_full_);
    }

    const uint64_t done = NowUs();
    ++k;
    uint64_t next_k = k;
    if (done > start + k * period)
    {
      // 来不及：跳到下一个还没过去的节拍
      next_k = (done - start) / period + 1U;
    }
    const uint64_t min_gap = (period / 8U > kMinGapUs) ? period / 8U : kMinGapUs;
    if (start + next_k * period - done < min_gap)
    {
      ++next_k;
    }
    if (next_k != k)
    {
      ring_.AddSkipped(static_cast<uint32_t>(next_k - k));
      k = next_k;
    }

    if (keep_ != nullptr && !keep_(keep_ctx_))
    {
      Stop();
      break;
    }
  }
  StopTickTimer();
}

}  // namespace XrDap::Vendor
