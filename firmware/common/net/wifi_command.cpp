// SPDX-License-Identifier: Apache-2.0
#include "wifi_command.hpp"

#include <cstring>

#include "xrdap/vendor/protocol.hpp"

namespace XrDap
{
using namespace Vendor;

namespace
{
constexpr uint8_t kOpStatus = 0x00U;
constexpr uint8_t kOpSet = 0x01U;
constexpr uint8_t kOpClear = 0x02U;
// 清除必须带的确认字节：请求为 86 02 A5 / Confirm byte a clear must carry: the request is
// 86 02 A5.
constexpr uint8_t kClearConfirm = 0xA5U;
constexpr uint8_t kFlagStarted = 0x01U;
constexpr uint8_t kFlagConnected = 0x02U;
constexpr uint8_t kFlagStorageRepaired = 0x04U;
// 状态回复：命令、状态、标志、IP(4)、信号、热点名长度，热点名，清除来源、复位原因 /
// Status reply: command, status, flags, IP(4), RSSI, SSID length, SSID, erase source,
// reset reason.
constexpr size_t kStatusHead = 9U;
constexpr size_t kStatusTail = 2U;
}  // namespace

bool WifiCommand::Length(const uint8_t* req, uint16_t avail, uint16_t& len)
{
  if (avail < 2U || req[0] != kCmd)
  {
    return false;
  }
  uint32_t need = 0U;
  switch (req[1])
  {
    case kOpStatus:
      need = 2U;
      break;
    case kOpClear:
      need = 3U;
      break;
    case kOpSet:
      if (avail < 3U)
      {
        return false;
      }
      need = 3U + req[2];
      if (need >= avail)
      {
        return false;
      }
      need += 1U + req[need];
      break;
    default:
      return false;
  }
  if (need > avail)
  {
    return false;
  }
  len = static_cast<uint16_t>(need);
  return true;
}

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

uint16_t WifiCommand::Control(void* ctx, const uint8_t* req, uint16_t len, uint8_t* resp,
                              uint16_t cap)
{
  // 设置、清除是最长的请求：1 个 op + 1 + ssid + 1 + 密码 / The longest request is a
  // save: op + 1 + ssid + 1 + password.
  uint8_t command[4U + kSsidMax + kPasswordMax];
  if (req == nullptr || len < 1U || len + 1U > sizeof(command))
  {
    return ::XrDap::Control::StatusOnly(len < 1U || req == nullptr ? 0U : req[0],
                                        ::XrDap::Control::kStatusBadRequest, resp, cap);
  }
  if (req[0] != kOpStatus && req[0] != kOpSet && req[0] != kOpClear)
  {
    return ::XrDap::Control::StatusOnly(req[0], ::XrDap::Control::kStatusUnsupported,
                                        resp, cap);
  }
  command[0] = kCmd;
  std::memcpy(&command[1], req, len);
  uint16_t out = 0U;
  (void)Handle(ctx, command, static_cast<uint16_t>(len + 1U), resp, cap, out);
  if (out >= 1U)
  {
    resp[0] = req[0];  // 应答以 op 开头 / The answer starts with the op.
  }
  return out;
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
  // 86 02 A5：确认字节不对就不清除，不兼容不带确认的旧请求（A4 用户决定 ③）/ 86 02 A5:
  // nothing is cleared without the right confirm byte; the old request without it is not
  // accepted (A4 user decision 3).
  if (len != 3U || req[2] != kClearConfirm)
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
