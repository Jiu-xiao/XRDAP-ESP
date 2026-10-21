// SPDX-License-Identifier: Apache-2.0
#pragma once

// DAP 活动和上位机状态：数每条 DAP 命令，记下 DAP_HostStatus 报告的“已连接”“运行中”，
// 给状态灯用。DAP_Disconnect 清除这两个状态；上位机异常退出没发 Disconnect 时，
// kHostQuietMs 内没有任何命令也视为清除。
//
// DAP activity and host state: counts every DAP command and keeps the "connected" and
// "running" flags reported by DAP_HostStatus, for status lights. DAP_Disconnect clears
// both; when a host dies without a Disconnect, kHostQuietMs without any command clears
// them as well.

#include <atomic>
#include <cstdint>

namespace XrDap::Vendor
{

/// DAP 活动统计，命令线程写、任意任务读 / DAP activity, written by the command thread and
/// read from any task.
class DapActivity
{
 public:
  /// 多久收不到命令就认为上位机已离开 / How long without commands before the host is
  /// taken as gone.
  static constexpr uint32_t kHostQuietMs = 5000U;

  /// 某一时刻的状态 / State at one moment.
  struct State
  {
    /// 累计命令数，只看变化 / Running command count; only changes matter.
    uint32_t commands;
    /// 上位机报告已连接 / Host reported connected.
    bool connected;
    /// 上位机报告目标运行中 / Host reported the target running.
    bool running;
  };

  /// 命令线程对每条 DAP 命令调用 / Called by the command thread for every DAP command.
  void Observe(const uint8_t* req, uint16_t len, uint32_t now_ms)
  {
    last_ms_.store(now_ms, std::memory_order_relaxed);
    commands_.fetch_add(1U, std::memory_order_relaxed);
    if (req[0] == kHostStatus && len >= 3U)
    {
      // DAP_HostStatus：类型 0 是“已连接”灯，1 是“运行中”灯；状态 1 亮、0 灭
      (req[1] == 0U ? connected_ : running_)
          .store(req[2] != 0U, std::memory_order_relaxed);
    }
    else if (req[0] == kDisconnect)
    {
      connected_.store(false, std::memory_order_relaxed);
      running_.store(false, std::memory_order_relaxed);
    }
  }

  /// 读当前状态，可在任意任务调用 / Read the current state; callable from any task.
  State Read(uint32_t now_ms) const
  {
    const bool quiet = now_ms - last_ms_.load(std::memory_order_relaxed) > kHostQuietMs;
    return State{commands_.load(std::memory_order_relaxed),
                 !quiet && connected_.load(std::memory_order_relaxed),
                 !quiet && running_.load(std::memory_order_relaxed)};
  }

 private:
  static constexpr uint8_t kHostStatus = 0x01U;
  static constexpr uint8_t kDisconnect = 0x03U;

  std::atomic<uint32_t> commands_{0U};
  std::atomic<uint32_t> last_ms_{0U};
  std::atomic<bool> connected_{false};
  std::atomic<bool> running_{false};
};

}  // namespace XrDap::Vendor
