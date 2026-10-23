// SPDX-License-Identifier: Apache-2.0
#pragma once

// 一颗 WS2812B 状态灯，按 XRDAP-Core 灯语给出的颜色和亮度显示（经 RMT）。
//
// One WS2812B status light that shows the colour and level given by XRDAP-Core's light
// rule (driven through RMT).

#include "led_strip.h"
#include "xrdap/light/status_light.hpp"

namespace XrDap
{

/// 一颗 WS2812B 状态灯 / One WS2812B status light.
class StatusLed
{
 public:
  /// @brief 配置 RMT 通道；失败时（RMT 通道用完）灯不亮，其余功能不受影响 / Set up the
  ///        RMT channel; on failure (no free RMT channel) the light stays dark and
  ///        nothing else is affected.
  /// @param gpio WS2812 数据脚 / WS2812 data pin.
  explicit StatusLed(int gpio);

  /// @brief 显示一帧 / Show one frame.
  /// @note 非线程安全，只由刷新任务调用 / Not thread-safe; called by the refresh task
  ///       only.
  void Show(const Light::Frame& frame);

 private:
  led_strip_handle_t strip_ = nullptr;
};

}  // namespace XrDap
