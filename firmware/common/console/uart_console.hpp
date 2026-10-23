// SPDX-License-Identifier: Apache-2.0
#pragma once

// 独占一个串口的 Wi-Fi 命令行（S3 的 USB 虚拟串口、C3 的 UART0），自带读取任务。与 DAP
// 链路共用一个口时用 SerialMux（serial_mux.hpp）。
//
// Wi-Fi console that owns a whole serial port (S3 USB CDC, C3 UART0) and runs its own
// reader task. Use SerialMux (serial_mux.hpp) to share a port with the DAP link.

#include <cstddef>
#include <cstdint>

#include "console_core.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libxr.hpp"
#include "probe_config.hpp"

namespace XrDap
{

/// @brief 把一段文字写到 libxr 串口，最多等 200 ms / Write text to a libxr UART, waiting
///        up to 200 ms.
template <typename UartT>
void UartWrite(UartT& uart, const char* text, size_t len)
{
  LibXR::Semaphore sem;
  LibXR::WriteOperation op(sem, 200);
  (void)uart.Write(LibXR::ConstRawData{text, len}, op);
}

/// 独占一个串口的命令行 / Console that owns a serial port.
/// @tparam UartT libxr UART 类型 / libxr UART type.
template <typename UartT>
class UartConsole
{
 public:
  /// @param uart 串口 / Serial port.
  /// @param settings 命令行读写的 Wi-Fi 设置 / Wi-Fi settings the console works on.
  /// @param heap heap 命令的内容，可为空 / Content of the heap command; may be null.
  /// @note 生命周期都需长于本对象 / All must outlive this object.
  UartConsole(UartT& uart, WifiSettings& settings, HeapReport* heap)
      : uart_(uart), core_(settings, heap, &UartConsole::Write, this)
  {
  }

  /// @brief 在指定的核上启动命令行任务 / Start the console task on a core.
  void Start(int core)
  {
    xTaskCreatePinnedToCore(&UartConsole::TaskEntry, "console", Config::kConsoleStack,
                            this, Config::kConsolePriority, nullptr, core);
  }

 private:
  static void TaskEntry(void* arg) { static_cast<UartConsole*>(arg)->Run(); }

  void Run()
  {
    for (;;)
    {
      // 队列为空时睡眠让出 CPU；S3 的 USB 关闭期间读端口一直没有数据。
      if (uart_.read_port_->Size() == 0)
      {
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }
      uint8_t c = 0;
      LibXR::Semaphore sem;
      LibXR::ReadOperation op(sem, 100);
      if (uart_.Read(LibXR::RawData{&c, 1}, op) == LibXR::ErrorCode::OK)
      {
        core_.OnByte(c);
      }
    }
  }

  static void Write(void* ctx, const char* text, size_t len)
  {
    UartWrite(static_cast<UartConsole*>(ctx)->uart_, text, len);
  }

  UartT& uart_;
  ConsoleCore core_;
};

}  // namespace XrDap
