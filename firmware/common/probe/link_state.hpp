// SPDX-License-Identifier: Apache-2.0
#pragma once

// 状态灯的输入怎么变成 XRDAP-Core 灯语里的连接和设置状态，以及按键长按的判定。纯逻辑，
// 不依赖 ESP-IDF，电脑上可测。连接状态的优先级：
//   无线   有网络调试客户端（OpenOCD TCP 或 USB/IP）
//   有线   USB 已被主机配置，或 5 s 内有经 USB、串口来的 DAP 命令
//   无线   只连着 Wi-Fi
//   正在连 Wi-Fi 正在入网
// 设置状态：已安排重启（常亮）优先于按住按键（快闪）。
//
// How the status light inputs become the link and setting states of XRDAP-Core's light
// rule, and how a long button press is detected. Pure logic without ESP-IDF, so it is
// PC-testable. Link priority:
//   wireless  a network debug client (OpenOCD TCP or USB/IP)
//   wired     USB configured by the host, or a DAP command over USB or serial within 5 s
//   wireless  only Wi-Fi
//   joining   Wi-Fi associating
// Setting: a scheduled restart (solid) wins over a held button (fast blink).

#include <cstdint>

#include "status_light.hpp"
#include "wifi_settings.hpp"

namespace XrDap
{

/// 状态灯每次刷新时由组合根给出的输入 / Inputs the composition root hands to the status
/// light on every refresh.
struct LinkInputs
{
  /// 有网络调试客户端 / A network debug client is connected.
  bool net_client;
  /// USB 已被主机配置 / USB is configured by the host.
  bool usb_configured;
  /// Wi-Fi 连接阶段 / Wi-Fi link stage.
  WifiLink wifi;
  /// 已安排重启（刚改了设置）/ A restart is scheduled (a setting just changed).
  bool restart_pending;
  /// 按键正被按住 / The button is being held.
  bool button_held;
};

/// @brief 连接状态 / Link state.
/// @param dap_wired 5 s 内有经 USB、串口来的 DAP 命令（DapActivity::State::wired）/ A
///        DAP command arrived over USB or serial within 5 s (DapActivity::State::wired).
inline Light::Link ClassifyLink(const LinkInputs& in, bool dap_wired)
{
  if (in.net_client)
  {
    return Light::Link::WIRELESS;
  }
  if (dap_wired || in.usb_configured)
  {
    return Light::Link::WIRED;
  }
  if (in.wifi == WifiLink::UP)
  {
    return Light::Link::WIRELESS;
  }
  return in.wifi == WifiLink::JOINING ? Light::Link::JOINING : Light::Link::NONE;
}

/// @brief 设置状态 / Setting state.
inline Light::Setting ClassifySetting(const LinkInputs& in)
{
  if (in.restart_pending)
  {
    return Light::Setting::APPLIED;
  }
  return in.button_held ? Light::Setting::CHANGING : Light::Setting::NONE;
}

/// 按键长按判定：按满 hold_ms 时报告一次，松开后重新计时 / Long-press detector: reports
/// once when held for hold_ms, restarts after release.
/// @note 非线程安全，由轮询按键的任务独占 / Not thread-safe; owned by the task that polls
///       the button.
class HoldDetector
{
 public:
  /// @param hold_ms 需要按住的时间 / Time the button must be held.
  explicit HoldDetector(uint32_t hold_ms) : hold_ms_(hold_ms) {}

  /// @brief 喂一次采样 / Feed one sample.
  /// @param pressed 本次是否按下 / Whether the button is pressed now.
  /// @param elapsed_ms 距上次采样的时间 / Time since the previous sample.
  /// @return 本次刚好按满时返回 true，每次按下最多一次 / True on the sample that reaches
  ///         the hold time; at most once per press.
  bool Update(bool pressed, uint32_t elapsed_ms)
  {
    if (!pressed)
    {
      held_ms_ = 0U;
      fired_ = false;
      return false;
    }
    held_ms_ = (held_ms_ + elapsed_ms < held_ms_) ? UINT32_MAX : held_ms_ + elapsed_ms;
    if (fired_ || held_ms_ < hold_ms_)
    {
      return false;
    }
    fired_ = true;
    return true;
  }

  /// 按键是否正被按住 / Whether the button is being held.
  bool Held() const { return held_ms_ > 0U; }

 private:
  uint32_t hold_ms_;
  uint32_t held_ms_ = 0U;
  bool fired_ = false;
};

}  // namespace XrDap
