// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "esp_err.h"

namespace XrDap
{
/// 以 STA 模式连接 Wi-Fi，热点参数从 NVS 读取（命名空间 "rmlink"，键 "wifi"）。
/// 等到拿到 IP 或超时才返回：
///   ESP_OK          已连接；
///   ESP_ERR_TIMEOUT 协议栈已启动但还没连上，会在后台继续重连；
///   其他            没有可用的热点参数，Wi-Fi 没有启动。
/// SSID 和密码不会打印。
esp_err_t WifiStaStart(uint32_t timeout_ms);

/// 保存热点参数。SSID 1 到 32 字节，密码 8 到 63 字节，或为空（开放网络）。
/// 格式与 RMLink 固件一致，用 RMLink 配过网的板子不用重配。
esp_err_t WifiStaSave(const char* ssid, const char* password);

/// 清除已保存的热点参数。
esp_err_t WifiStaClear();

struct WifiStatus
{
  bool started;     // Wi-Fi 是否已启动（有热点参数才会启动）
  bool connected;   // 是否已拿到 IP
  char ssid[33];    // 已保存的热点名，没有则为空串
  uint8_t ip[4];    // 已拿到的 IPv4 地址
  int8_t rssi;      // 信号强度，没连上时为 0
};

WifiStatus WifiStaGetStatus();
}  // namespace XrDap
