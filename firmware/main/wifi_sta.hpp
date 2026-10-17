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
}  // namespace XrDap
