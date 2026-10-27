// SPDX-License-Identifier: Apache-2.0
// 状态灯输入和按键：判据是用户定的 XR-DAP 灯语（红 = 有线、蓝 = 无线、紫 = 都没连或改
// 设置）、status_panel.hpp 写明的优先级，以及 A4 决定（按键按满 5 s 清除一次）。
//
// Status light inputs and the button: the oracles are the user's XR-DAP light rule (red
// = wired, blue = wireless, purple = nothing connected or a setting change), the
// priority stated in status_panel.hpp, and the A4 decision (holding the button 5 s
// clears once).
#include "link_state.hpp"
#include "test.hpp"

namespace
{
using XrDap::HoldDetector;
using XrDap::LinkInputs;
using XrDap::WifiLink;
using XrDap::Light::Link;
using XrDap::Light::Setting;

LinkInputs Inputs() { return LinkInputs{}; }
}  // namespace

TEST(link_nothing_connected_is_none)
{
  CHECK(XrDap::ClassifyLink(Inputs(), false) == Link::NONE);
}

TEST(link_usb_configured_or_recent_wired_command_is_wired)
{
  LinkInputs in = Inputs();
  in.usb_configured = true;
  CHECK(XrDap::ClassifyLink(in, false) == Link::WIRED);
  CHECK(XrDap::ClassifyLink(Inputs(), true) == Link::WIRED);
}

TEST(link_network_client_wins_over_wired)
{
  LinkInputs in = Inputs();
  in.net_client = true;
  in.usb_configured = true;
  CHECK(XrDap::ClassifyLink(in, true) == Link::WIRELESS);
}

TEST(link_wired_wins_over_wifi_only)
{
  LinkInputs in = Inputs();
  in.wifi = WifiLink::UP;
  CHECK(XrDap::ClassifyLink(in, false) == Link::WIRELESS);
  in.usb_configured = true;
  CHECK(XrDap::ClassifyLink(in, false) == Link::WIRED);
}

TEST(link_joining_wifi)
{
  LinkInputs in = Inputs();
  in.wifi = WifiLink::JOINING;
  CHECK(XrDap::ClassifyLink(in, false) == Link::JOINING);
}

TEST(setting_restart_pending_wins_over_button)
{
  LinkInputs in = Inputs();
  CHECK(XrDap::ClassifySetting(in) == Setting::NONE);
  in.button_held = true;
  CHECK(XrDap::ClassifySetting(in) == Setting::CHANGING);
  in.restart_pending = true;
  CHECK(XrDap::ClassifySetting(in) == Setting::APPLIED);
}

TEST(hold_fires_once_after_threshold)
{
  HoldDetector hold(5000);
  int fired = 0;
  for (int i = 0; i < 249; ++i)
  {
    fired += hold.Update(true, 20) ? 1 : 0;
  }
  CHECK(fired == 0);
  CHECK(hold.Held());
  fired += hold.Update(true, 20) ? 1 : 0;  // 5000 ms
  CHECK(fired == 1);
  for (int i = 0; i < 500; ++i)
  {
    fired += hold.Update(true, 20) ? 1 : 0;
  }
  CHECK(fired == 1);  // 一直按着也只清除一次 / held on, still only one clear
}

TEST(hold_release_restarts_the_count)
{
  HoldDetector hold(5000);
  for (int i = 0; i < 200; ++i)
  {
    CHECK(!hold.Update(true, 20));
  }
  CHECK(!hold.Update(false, 20));
  CHECK(!hold.Held());
  for (int i = 0; i < 249; ++i)
  {
    CHECK(!hold.Update(true, 20));
  }
  CHECK(hold.Update(true, 20));
}
