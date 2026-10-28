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

// 状态末尾是清除来源和复位原因（A4 用户决定 ⑤）/ The status ends with the erase source
// and the reset reason (A4 user decision 5).
TEST(wifi_cmd_query_without_credentials)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, {0x86, 0x00}),
              (Bytes{0x86, 0x00, 0x00, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x00}));
}

TEST(wifi_cmd_query_reports_erase_source_and_reset_reason)
{
  FakeWifiSettings wifi;
  wifi.status.last_erase = XrDap::EraseSource::VENDOR_COMMAND;
  wifi.status.reset_reason = XrDap::ResetReason::SOFTWARE;
  CHECK_BYTES(Run(wifi, {0x86, 0x00}),
              (Bytes{0x86, 0x00, 0x00, 0, 0, 0, 0, 0x00, 0x00, 0x03, 0x02}));
}

// NVS 损坏后擦除重建：标志位 2（A4 用户决定 ④）/ NVS rebuilt after damage: flag bit 2
// (A4 user decision 4).
TEST(wifi_cmd_query_reports_storage_repair)
{
  FakeWifiSettings wifi;
  wifi.status.storage_repaired = true;
  wifi.status.last_erase = XrDap::EraseSource::STORAGE_REPAIR;
  wifi.status.reset_reason = XrDap::ResetReason::POWER_ON;
  CHECK_BYTES(Run(wifi, {0x86, 0x00}),
              (Bytes{0x86, 0x00, 0x04, 0, 0, 0, 0, 0x00, 0x00, 0x04, 0x01}));
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
                                              0xD8, 0x03, 'l', 'a', 'b', 0x00, 0x00}));
}

// 查询不需要会话所有权，任何链路都能发（XRDAP-Core B2 扩展），所以它只读：不保存、不清除、
// 不重启 / A query needs no session ownership and may come from any link (XRDAP-Core B2
// extension), so it is read-only: no save, no clear, no restart.
TEST(wifi_cmd_query_changes_nothing)
{
  FakeWifiSettings wifi;
  std::memcpy(wifi.status.ssid, "lab", 4);
  for (int i = 0; i < 3; ++i)
  {
    (void)Run(wifi, {0x86, 0x00});
  }
  CHECK(wifi.saves == 0);
  CHECK(wifi.clears == 0);
  CHECK(wifi.restarts == 0);
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
  CHECK_BYTES(Run(wifi, {0x86, 0x00}, 13), (Bytes{0x86, 0x03}));
  CHECK(Run(wifi, {0x86, 0x00}, 14).size() == 14);
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
  CHECK_BYTES(Run(wifi, {0x86, 0x02, 0xA5}), (Bytes{0x86, 0x00}));
  CHECK(wifi.clears == 1);
  CHECK(wifi.cleared_by == XrDap::EraseSource::VENDOR_COMMAND);
  CHECK(wifi.restarts == 1);
}

TEST(wifi_cmd_clear_storage_failure)
{
  FakeWifiSettings wifi;
  wifi.storage_ok = false;
  CHECK_BYTES(Run(wifi, {0x86, 0x02, 0xA5}), (Bytes{0x86, 0x03}));
  CHECK(wifi.restarts == 0);
}

TEST(wifi_cmd_tiny_buffer_gets_no_reply)
{
  FakeWifiSettings wifi;
  CHECK(Run(wifi, {0x86, 0x00}, 1).empty());
}

// 清除要带确认字节 0xA5（协议 3.7），缺了、错了或多了都回 01、不清除 / Clear needs the
// confirm byte 0xA5 (protocol 3.7); missing, wrong or extra bytes get 01 and erase
// nothing.
TEST(wifi_cmd_clear_rejects_unexpected_bytes)
{
  FakeWifiSettings wifi;
  CHECK_BYTES(Run(wifi, {0x86, 0x02}), (Bytes{0x86, 0x01}));
  CHECK_BYTES(Run(wifi, {0x86, 0x02, 0x00}), (Bytes{0x86, 0x01}));
  CHECK_BYTES(Run(wifi, {0x86, 0x02, 0xA5, 0xA5}), (Bytes{0x86, 0x01}));
  CHECK(wifi.clears == 0);
  CHECK(wifi.restarts == 0);
}

namespace
{
uint16_t Length(const Bytes& req)
{
  uint16_t len = 0xFFFF;
  return XrDap::WifiCommand::Length(req.data(), static_cast<uint16_t>(req.size()), len)
             ? len
             : 0U;
}
}  // namespace

// DAP_ExecuteCommands / DAP_QueueCommands 里一条 0x86 的字节数：查询 2，清除 3，保存
// 3 + ssid + 1 + 密码（协议 3.7 的例子共 15 字节）/ Bytes of one 0x86 inside
// DAP_ExecuteCommands / DAP_QueueCommands: query 2, clear 3, save 3 + ssid + 1 + password
// (the example of protocol 3.7 is 15 bytes).
TEST(wifi_cmd_length_of_each_operation)
{
  CHECK(Length({0x86, 0x00}) == 2U);
  CHECK(Length({0x86, 0x02, 0xA5}) == 3U);
  CHECK(Length({0x86, 0x01, 0x03, 0x6C, 0x61, 0x62, 0x08, 0x31, 0x32, 0x33, 0x34, 0x35,
                0x36, 0x37, 0x38}) == 15U);
}

TEST(wifi_cmd_length_rejects_incomplete_or_unknown)
{
  CHECK(Length({0x86}) == 0U);
  CHECK(Length({0x86, 0x02}) == 0U);
  CHECK(Length({0x86, 0x07}) == 0U);
  CHECK(Length({0x86, 0x01, 0x03, 0x6C, 0x61}) == 0U);
  CHECK(Length({0x85, 0x00}) == 0U);
}

// 控制口:请求是去掉 0x86 的 op + 载荷,应答以 op 开头,其余与 0x86 的应答相同 / The control
// port: the request is op + payload without the 0x86 byte, the answer starts with the op and is
// otherwise the same as the 0x86 answer.
TEST(wifi_control_matches_the_dap_command)
{
  FakeWifiSettings a;
  FakeWifiSettings b;
  a.status.connected = true;
  b.status.connected = true;
  Bytes dap = Run(a, {0x86, 0x00});
  Bytes ctl(64);
  const uint8_t req[1] = {0x00};
  const uint16_t n = XrDap::WifiCommand::Control(&b, req, 1U, ctl.data(), 64U);
  ctl.resize(n);
  dap[0] = 0x00;  // the op instead of 0x86
  CHECK_BYTES(ctl, dap);

  // Save and clear go through the same code.
  Bytes save = SetRequest("net", "password123");
  Bytes ctl_req(save.begin() + 1, save.end());
  Bytes resp(64);
  const uint16_t m = XrDap::WifiCommand::Control(&b, ctl_req.data(),
                                                 static_cast<uint16_t>(ctl_req.size()),
                                                 resp.data(), 64U);
  resp.resize(m);
  CHECK_BYTES(resp, (Bytes{0x01, 0x00}));
}

TEST(wifi_control_rejects_empty_and_oversized_requests)
{
  FakeWifiSettings wifi;
  Bytes resp(64);
  const uint8_t big[200] = {0x01};
  CHECK(XrDap::WifiCommand::Control(&wifi, big, 200U, resp.data(), 64U) == 2U);
  CHECK(resp[0] == 0x01 && resp[1] == XrDap::Control::kStatusBadRequest);
  CHECK(XrDap::WifiCommand::Control(&wifi, nullptr, 0U, resp.data(), 64U) == 2U);
  CHECK(resp[1] == XrDap::Control::kStatusBadRequest);
}
