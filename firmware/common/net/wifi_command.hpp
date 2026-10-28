// SPDX-License-Identifier: Apache-2.0
#pragma once

// 厂商命令 0x86：经 DAP 本身（USB、串口、TCP 均可）查询、设置、清除 Wi-Fi 热点参数，
// 无需另开串口终端。格式见 XRDAP-Core 的 docs/protocol.md 3.7
// 节。这里只解析命令、填回复， 设置经 WifiSettings 读写，不依赖 ESP-IDF。
//   0x86 0x00                         查询：回状态、标志、IP、信号、热点名（不回密码）
//   0x86 0x01 len ssid len password   保存热点参数，回复发出后约 0.5 秒重启
//   0x86 0x02 0xA5                    清除热点参数（0xA5 为确认字节），回复发出后约 0.5
//   秒
//                                     重启
//
// Vendor command 0x86: query, set or clear the Wi-Fi credentials over DAP itself (USB,
// serial or TCP) without a separate serial terminal. See section 3.7 of docs/protocol.md
// in XRDAP-Core. Only the parsing and the reply live here; the settings are reached
// through WifiSettings, without ESP-IDF.
//   0x86 0x00                         query: status, flags, IP, RSSI, SSID (no password)
//   0x86 0x01 len ssid len password   save credentials; restart ~0.5 s after the reply
//   0x86 0x02 0xA5                    clear credentials (0xA5 confirms); restart ~0.5 s
//                                     after the reply

#include <cstdint>

#include "wifi_settings.hpp"
#include "xrdap/control/control.hpp"

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

  /// @brief 处理一条 0x86 请求，签名与 XRDAP-Core DapVendor::ExtraHandler 相同 / Handle
  ///        one 0x86 request; the signature matches XRDAP-Core DapVendor::ExtraHandler.
  /// @param settings 指向 WifiSettings / Points to a WifiSettings.
  /// @param req 请求，req[0] 为 0x86 / Request; req[0] is 0x86.
  /// @param len 请求长度 / Request length.
  /// @param resp 回复缓冲 / Reply buffer.
  /// @param cap 回复缓冲容量 / Reply buffer capacity.
  /// @param out 回复长度；cap 小于 2 时为 0 / Reply length; 0 when cap is below 2.
  /// @return 总是 true（已填好回复）/ Always true (the reply is filled in).
  /// @note 在执行该 DAP 命令的任务里调用；保存、清除成功后经 settings 安排重启 / Called
  ///       in the task that executes the DAP command; a successful save or clear
  ///       schedules the restart through settings.
  static bool Handle(void* settings, const uint8_t* req, uint16_t len, uint8_t* resp,
                     uint16_t cap, uint16_t& out);

  /// @brief 同样的设置经控制口：请求是 op + 载荷（去掉 0x86 字节），应答是 op + 状态 + 载荷 /
  ///        The same settings through the control port: the request is op + payload (without
  ///        the 0x86 byte), the answer op + status + payload.
  /// @note XRDAP-Core Control::Handler 的签名；在执行上下文里调用 / The signature of
  ///       XRDAP-Core's Control::Handler; called in the execution context.
  static uint16_t Control(void* settings, const uint8_t* req, uint16_t len, uint8_t* resp,
                          uint16_t cap);

  /// @brief 一条 0x86 请求的字节数，DAP_ExecuteCommands / DAP_QueueCommands 里要用；签名
  ///        与 XRDAP-Core DapVendor::ExtraLength 相同 / Request bytes of one 0x86 command,
  ///        needed inside DAP_ExecuteCommands / DAP_QueueCommands; the signature matches
  ///        XRDAP-Core DapVendor::ExtraLength.
  /// @return 内容不完整或操作未知时 false / False when the content is incomplete or the
  ///         operation unknown.
  /// @note 纯解析，任何上下文 / Pure parsing, any context.
  static bool Length(const uint8_t* req, uint16_t avail, uint16_t& len);

 private:
  static bool Status(WifiSettings& settings, uint8_t* resp, uint16_t cap, uint16_t& out);
  static bool Set(WifiSettings& settings, const uint8_t* req, uint16_t len, uint8_t* resp,
                  uint16_t& out);
  static bool Clear(WifiSettings& settings, const uint8_t* req, uint16_t len,
                    uint8_t* resp, uint16_t& out);
};

}  // namespace XrDap
