// SPDX-License-Identifier: Apache-2.0
#pragma once

// 板上的状态灯（一颗 WS2812）和 Wi-Fi 清除按键，Lite 板用。一个低优先级任务每 50 ms
// 刷新灯色、读一次按键：
//   蓝色常亮   没有热点参数，只走 USB
//   黄色慢闪   有热点参数，正在连接
//   绿色常亮   Wi-Fi 已连上
//   青色常亮   有网络调试客户端（OpenOCD TCP 或 USB/IP）连着
//   红色快闪   按键按住中；按满 kWifiClearHoldMs 后红色常亮，清除热点参数并重启
//
// On-board status light (one WS2812) and Wi-Fi clear button, used by the Lite board. A
// low-priority task refreshes the colour and reads the button every 50 ms:
//   solid blue     no credentials, USB only
//   slow yellow    credentials saved, joining
//   solid green    Wi-Fi joined
//   solid cyan     a network debug client (OpenOCD TCP or USB/IP) is connected
//   fast red       button held; after kWifiClearHoldMs solid red, the credentials are
//                  cleared and the probe restarts

#include <atomic>
#include <cstdint>

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
  StatusPanel(int led_gpio, int button_gpio);

  /// 在 core 上启动刷新任务 / Start the refresh task on core.
  void Start(int core);

  /// 网络客户端连上或断开时调用，可在任意任务调用 / Call when a network client connects
  /// or leaves; callable from any task.
  void SetNetworkClient(bool connected) { client_.store(connected); }

 private:
  static void TaskEntry(void* arg);
  void Run();
  void Show(uint8_t r, uint8_t g, uint8_t b);

  int button_gpio_;
  led_strip_handle_t strip_ = nullptr;
  std::atomic<bool> client_{false};
};

}  // namespace XrDap
