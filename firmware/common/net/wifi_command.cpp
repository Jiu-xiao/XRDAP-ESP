// SPDX-License-Identifier: Apache-2.0
#include "wifi_command.hpp"

#include <cstring>

#include "vendor_protocol.hpp"

namespace XrDap
{
using namespace Vendor;

namespace
{
constexpr uint8_t kOpStatus = 0x00U;
constexpr uint8_t kOpSet = 0x01U;
constexpr uint8_t kOpClear = 0x02U;
constexpr uint8_t kFlagStarted = 0x01U;
constexpr uint8_t kFlagConnected = 0x02U;
constexpr uint8_t kFlagStorageRepaired = 0x04U;
// 状态回复：命令、状态、标志、IP(4)、信号、热点名长度，热点名，清除来源、复位原因 /
// Status reply: command, status, flags, IP(4), RSSI, SSID length, SSID, erase source,
// reset reason.
constexpr size_t kStatusHead = 9U;
constexpr size_t kStatusTail = 2U;

// 清除请求在 op 之后要带的确认字节，由 XRDAP-Core protocol.md 3.7 定义，不兼容不带确认的
// 旧请求（A4 用户决定 ③）。字节定下之前没有参数，请求仍是 86 02 / Confirm bytes the
// clear request carries after the op, defined by XRDAP-Core protocol.md 3.7; old
// requests without them are not accepted (A4 user decision 3). Until the bytes are
// defined the request has no arguments and stays 86 02.
bool ClearConfirmed(const uint8_t* /*args*/, uint16_t count) { return count == 0U; }
}  // namespace

bool WifiCommand::Handle(void* ctx, const uint8_t* req, uint16_t len, uint8_t* resp,
                         uint16_t cap, uint16_t& out)
{
  auto& settings = *static_cast<WifiSettings*>(ctx);
  if (cap < 2U)
  {
    out = 0U;
    return true;
  }
  resp[0] = kCmd;
  if (len < 2U)
  {
    return Reply(resp, kStatusArg, out);
  }
  switch (req[1])
  {
    case kOpStatus:
      return (len == 2U) ? Status(settings, resp, cap, out)
                         : Reply(resp, kStatusArg, out);
    case kOpSet:
      return Set(settings, req, len, resp, out);
    case kOpClear:
      return Clear(settings, req, len, resp, out);
    default:
      return Reply(resp, kStatusArg, out);
  }
}

bool WifiCommand::Status(WifiSettings& settings, uint8_t* resp, uint16_t cap,
                         uint16_t& out)
{
  const WifiStatus st = settings.Status();
  const size_t ssid_len = strnlen(st.ssid, kSsidMax);
  const size_t total = kStatusHead + ssid_len + kStatusTail;
  if (cap < total)
  {
    return Reply(resp, kStatusResource, out);
  }
  resp[1] = kStatusOk;
  resp[2] = static_cast<uint8_t>((st.started ? kFlagStarted : 0U) |
                                 (st.connected ? kFlagConnected : 0U) |
                                 (st.storage_repaired ? kFlagStorageRepaired : 0U));
  std::memcpy(&resp[3], st.ip, 4U);
  resp[7] = static_cast<uint8_t>(st.rssi);
  resp[8] = static_cast<uint8_t>(ssid_len);
  std::memcpy(&resp[kStatusHead], st.ssid, ssid_len);
  resp[kStatusHead + ssid_len] = static_cast<uint8_t>(st.last_erase);
  resp[kStatusHead + ssid_len + 1U] = static_cast<uint8_t>(st.reset_reason);
  out = static_cast<uint16_t>(total);
  return true;
}

// 0x86 0x01 ssid_len ssid[ssid_len] pw_len pw[pw_len]
bool WifiCommand::Set(WifiSettings& settings, const uint8_t* req, uint16_t len,
                      uint8_t* resp, uint16_t& out)
{
  if (len < 4U)
  {
    return Reply(resp, kStatusArg, out);
  }
  const uint8_t ssid_len = req[2];
  if (len < 3U + ssid_len + 1U)
  {
    return Reply(resp, kStatusArg, out);
  }
  const uint8_t pw_len = req[3U + ssid_len];
  if (len != 4U + ssid_len + pw_len || ssid_len > kSsidMax || pw_len > kPasswordMax)
  {
    return Reply(resp, kStatusArg, out);
  }
  char ssid[kSsidMax + 1U] = {};
  char password[kPasswordMax + 1U] = {};
  std::memcpy(ssid, &req[3], ssid_len);
  std::memcpy(password, &req[4U + ssid_len], pw_len);
  const WifiSettings::SaveResult result = settings.Save(ssid, password);
  std::memset(password, 0, sizeof(password));
  if (result == WifiSettings::SaveResult::INVALID)
  {
    return Reply(resp, kStatusArg, out);
  }
  if (result != WifiSettings::SaveResult::OK)
  {
    return Reply(resp, kStatusResource, out);
  }
  settings.RestartLater();
  return Reply(resp, kStatusOk, out);
}

bool WifiCommand::Clear(WifiSettings& settings, const uint8_t* req, uint16_t len,
                        uint8_t* resp, uint16_t& out)
{
  if (!ClearConfirmed(&req[2], static_cast<uint16_t>(len - 2U)))
  {
    return Reply(resp, kStatusArg, out);
  }
  if (!settings.Clear(EraseSource::VENDOR_COMMAND))
  {
    return Reply(resp, kStatusResource, out);
  }
  settings.RestartLater();
  return Reply(resp, kStatusOk, out);
}

}  // namespace XrDap
