// SPDX-License-Identifier: Apache-2.0
#pragma once

// 板上按键（按下接地，用内部上拉），长按时调用一次回调。用于 Lite 板和 S2 板的
// GPIO21：默认 开发板的 BOOT 键（GPIO0）接着 USB 转串口芯片的自动下载电路，串口线的
// DTR/RTS 会把它 拉低，不能当运行时按键。
//
// On-board button (shorts to ground, internal pull-up) that calls a callback once on a
// long press. Used for GPIO21 on the Lite and S2 boards: the default dev board's BOOT key
// (GPIO0) sits on the USB-UART auto-download circuit, where the serial DTR/RTS lines can
// pull it low, so it cannot serve as a runtime button.

#include <atomic>
#include <cstdint>

#include "link_state.hpp"

namespace XrDap
{

/// 长按按键 / Long-press button.
class HoldButton
{
 public:
  /// 按满时调用，在按键任务里执行 / Called when the hold time is reached, in the button
  /// task.
  using HoldFn = void (*)(void* ctx);

  /// @param gpio 按键脚 / Button pin.
  /// @param hold_ms 需要按住的时间 / Time the button must be held.
  /// @param on_hold 按满时调用 / Called when the hold time is reached.
  /// @param ctx 传给 on_hold 的参数 / Argument passed to on_hold.
  HoldButton(int gpio, uint32_t hold_ms, HoldFn on_hold, void* ctx);

  /// @brief 在指定的核上启动每 20 ms 读一次按键的低优先级任务 / Start the low-priority
  ///        task that reads the button every 20 ms on a core.
  /// @return 任务创建失败（内存不足）时返回 false / False when the task could not be
  ///         created (out of memory).
  bool Start(int core);

  /// 按键是否正被按住，可在任意任务调用 / Whether the button is being held; callable from
  /// any task.
  bool Held() const { return held_.load(std::memory_order_relaxed); }

 private:
  static void TaskEntry(void* arg);
  void Run();

  int gpio_;
  HoldDetector detector_;
  HoldFn on_hold_;
  void* ctx_;
  std::atomic<bool> held_{false};
};

}  // namespace XrDap
