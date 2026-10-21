// SPDX-License-Identifier: Apache-2.0
#pragma once

// 状态灯刷新：一个低优先级任务每 20 ms 取一次输入（组合根给的 LinkInputs 和 DAP 活动），
// 按 link_state.hpp 换成连接和设置状态，再按 XRDAP-Core 的灯语（status_light.hpp）算出
// 一帧交给 StatusLed。按键和 Wi-Fi 设置不在这里。
//
// Status light refresh: a low-priority task takes the inputs every 20 ms (the
// LinkInputs from the composition root and the DAP activity), turns them into link and
// setting states with link_state.hpp, computes a frame with XRDAP-Core's light rule
// (status_light.hpp) and hands it to StatusLed. The button and the Wi-Fi settings are
// not handled here.

#include "link_state.hpp"
#include "status_led.hpp"
#include "xrdap/activity/dap_activity.hpp"

namespace XrDap
{

/// 状态灯刷新任务 / Status light refresh task.
class StatusPanel
{
 public:
  /// 取一次输入，在刷新任务里调用，须可在任意任务调用 / Take the inputs once; called in
  /// the refresh task, so it must be callable from any task.
  using InputsFn = LinkInputs (*)(void* ctx);

  /// @param led 状态灯 / Status light.
  /// @param dap DAP 活动来源 / DAP activity source.
  /// @param inputs 取输入的函数 / Function that takes the inputs.
  /// @param ctx 传给 inputs 的参数 / Argument passed to inputs.
  /// @note led、dap 的生命周期需长于本对象 / led and dap must outlive this object.
  StatusPanel(StatusLed& led, const DapActivity& dap, InputsFn inputs, void* ctx)
      : led_(led), dap_(dap), inputs_(inputs), ctx_(ctx)
  {
  }

  /// @brief 在指定的核上启动刷新任务 / Start the refresh task on a core.
  /// @return 任务创建失败（内存不足）时返回 false，灯不亮 / False when the task could not
  ///         be created (out of memory); the light stays dark.
  bool Start(int core);

 private:
  static void TaskEntry(void* arg);
  void Run();

  StatusLed& led_;
  const DapActivity& dap_;
  InputsFn inputs_;
  void* ctx_;
};

}  // namespace XrDap
