// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "wifi_settings.hpp"

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

/// @brief 命令行和 0x86 用的 Wi-Fi 设置，热点参数存在 NVS / Wi-Fi settings for the
///        console and 0x86; the credentials live in NVS.
/// @note 返回的对象一直有效，方法可在任意任务调用 / The object stays valid; its methods
///       are callable from any task.
WifiSettings& WifiStaSettings();

/// 读取连接阶段；不读 NVS，适合周期性查询，可在任意任务调用 / Read the link stage; does
/// not touch NVS, so it is cheap to poll; callable from any task.
WifiLink WifiStaGetLink();
}  // namespace XrDap
