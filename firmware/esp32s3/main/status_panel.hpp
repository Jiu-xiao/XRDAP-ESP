// SPDX-License-Identifier: Apache-2.0
#pragma once

// 板上的状态灯（一颗 WS2812B）和 Wi-Fi 清除按键，Lite 板用。一个低优先级任务每 20 ms
// 刷新灯色、读一次按键。上电时彩虹扫过一圈，之后：
//   底色       蓝：没有热点参数；黄色呼吸：正在连 Wi-Fi；绿：Wi-Fi 已连上；
//              青：有网络调试客户端（OpenOCD TCP 或 USB/IP）
//   白         上位机经 DAP_HostStatus 报告已连接；目标运行中时 1 s 一次呼吸
//   闪亮       每条 DAP 命令（任何传输）把灯提亮再衰减，命令不断时约 12 Hz 闪烁
//   红色快闪   按键按住中；按满 5 s 后红色常亮，清除热点参数并重启
//
// On-board status light (one WS2812B) and Wi-Fi clear button, used by the Lite board. A
// low-priority task refreshes the light and reads the button every 20 ms. A rainbow
// sweep runs at power-up, then:
//   base       blue: no credentials; breathing yellow: joining Wi-Fi; green: Wi-Fi
//              joined; cyan: a network debug client (OpenOCD TCP or USB/IP)
//   white      the host reported connected through DAP_HostStatus; one breath per
//              second while the target runs
//   flash      every DAP command (any transport) brightens the light, which then
//              decays; steady traffic flickers at about 12 Hz
//   fast red   button held; after 5 s solid red, the credentials are cleared and the
//              probe restarts

#include <atomic>
#include <cstdint>

#include "dap_activity.hpp"
#include "led_strip.h"

namespace XrDap
{

/// 状态灯和 Wi-Fi 清除按键 / Status light and Wi-Fi clear button.
class StatusPanel
{
 public:
  /// @param led_gpio WS2812 数据脚 / WS2812 data pin.
  /// @param button_gpio 按键脚，按下接地，用内部上拉 / Button pin, shorted to ground when
  ///        pressed; the internal pull-up is used.
  /// @param dap DAP 活动来源，生命周期需长于本对象 / DAP activity source; must outlive
  ///        this object.
  StatusPanel(int led_gpio, int button_gpio, const Vendor::DapActivity& dap);

  /// 在 core 上启动刷新任务 / Start the refresh task on core.
  void Start(int core);

  /// 网络客户端连上或断开时调用，可在任意任务调用 / Call when a network client connects
  /// or leaves; callable from any task.
  void SetNetworkClient(bool connected) { client_.store(connected); }

 private:
  static void TaskEntry(void* arg);
  void Run();

  int button_gpio_;
  const Vendor::DapActivity& dap_;
  led_strip_handle_t strip_ = nullptr;
  std::atomic<bool> client_{false};
};

}  // namespace XrDap
