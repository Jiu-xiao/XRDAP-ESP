// SPDX-License-Identifier: Apache-2.0
// 0x86：判据是 XRDAP-Core docs/protocol.md 3.7 节 / 0x86: the oracle is XRDAP-Core
// docs/protocol.md section 3.7.
#include "fakes.hpp"
#include "wifi_command.hpp"

namespace
{
using Test::Bytes;
using Test::FakeWifiSettings;

Bytes Run(FakeWifiSettings& wifi, const Bytes& req, uint16_t cap = 64)
{
  Bytes resp(cap);
  uint16_t out = 0xFFFF;
  CHECK(XrDap::WifiCommand::Handle(&wifi, req.data(), static_cast<uint16_t>(req.size()),
                                   resp.data(), cap, out));
  resp.resize(out);
  return resp;
}

Bytes SetRequest(const char* ssid, const char* password)
{
  Bytes req = {0x86, 0x01, static_cast<uint8_t>(std::strlen(ssid))};
  Test::Append(req, Test::Str(ssid));
  req.push_back(static_cast<uint8_t>(std::strlen(password)));
  Test::Append(req, Test::Str(password));
  return req;
}
}  // namespace

static_assert(XrDap::WifiCommand::kCmd == 0x86, "command byte");
static_assert(XrDap::WifiCommand::kFeature == (1U << 4), "INFO feature bit 4");

TEST(wifi_cmd_query_without_credentials)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, {0x86, 0x00}), (Bytes{0x86, 0x00, 0x00, 0, 0, 0, 0, 0x00, 0x00}));
}

// 例子取自协议文档：C0 A8 01 14 即 192.168.1.20 / Example from the protocol document:
// C0 A8 01 14 is 192.168.1.20.
TEST(wifi_cmd_query_connected)
{
  FakeWifiSettings wifi;
  wifi.status.started = true;
  wifi.status.connected = true;
  std::memcpy(wifi.status.ssid, "lab", 4);
  const uint8_t ip[4] = {0xC0, 0xA8, 0x01, 0x14};
  std::memcpy(wifi.status.ip, ip, 4);
  wifi.status.rssi = -40;
  CHECK_BYTES(Run(wifi, {0x86, 0x00}), (Bytes{0x86, 0x00, 0x03, 0xC0, 0xA8, 0x01, 0x14,
                                              0xD8, 0x03, 'l', 'a', 'b'}));
}

TEST(wifi_cmd_query_rejects_extra_bytes)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, {0x86, 0x00, 0x00}), (Bytes{0x86, 0x01}));
}

TEST(wifi_cmd_query_reply_must_fit)
{
  FakeWifiSettings wifi;
  std::memcpy(wifi.status.ssid, "lab", 4);
  CHECK_BYTES(Run(wifi, {0x86, 0x00}, 11), (Bytes{0x86, 0x03}));
}

TEST(wifi_cmd_missing_op_and_undefined_op)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, {0x86}), (Bytes{0x86, 0x01}));
  CHECK_BYTES(Run(wifi, {0x86, 0x07}), (Bytes{0x86, 0x01}));
}

TEST(wifi_cmd_save_then_restart)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, SetRequest("lab", "password1")), (Bytes{0x86, 0x00}));
  CHECK(wifi.saved_ssid == "lab");
  CHECK(wifi.saved_password == "password1");
  CHECK(wifi.restarts == 1);
}

TEST(wifi_cmd_save_open_network)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, SetRequest("lab", "")), (Bytes{0x86, 0x00}));
  CHECK(wifi.saved_password.empty());
}

TEST(wifi_cmd_save_length_must_match)
{
  FakeWifiSettings wifi;
  Bytes req = SetRequest("lab", "password1");
  req.push_back(0x00);
  CHECK_BYTES(Run(wifi, req), (Bytes{0x86, 0x01}));
  req.resize(req.size() - 2);
  CHECK_BYTES(Run(wifi, req), (Bytes{0x86, 0x01}));
  CHECK(wifi.saves == 0);
  CHECK(wifi.restarts == 0);
}

TEST(wifi_cmd_save_rejects_bad_lengths)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, SetRequest("", "password1")), (Bytes{0x86, 0x01}));
  CHECK_BYTES(Run(wifi, SetRequest("123456789012345678901234567890123", "")),
              (Bytes{0x86, 0x01}));
  CHECK_BYTES(Run(wifi, SetRequest("lab", "1234567")), (Bytes{0x86, 0x01}));
  CHECK_BYTES(
      Run(wifi,
          SetRequest("lab",
                     "1234567890123456789012345678901234567890123456789012345678901234")),
      (Bytes{0x86, 0x01}));
  CHECK(wifi.saves == 0);
  CHECK(wifi.restarts == 0);
}

TEST(wifi_cmd_save_storage_failure)
{
  FakeWifiSettings wifi;
  wifi.storage_ok = false;
  CHECK_BYTES(Run(wifi, SetRequest("lab", "password1")), (Bytes{0x86, 0x03}));
  CHECK(wifi.restarts == 0);
}

TEST(wifi_cmd_clear_then_restart)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, {0x86, 0x02}), (Bytes{0x86, 0x00}));
  CHECK(wifi.clears == 1);
  CHECK(wifi.restarts == 1);
}

TEST(wifi_cmd_clear_storage_failure)
{
  FakeWifiSettings wifi;
  wifi.storage_ok = false;
  CHECK_BYTES(Run(wifi, {0x86, 0x02}), (Bytes{0x86, 0x03}));
  CHECK(wifi.restarts == 0);
}

TEST(wifi_cmd_tiny_buffer_gets_no_reply)
{
  FakeWifiSettings wifi;
  CHECK(Run(wifi, {0x86, 0x00}, 1).empty());
}
