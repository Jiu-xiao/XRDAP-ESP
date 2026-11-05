// SPDX-License-Identifier: Apache-2.0
#include "wifi_command.hpp"

#include <cstring>

#include "xrdap/vendor/protocol.hpp"
#include "xrdap/vendor/wifi_command.hpp"

namespace XrDap
{
namespace Codec = Vendor::WifiCommand;
using Vendor::kStatusArg;
using Vendor::kStatusOk;
using Vendor::kStatusResource;
using Vendor::Reply;

bool WifiCommand::Length(const uint8_t* req, uint16_t avail, uint16_t& len)
{
  return Codec::RequestLength(req, avail, len);
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
  Codec::Request request{};
  if (len < 2U || Codec::ParseRequest(&req[1], static_cast<uint16_t>(len - 1U),
                                      request) != Codec::Parse::OK)
  {
    return Reply(resp, kStatusArg, out);
  }
  switch (request.op)
  {
    case Codec::Op::QUERY:
      return Status(settings, resp, cap, out);
    case Codec::Op::SAVE:
      return Set(settings, request, resp, out);
    case Codec::Op::CLEAR:
      return Clear(settings, resp, out);
  }
  return Reply(resp, kStatusArg, out);
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
  const auto op = static_cast<Codec::Op>(req[0]);
  if (op != Codec::Op::QUERY && op != Codec::Op::SAVE && op != Codec::Op::CLEAR)
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
  Codec::Query query{};
  query.started = st.started;
  query.connected = st.connected;
  query.storage_repaired = st.storage_repaired;
  std::memcpy(query.ip, st.ip, sizeof(query.ip));
  query.rssi = st.rssi;
  query.ssid = st.ssid;
  query.ssid_len = static_cast<uint8_t>(strnlen(st.ssid, kSsidMax));
  query.last_erase = st.last_erase;
  query.reset_reason = st.reset_reason;
  out = Codec::EncodeQuery(kCmd, query, resp, cap);
  return true;
}

bool WifiCommand::Set(WifiSettings& settings, const Codec::Request& request,
                      uint8_t* resp, uint16_t& out)
{
  char ssid[kSsidMax + 1U] = {};
  char password[kPasswordMax + 1U] = {};
  std::memcpy(ssid, request.ssid, request.ssid_len);
  std::memcpy(password, request.password, request.password_len);
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

bool WifiCommand::Clear(WifiSettings& settings, uint8_t* resp, uint16_t& out)
{
  if (!settings.Clear(EraseSource::VENDOR_COMMAND))
  {
    return Reply(resp, kStatusResource, out);
  }
  settings.RestartLater();
  return Reply(resp, kStatusOk, out);
}

}  // namespace XrDap
