// SPDX-License-Identifier: Apache-2.0
#pragma once

// 板上的状态灯（一颗 WS2812B）和 Wi-Fi 清除按键，引脚见 config.hpp。一个低优先级任务
// 每 20 ms 刷新灯色、读一次按键。上电时彩虹扫过一圈，之后主色表示连接：
//   红         有线：USB 已配置，或 5 s 内有经 USB、串口来的 DAP 命令
//   蓝         无线：有网络调试客户端（OpenOCD TCP 或 USB/IP），或只连着 Wi-Fi
//   紫         USB 和 Wi-Fi 都没连上；正在连 Wi-Fi 时呼吸
// 上位机经 DAP_HostStatus 报告已连接时主色与紫色来回（2 s 一次），目标运行中时红蓝来回
// （1 s 一次）；每条 DAP 命令把灯提亮再衰减，命令不断时约 12 Hz 闪烁。改配置时是紫色：
// 按住按键快闪，按满 5 s 或经命令行、0x86 改了热点参数后常亮，随后重启。
//
// On-board status light (one WS2812B) and Wi-Fi clear button; pins in config.hpp. A
// low-priority task refreshes the light and reads the button every 20 ms. A rainbow
// sweep runs at power-up; afterwards the main colour shows the connection:
//   red        wired: USB configured, or a DAP command over USB or serial within 5 s
//   blue       wireless: a network debug client (OpenOCD TCP or USB/IP), or only Wi-Fi
//   purple     neither USB nor Wi-Fi; breathing while joining Wi-Fi
// While the host reports connected through DAP_HostStatus the main colour fades to
// purple and back every 2 s, and while the target runs red and blue alternate every
// second; every DAP command brightens the light, which then fades (about 12 Hz under
// steady traffic). Changing a setting is purple: fast blinking while the button is held,
// solid after 5 s or after the credentials were changed from the console or 0x86, then
// the probe restarts.

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
  /// 查询 USB 是否已被主机配置 / Query whether USB is configured by the host.
  using UsbConfiguredFn = bool (*)(void* ctx);

  /// @param dap DAP 活动来源，生命周期需长于本对象 / DAP activity source; must outlive
  ///        this object.
  /// @param usb_configured 查询 USB 是否已配置，从刷新任务里调用 / Queries whether USB
  ///        is configured; called from the refresh task.
  StatusPanel(int led_gpio, int button_gpio, const Vendor::DapActivity& dap,
              UsbConfiguredFn usb_configured, void* usb_ctx);

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
  UsbConfiguredFn usb_configured_;
  void* usb_ctx_;
  led_strip_handle_t strip_ = nullptr;
  std::atomic<bool> client_{false};
};

}  // namespace XrDap
