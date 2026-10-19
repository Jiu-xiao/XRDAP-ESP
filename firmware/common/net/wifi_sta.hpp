// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "esp_err.h"

// Wi-Fi STA 连接，以及热点参数在 NVS 中的存取。
//
// Wi-Fi STA connection and NVS storage of the access-point credentials.

namespace XrDap
{
/// @brief 以 STA 模式连接 Wi-Fi，热点参数从 NVS 读取（命名空间 "rmlink"，键 "wifi"）/
///        Connect in STA mode using credentials from NVS (namespace "rmlink", key
///        "wifi").
/// @param timeout_ms 等待拿到 IP 的时间 / Time to wait for an IP.
/// @return ESP_OK：已连接 / connected.
/// @return ESP_ERR_TIMEOUT：协议栈已启动但未连上，后台继续重连 / Stack started but not
///         connected; reconnecting in the background.
/// @return 其他 / Other：无可用热点参数，Wi-Fi 未启动 / No usable credentials, Wi-Fi not
///         started.
/// @note 只调用一次，阻塞调用任务至多 timeout_ms；SSID 和密码不会打印 / Call once;
///       blocks the calling task for up to timeout_ms. SSID and password are never
///       logged.
esp_err_t WifiStaStart(uint32_t timeout_ms);

/// @brief 保存热点参数到 NVS，重启后生效 / Save the credentials to NVS; they take effect
///        after a restart.
/// @param ssid 热点名，1 到 32 字节 / SSID, 1 to 32 bytes.
/// @param password 密码，8 到 63 字节，空串表示开放网络 / Password, 8 to 63 bytes; empty
///        means an open network.
/// @return ESP_ERR_INVALID_ARG 长度不合法 / invalid length；其他非 ESP_OK 为 NVS 错误 /
///         other non-OK values are NVS errors.
/// @note 存储格式固定，升级固件不丢热点参数 / The storage format is fixed, so firmware
///       upgrades keep the credentials.
esp_err_t WifiStaSave(const char* ssid, const char* password);

/// @brief 清除已保存的热点参数；本来就没有也返回 ESP_OK / Erase the saved credentials;
///        returns ESP_OK even if none exist.
esp_err_t WifiStaClear();

/// Wi-Fi 状态快照 / Wi-Fi status snapshot.
struct WifiStatus
{
  /// Wi-Fi 是否已启动（有热点参数才会启动） / Wi-Fi started (only with saved
  /// credentials).
  bool started;
  /// 是否已拿到 IP / An IP has been obtained.
  bool connected;
  /// 已保存的热点名，没有则为空串 / Saved SSID; empty if none.
  char ssid[33];
  /// 已拿到的 IPv4 地址 / Obtained IPv4 address.
  uint8_t ip[4];
  /// 信号强度，未连接时为 0 / Signal strength; 0 when not connected.
  int8_t rssi;
};

/// 读取当前 Wi-Fi 状态，可在任意任务调用 / Read the current Wi-Fi status; callable from
/// any task.
WifiStatus WifiStaGetStatus();
}  // namespace XrDap
