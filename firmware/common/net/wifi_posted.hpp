// SPDX-License-Identifier: Apache-2.0
#pragma once

// 经执行上下文改 Wi-Fi 设置:命令行、清除按键这些入口不直接动设置,而是让 XRDAP-Core 的执行
// 上下文运行(Executor::Call),这样设置的读写和采样、DAP 命令、控制口请求一件一件做,不会同时
// 发生。几个入口共用一个实例时,这里用互斥量让它们排队。
//
// Changing the Wi-Fi settings through the execution context: entries such as the console and the
// clear button do not touch the settings directly but have XRDAP-Core's execution context run the
// call (Executor::Call), so reading and writing the settings is done one after the other with
// sampling, DAP commands and control requests. Entries sharing one instance queue on a mutex here.

#include "mutex.hpp"
#include "wifi_settings.hpp"
#include "xrdap/session/transport.hpp"

namespace XrDap
{

/// @tparam ExecT XRDAP-Core 的 Executor<DapT> / XRDAP-Core's Executor<DapT>.
template <typename ExecT>
class PostedWifiSettings final : public WifiSettings
{
 public:
  /// @param exec 执行器,执行任务要在运行 / The executor; its execution task must be running.
  /// @param real 真正的设置,生命周期需长于本对象 / The real settings; must outlive this object.
  PostedWifiSettings(ExecT& exec, WifiSettings& real) : exec_(exec), real_(real) {}

  WifiStatus Status() override
  {
    WifiStatus result{};
    Run([&] { result = real_.Status(); });
    return result;
  }

  SaveResult Save(const char* ssid, const char* password) override
  {
    SaveResult result = SaveResult::STORAGE;
    Run([&] { result = real_.Save(ssid, password); });
    return result;
  }

  bool Clear(EraseSource source) override
  {
    bool result = false;
    Run([&] { result = real_.Clear(source); });
    return result;
  }

  void RestartLater() override
  {
    Run([&] { real_.RestartLater(); });
  }

 private:
  template <typename F>
  void Run(F&& f)
  {
    LibXR::Mutex::LockGuard guard(mutex_);
    exec_.Call(
        Transport::CONSOLE, [](void* p) { (*static_cast<F*>(p))(); }, &f);
  }

  ExecT& exec_;
  WifiSettings& real_;
  LibXR::Mutex mutex_;
};

}  // namespace XrDap
