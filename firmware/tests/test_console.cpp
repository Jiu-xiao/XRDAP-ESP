// SPDX-License-Identifier: Apache-2.0
// 命令行：判据是 console_core.hpp 写明的规则和 README 的命令表 / Console: the oracle is
// the rules stated in console_core.hpp and the command table in the README.
#include <cstring>
#include <string>

#include "console_core.hpp"
#include "fakes.hpp"

namespace
{
using Test::FakeWifiSettings;

// 假运行状态：写一行固定文字，记下被调用的次数 / Fake runtime report: writes one fixed
// line and counts the calls.
struct FakeHeapReport final : XrDap::HeapReport
{
  int calls = 0;

  void Write(XrDap::ConsoleWriteFn write, void* ctx) override
  {
    ++calls;
    write(ctx, "heap 1234\r\n", 11);
  }
};

struct Console
{
  FakeWifiSettings wifi;
  FakeHeapReport heap;
  std::string out;
  XrDap::ConsoleCore core{wifi, &heap, &Console::Write, this};

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
  CHECK(c.wifi.cleared_by == XrDap::EraseSource::CONSOLE);
  CHECK(c.wifi.restarts == 1);
}

// wifi status 给出清除来源和复位原因，没有热点参数时也给（A4 用户决定 ⑤）/ wifi status
// shows the erase source and the reset reason, also without credentials (A4 user
// decision 5).
TEST(console_status_shows_erase_source_and_reset_reason)
{
  Console c;
  c.wifi.status.last_erase = XrDap::EraseSource::BUTTON;
  c.wifi.status.reset_reason = XrDap::ResetReason::SOFTWARE;
  c.Type("wifi status\r");
  CHECK(c.Printed("上次清除：按键"));
  CHECK(c.Printed("上次复位：软件重启"));
}

// 看门狗重启后给出超时时各核在运行的任务 / After a watchdog reboot, show the task each
// core was running at the timeout.
TEST(console_status_shows_watchdog_tasks)
{
  Console c;
  c.wifi.status.reset_reason = XrDap::ResetReason::WATCHDOG;
  std::memcpy(c.wifi.status.watchdog_task[0], "dap_serial", 11);
  std::memcpy(c.wifi.status.watchdog_task[1], "dap", 4);
  c.Type("wifi status\r");
  CHECK(c.Printed("上次复位：看门狗"));
  CHECK(c.Printed("看门狗超时时在运行：核 0 dap_serial，核 1 dap"));
}

TEST(console_status_without_watchdog_record)
{
  Console c;
  c.Type("wifi status\r");
  CHECK(!c.Printed("看门狗超时时在运行"));
}

TEST(console_status_reports_storage_repair)
{
  Console c;
  c.wifi.status.storage_repaired = true;
  c.Type("wifi status\r");
  CHECK(c.Printed("NVS 损坏"));
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

// heap 命令给出堆和任务栈的状态，S2 板测试时据此判断内存余量 / The heap command shows
// the heap and task-stack status, which the S2 board test uses to judge the memory
// margin.
TEST(console_heap_prints_runtime_report)
{
  Console c;
  c.Type("heap\r");
  CHECK(c.heap.calls == 1);
  CHECK(c.Printed("heap 1234"));
  CHECK(c.wifi.restarts == 0);
}

TEST(console_help_lists_heap)
{
  Console c;
  c.Type("help\r");
  CHECK(c.Printed("heap"));
}

TEST(console_heap_without_report_is_unknown)
{
  FakeWifiSettings wifi;
  std::string out;
  XrDap::ConsoleCore core{wifi, nullptr, [](void* ctx, const char* text, size_t len)
                          { static_cast<std::string*>(ctx)->append(text, len); }, &out};
  for (char ch : std::string("heap\r"))
  {
    core.OnByte(static_cast<uint8_t>(ch));
  }
  CHECK(out.find("未知命令") != std::string::npos);
}
