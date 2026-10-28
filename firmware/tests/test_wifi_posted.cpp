// SPDX-License-Identifier: Apache-2.0
// 经执行上下文改设置:每个调用恰好经一次 Call 转给真正的设置,返回值和副作用不变 / Changing
// settings through the execution context: each call goes to the real settings through exactly
// one Call, with the same return value and side effects.
#include "fakes.hpp"
#include "wifi_posted.hpp"

namespace
{
struct FakeExec
{
  int calls = 0;
  XrDap::Transport last = XrDap::Transport::NONE;
  void Call(XrDap::Transport t, void (*fn)(void*), void* ctx)
  {
    ++calls;
    last = t;
    fn(ctx);
  }
};
}  // namespace

TEST(posted_wifi_forwards_each_call_through_the_execution_context)
{
  Test::FakeWifiSettings real;
  FakeExec exec;
  XrDap::PostedWifiSettings<FakeExec> posted(exec, real);

  real.status.connected = true;
  CHECK(posted.Status().connected);
  CHECK(exec.calls == 1 && exec.last == XrDap::Transport::CONSOLE);

  CHECK(posted.Save("net", "password123") == XrDap::WifiSettings::SaveResult::OK);
  CHECK(real.saved_ssid == "net" && real.saved_password == "password123");
  CHECK(posted.Save("net", "x") == XrDap::WifiSettings::SaveResult::INVALID);
  CHECK(exec.calls == 3);

  CHECK(posted.Clear(XrDap::EraseSource::CONSOLE));
  CHECK(real.clears == 1 && real.cleared_by == XrDap::EraseSource::CONSOLE);
  real.storage_ok = false;
  CHECK(!posted.Clear(XrDap::EraseSource::BUTTON));
  CHECK(posted.Save("net", "password123") == XrDap::WifiSettings::SaveResult::STORAGE);

  posted.RestartLater();
  CHECK(real.restarts == 1);
  CHECK(exec.calls == 7);
}
