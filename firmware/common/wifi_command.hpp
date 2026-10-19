// SPDX-License-Identifier: Apache-2.0
#pragma once

// 厂商命令 0x86：经 DAP 本身（USB、串口、TCP 均可）查询、设置、清除 Wi-Fi 热点参数，
// 无需另开串口终端。格式见 docs/protocol.md 3.7 节。
//   0x86 0x00                         查询：回状态、标志、IP、信号、热点名（不回密码）
//   0x86 0x01 len ssid len password   保存热点参数，回复发出后约 0.5 秒重启
//   0x86 0x02                         清除热点参数，回复发出后约 0.5 秒重启
//
// Vendor command 0x86: query, set or clear the Wi-Fi credentials over DAP itself (USB,
// serial or TCP) without a separate serial terminal. See docs/protocol.md section 3.7.
//   0x86 0x00                         query: status, flags, IP, RSSI, SSID (no password)
//   0x86 0x01 len ssid len password   save credentials; restart ~0.5 s after the reply
//   0x86 0x02                         clear credentials; restart ~0.5 s after the reply

#include <cstdint>
#include <cstring>

#include "dap_vendor.hpp"
#include "esp_system.h"
#include "esp_timer.h"
#include "wifi_sta.hpp"

namespace XrDap
{

/// Wi-Fi 配置命令（0x86）/ Wi-Fi configuration command (0x86).
class WifiCommand
{
 public:
  /// 命令字节 / Command byte.
  static constexpr uint8_t kCmd = 0x86U;
  /// INFO 功能位 4：Wi-Fi 配置 / INFO feature bit 4: Wi-Fi configuration.
  static constexpr uint32_t kFeature = 1U << 4;

  /// @brief 把命令挂到厂商扩展 / Register the command with the vendor extension.
  /// @param vendor 厂商扩展，需在 DAP 命令开始处理前调用 / Vendor extension; call before
  ///        DAP commands are processed.
  static void Install(DapVendor& vendor)
  {
    vendor.SetExtraCommand(kCmd, kFeature, &WifiCommand::Handle, nullptr);
  }

 private:
  static constexpr uint8_t kOpStatus = 0x00U;
  static constexpr uint8_t kOpSet = 0x01U;
  static constexpr uint8_t kOpClear = 0x02U;
  static constexpr uint8_t kFlagStarted = 0x01U;
  static constexpr uint8_t kFlagConnected = 0x02U;
  static constexpr uint64_t kRestartDelayUs = 500000U;  // 留出时间让回复发出去

  static bool Handle(void*, const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                     uint16_t& out)
  {
    if (cap < 2U)
    {
      out = 0U;
      return true;
    }
    resp[0] = kCmd;
    if (len < 2U)
    {
      return Reply(resp, DapVendor::kStatusArg, out);
    }
    switch (req[1])
    {
      case kOpStatus:
        return (len == 2U) ? Status(resp, cap, out)
                           : Reply(resp, DapVendor::kStatusArg, out);
      case kOpSet:
        return Set(req, len, resp, out);
      case kOpClear:
        if (len != 2U)
        {
          return Reply(resp, DapVendor::kStatusArg, out);
        }
        if (WifiStaClear() != ESP_OK)
        {
          return Reply(resp, DapVendor::kStatusResource, out);
        }
        ScheduleRestart();
        return Reply(resp, DapVendor::kStatusOk, out);
      default:
        return Reply(resp, DapVendor::kStatusArg, out);
    }
  }

  static bool Reply(uint8_t* resp, uint8_t status, uint16_t& out)
  {
    resp[1] = status;
    out = 2U;
    return true;
  }

  static bool Status(uint8_t* resp, uint16_t cap, uint16_t& out)
  {
    const WifiStatus st = WifiStaGetStatus();
    const size_t ssid_len = strnlen(st.ssid, sizeof(st.ssid) - 1U);
    if (cap < 9U + ssid_len)
    {
      return Reply(resp, DapVendor::kStatusResource, out);
    }
    resp[1] = DapVendor::kStatusOk;
    resp[2] = static_cast<uint8_t>((st.started ? kFlagStarted : 0U) |
                                   (st.connected ? kFlagConnected : 0U));
    std::memcpy(&resp[3], st.ip, 4U);
    resp[7] = static_cast<uint8_t>(st.rssi);
    resp[8] = static_cast<uint8_t>(ssid_len);
    std::memcpy(&resp[9], st.ssid, ssid_len);
    out = static_cast<uint16_t>(9U + ssid_len);
    return true;
  }

  static bool Set(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t& out)
  {
    // 0x86 0x01 ssid_len ssid[ssid_len] pw_len pw[pw_len]
    if (len < 4U)
    {
      return Reply(resp, DapVendor::kStatusArg, out);
    }
    const uint8_t ssid_len = req[2];
    if (len < 3U + ssid_len + 1U)
    {
      return Reply(resp, DapVendor::kStatusArg, out);
    }
    const uint8_t pw_len = req[3U + ssid_len];
    if (len != 4U + ssid_len + pw_len || ssid_len > 32U || pw_len > 63U)
    {
      return Reply(resp, DapVendor::kStatusArg, out);
    }
    char ssid[33] = {};
    char password[64] = {};
    std::memcpy(ssid, &req[3], ssid_len);
    std::memcpy(password, &req[4U + ssid_len], pw_len);
    const esp_err_t err = WifiStaSave(ssid, password);
    std::memset(password, 0, sizeof(password));
    if (err == ESP_ERR_INVALID_ARG)
    {
      return Reply(resp, DapVendor::kStatusArg, out);
    }
    if (err != ESP_OK)
    {
      return Reply(resp, DapVendor::kStatusResource, out);
    }
    ScheduleRestart();
    return Reply(resp, DapVendor::kStatusOk, out);
  }

  // 交给 esp_timer 延后重启，命令处理线程先把回复写完。
  static void ScheduleRestart()
  {
    static esp_timer_handle_t timer = nullptr;
    if (timer == nullptr)
    {
      esp_timer_create_args_t args = {};
      args.callback = [](void*) { esp_restart(); };
      args.name = "wifi_restart";
      if (esp_timer_create(&args, &timer) != ESP_OK)
      {
        timer = nullptr;
        return;
      }
    }
    (void)esp_timer_stop(timer);
    (void)esp_timer_start_once(timer, kRestartDelayUs);
  }
};

}  // namespace XrDap
