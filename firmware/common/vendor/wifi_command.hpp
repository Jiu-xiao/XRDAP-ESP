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

#include "dap_vendor.hpp"

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

  /// @brief 把命令挂到厂商扩展，需在 DAP 命令开始处理前调用 / Register the command with
  ///        the vendor extension; call before DAP commands are processed.
  static void Install(DapVendor& vendor);

 private:
  static bool Handle(void*, const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                     uint16_t& out);
  static bool Status(uint8_t* resp, uint16_t cap, uint16_t& out);
  static bool Set(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t& out);
  static bool Clear(uint16_t len, uint8_t* resp, uint16_t& out);
};

}  // namespace XrDap
