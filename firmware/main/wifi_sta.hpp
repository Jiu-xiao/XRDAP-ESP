#pragma once

#include "esp_err.h"

namespace XrDap
{
/// Starts Wi-Fi STA using the credentials already stored on this device by the RMLink
/// firmware (NVS namespace "rmlink", blob "wifi"). Blocks until an IP is obtained or
/// `timeout_ms` expires. Credentials are never printed.
esp_err_t WifiStaStart(uint32_t timeout_ms);
}  // namespace XrDap
