// SPDX-License-Identifier: Apache-2.0
// 命令行：判据是 console_core.hpp 写明的规则和 README 的命令表 / Console: the oracle is
// the rules stated in console_core.hpp and the command table in the README.
#include <string>

#include "console_core.hpp"
#include "fakes.hpp"

namespace
{
using Test::FakeWifiSettings;

struct Console
{
  FakeWifiSettings wifi;
  std::string out;
  XrDap::ConsoleCore core{wifi, &Console::Write, this};

  static void Write(void* ctx, const char* text, size_t len)
  {
    static_cast<Console*>(ctx)->out.append(text, len);
  }

  void Type(const std::string& s)
  {
    for (char c : s)
    {
      core.OnByte(static_cast<uint8_t>(c));
    }
  }

  bool Printed(const char* s) const { return out.find(s) != std::string::npos; }
};
}  // namespace

TEST(console_set_saves_and_restarts)
{
  Console c;
  c.Type("wifi set lab password1\r");
  CHECK(c.wifi.saves == 1);
  CHECK(c.wifi.saved_ssid == "lab");
  CHECK(c.wifi.saved_password == "password1");
  CHECK(c.wifi.restarts == 1);
  CHECK(c.Printed("已保存"));
}

TEST(console_set_quoted_values_and_open_network)
{
  Console c;
  c.Type("wifi set \"lab 2\"\n");
  CHECK(c.wifi.saves == 1);
  CHECK(c.wifi.saved_ssid == "lab 2");
  CHECK(c.wifi.saved_password.empty());
}

TEST(console_set_rejects_bad_lengths_without_restart)
{
  Console c;
  c.Type("wifi set lab short\r");
  CHECK(c.wifi.saves == 0);
  CHECK(c.wifi.restarts == 0);
  CHECK(c.Printed("8 到 63"));
}

TEST(console_set_storage_failure_does_not_restart)
{
  Console c;
  c.wifi.storage_ok = false;
  c.Type("wifi set lab password1\r");
  CHECK(c.wifi.restarts == 0);
  CHECK(c.Printed("保存失败"));
}

TEST(console_clear_clears_and_restarts)
{
  Console c;
  c.Type("wifi clear\r");
  CHECK(c.wifi.clears == 1);
  CHECK(c.wifi.restarts == 1);
}

TEST(console_status_without_credentials)
{
  Console c;
  c.Type("wifi status\r");
  CHECK(c.Printed("没有保存热点参数"));
}

TEST(console_status_connected_shows_ip_and_rssi)
{
  Console c;
  c.wifi.status.started = true;
  c.wifi.status.connected = true;
  std::memcpy(c.wifi.status.ssid, "lab", 4);
  const uint8_t ip[4] = {192, 168, 1, 20};
  std::memcpy(c.wifi.status.ip, ip, 4);
  c.wifi.status.rssi = -40;
  c.Type("wifi status\r");
  CHECK(c.Printed("lab"));
  CHECK(c.Printed("192.168.1.20"));
  CHECK(c.Printed("-40 dBm"));
}

TEST(console_crlf_is_one_line_ending)
{
  Console c;
  c.Type("\r\n");
  CHECK(c.out == "\r\n> ");
}

TEST(console_backspace_edits_the_line)
{
  Console c;
  c.Type("wifi clearx\b\r");
  CHECK(c.wifi.clears == 1);
}

TEST(console_unknown_command_is_reported)
{
  Console c;
  c.Type("reboot\r");
  CHECK(c.Printed("未知命令"));
  CHECK(c.wifi.restarts == 0);
}

// 与二进制协议共用一个口：以 'Z' 开头的行不回显、不执行、不出提示符 / Shared with a
// binary protocol: a line starting with 'Z' is not echoed, not run, and gets no prompt.
TEST(console_drops_frame_start_line_silently)
{
  Console c;
  c.Type("Zwifi clear\r");
  CHECK(c.out.empty());
  CHECK(c.wifi.clears == 0);
}

// 行内出现控制字符（DAP 帧头必带 0x00）：整行作废，不执行、不出提示符 / A control
// character in the line (a DAP header always has 0x00): the line is void, not run, no
// prompt.
TEST(console_drops_line_with_control_byte)
{
  Console c;
  std::string line = "wifi clear";
  line += '\0';
  line += "\r";
  c.Type(line);
  CHECK(c.wifi.clears == 0);
  CHECK(!c.Printed("> "));
}
