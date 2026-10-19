// SPDX-License-Identifier: Apache-2.0
#pragma once

// 探针端周期采样。第 k 拍的目标时刻为 start + k × period，不累计漂移；来不及时跳过节拍，
// 不补做。节拍由 gptimer 中断唤醒采样任务，任务在节拍之间阻塞，所以它可以是所在核上
// 优先级最高的任务：节拍准时，别的任务（DAP 命令、TCP、串口）在节拍之间运行。每个周期
// 至少给别的任务留 1/8 周期、最少 kMinGapUs，否则跳一拍。
//
// Probe-side periodic sampling. Tick k is due at start + k × period with no accumulated
// drift; a late tick is skipped, not made up. A gptimer interrupt wakes the sampling task
// on each tick and the task blocks in between, so it can be the highest-priority task on
// its core: ticks stay on time and other tasks (DAP commands, TCP, serial) run in the
// gaps. Each period leaves at least 1/8 of the period, and no less than kMinGapUs, to
// other tasks; otherwise one more tick is skipped.

#include <atomic>
#include <cstdint>

#include "driver/gptimer.h"
#include "frame_ring.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mem_ap.hpp"
#include "vendor_protocol.hpp"

namespace XrDap::Vendor
{

/// 采样配置、采样任务和帧缓冲 / Sampling configuration, sampling task and frame buffer.
class Sampler
{
 public:
  /// @brief 每拍末尾调用，返回 false 时采样停止 / Called at the end of every tick;
  ///        sampling stops when it returns false.
  using KeepFn = bool (*)(void* ctx);

  /// SUB_SET 的估计结果 / Estimate reported by SUB_SET.
  struct Estimate
  {
    /// 一帧的最大字节数 / Maximum bytes of one frame.
    uint16_t max_frame = 0;
    /// 估计的单帧耗时（微秒）/ Estimated frame time in microseconds.
    uint32_t frame_us = 0;
  };

  explicit Sampler(MemAp& mem) : mem_(mem) {}

  /// @brief 在指定的核上启动采样任务 / Start the sampling task on a core.
  /// @param core 必须是 SWD 所在的核 / Must be the core that owns the SWD pins.
  /// @param task_prio 应为该核上最高 / Should be the highest on that core.
  /// @param keep 每拍末尾的继续条件 / Keep-going check at the end of every tick.
  void Begin(int core, uint32_t task_prio, KeepFn keep, void* ctx);

  /// @brief 实测这组条目一帧的耗时并保存配置（SUB_SET）/ Measure the frame time of these
  ///        items and store the configuration (SUB_SET).
  /// @return kStatusOk；估计耗时超过周期或帧太大时 kStatusResource；SWD 访问失败时
  ///         kStatusNoTarget / kStatusOk; kStatusResource when the estimate exceeds the
  ///         period or the frame is too big; kStatusNoTarget when SWD access fails.
  uint8_t Configure(const SubItem* items, uint16_t count, uint32_t period_us,
                    bool stop_on_full, Estimate& est);
  /// 是否有有效配置 / Whether a valid configuration exists.
  bool Configured() const { return configured_; }
  /// 是否正在采样 / Whether sampling is running.
  bool Active() const { return active_.load(std::memory_order_acquire); }
  /// 最近一次标定的单次访问耗时（微秒），0 表示还没有标定 / Last calibrated per-access
  /// time in microseconds; 0 before any calibration.
  uint32_t AccessUs() const { return access_us_; }

  /// 读好各 AP 的 CSW 后开始采样；失败时返回 false / Read every AP's CSW, then start;
  /// false on failure.
  bool Start();
  /// 停止采样（不等手头这一拍）/ Stop sampling (does not wait for the current tick).
  void Stop() { active_.store(false, std::memory_order_release); }
  /// 等采样任务读完手头这一拍，最多 50 ms / Wait up to 50 ms for the current tick.
  void WaitTickDone();
  /// 清空帧缓冲和计数 / Clear the frame buffer and the counters.
  void ClearFrames() { ring_.Reset(); }
  /// 取出整帧（SUB_READ）/ Take whole frames (SUB_READ).
  FrameRing::Taken TakeFrames(uint8_t* dst, uint32_t limit)
  {
    return ring_.Take(dst, limit);
  }

 private:
  struct ApCsw
  {
    uint8_t ap;
    uint32_t base_csw;
    bool valid;
  };

  static constexpr uint32_t kTickLockMs = 1000U;
  static constexpr uint32_t kConfigLockMs = 2000U;
  // 被唤醒后离节拍不到这么久时忙等，补上中断和切换的延迟
  static constexpr uint32_t kSpinUs = 30U;
  // 每个周期至少留给别的任务的时间
  static constexpr uint32_t kMinGapUs = 15U;

  static void TaskEntry(void* arg);
  static bool OnTickAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t*, void* ctx);

  bool MeasureFrame(const SubItem* items, uint16_t count, uint32_t& us);
  const ApCsw* FindCsw(uint8_t ap) const;
  void TaskLoop();
  void CreateTickTimer();
  void StartTickTimer(uint64_t start, uint32_t period);
  void StopTickTimer();
  void SleepUntil(uint64_t deadline_us);
  void SampleTick(const uint16_t* order, uint16_t due, uint8_t* values, uint8_t* marks);
  void Run(uint32_t gen);

  MemAp& mem_;
  KeepFn keep_ = nullptr;
  void* keep_ctx_ = nullptr;

  std::atomic<bool> active_{false};
  std::atomic<bool> in_tick_{false};
  std::atomic<uint32_t> gen_{0};
  SemaphoreHandle_t wake_ = nullptr;
  TaskHandle_t task_ = nullptr;
  gptimer_handle_t tick_timer_ = nullptr;

  SubItem items_[kMaxItems];
  uint16_t count_ = 0U;
  uint32_t period_us_ = 0U;
  bool stop_on_full_ = false;
  bool configured_ = false;
  uint32_t access_us_ = 0U;
  ApCsw csw_[kMaxAps];
  uint8_t csw_count_ = 0U;
  uint64_t start_us_ = 0U;

  FrameRing ring_;
};

}  // namespace XrDap::Vendor
