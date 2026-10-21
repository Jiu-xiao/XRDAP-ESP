// SPDX-License-Identifier: Apache-2.0
#include "wifi_command.hpp"

#include <cstring>

#include "restart.hpp"
#include "vendor_protocol.hpp"
#include "wifi_sta.hpp"

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
// 留出时间让回复发出去 / Time for the reply to go out.
constexpr uint32_t kRestartDelayMs = 500U;
}  // namespace

void WifiCommand::Install(DapVendor& vendor)
{
  vendor.SetExtraCommand(kCmd, kFeature, &WifiCommand::Handle, nullptr);
}

bool WifiCommand::Handle(void*, const uint8_t* req, uint16_t len, uint8_t* resp,
                         uint16_t cap, uint16_t& out)
{
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
      return (len == 2U) ? Status(resp, cap, out) : Reply(resp, kStatusArg, out);
    case kOpSet:
      return Set(req, len, resp, out);
    case kOpClear:
      return Clear(len, resp, out);
    default:
      return Reply(resp, kStatusArg, out);
  }
}

bool WifiCommand::Status(uint8_t* resp, uint16_t cap, uint16_t& out)
{
  const WifiStatus st = WifiStaGetStatus();
  const size_t ssid_len = strnlen(st.ssid, sizeof(st.ssid) - 1U);
  if (cap < 9U + ssid_len)
  {
    return Reply(resp, kStatusResource, out);
  }
  resp[1] = kStatusOk;
  resp[2] = static_cast<uint8_t>((st.started ? kFlagStarted : 0U) |
                                 (st.connected ? kFlagConnected : 0U));
  std::memcpy(&resp[3], st.ip, 4U);
  resp[7] = static_cast<uint8_t>(st.rssi);
  resp[8] = static_cast<uint8_t>(ssid_len);
  std::memcpy(&resp[9], st.ssid, ssid_len);
  out = static_cast<uint16_t>(9U + ssid_len);
  return true;
}

// 0x86 0x01 ssid_len ssid[ssid_len] pw_len pw[pw_len]
bool WifiCommand::Set(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t& out)
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
  if (len != 4U + ssid_len + pw_len || ssid_len > 32U || pw_len > 63U)
  {
    return Reply(resp, kStatusArg, out);
  }
  char ssid[33] = {};
  char password[64] = {};
  std::memcpy(ssid, &req[3], ssid_len);
  std::memcpy(password, &req[4U + ssid_len], pw_len);
  const esp_err_t err = WifiStaSave(ssid, password);
  std::memset(password, 0, sizeof(password));
  if (err == ESP_ERR_INVALID_ARG)
  {
    return Reply(resp, kStatusArg, out);
  }
  if (err != ESP_OK)
  {
    return Reply(resp, kStatusResource, out);
  }
  RestartLater(kRestartDelayMs);
  return Reply(resp, kStatusOk, out);
}

bool WifiCommand::Clear(uint16_t len, uint8_t* resp, uint16_t& out)
{
  if (len != 2U)
  {
    return Reply(resp, kStatusArg, out);
  }
  if (WifiStaClear() != ESP_OK)
  {
    return Reply(resp, kStatusResource, out);
  }
  RestartLater(kRestartDelayMs);
  return Reply(resp, kStatusOk, out);
}

}  // namespace XrDap
