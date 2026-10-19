// SPDX-License-Identifier: Apache-2.0
#pragma once

// 一个串口同时跑 DAP 链路和文本命令行，无需切换模式。读到的每块数据先交给 DAP 链路
// （DapSerialTransport::Feed），它自己找帧头、校验 CRC，非帧字节直接丢弃。这一块没有执行
// 任何 DAP 帧、且最近 kQuietMs 内也没有 DAP 帧时，才把字节交给命令行（ConsoleCore）。
// 命令行只在收到一行人打的命令并回车后才输出，行内出现控制字符就整行丢弃，所以调试工具
// 不会收到命令行插进来的字。串口终端打开该口即可输入 help、wifi set 等命令；xrdap、
// xrdap-forward 照常连接。
//
// Runs the DAP link and a text console on one serial port without mode switching. Each
// chunk read is first fed to the DAP link (DapSerialTransport::Feed), which finds frame
// headers and checks CRC itself and discards non-frame bytes. Only when the chunk
// executed no DAP frame and no DAP frame was seen within kQuietMs are the bytes passed
// to the console (ConsoleCore). The console prints only after a human-typed line ends
// with Enter and drops any line containing control characters, so debug tools never
// receive console output. A serial terminal can type help, wifi set, etc. on this port
// while xrdap and xrdap-forward keep working.

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libxr.hpp"
#include "wifi_console.hpp"

namespace XrDap
{

/// 串口复用器 / Serial multiplexer.
/// @tparam LinkT DapSerialTransport 类型 / DapSerialTransport type.
/// @tparam UartT libxr UART 类型 / libxr UART type.
template <typename LinkT, typename UartT>
class SerialMux
{
 public:
  /// @param link DAP 串口链路 / DAP serial link.
  /// @param uart 共用的串口 / Shared serial port.
  /// @note 两者生命周期都需长于本对象 / Both must outlive this object.
  SerialMux(LinkT& link, UartT& uart)
      : link_(link), uart_(uart), console_(&SerialMux::Write, this)
  {
  }

  /// @brief 在指定的核上启动读任务，代替 link.Start()；DAP 命令在该任务里执行 / Start
  ///        the reader task on a core in place of link.Start(); DAP commands run in this
  ///        task.
  /// @param core 任务所在的核 / Core that runs the task.
  /// @param priority FreeRTOS 优先级 / FreeRTOS priority.
  void Start(int core, unsigned priority)
  {
    xTaskCreatePinnedToCore(&SerialMux::TaskEntry, "dap_serial", 8192, this, priority,
                            nullptr, core);
  }

 private:
  static constexpr uint32_t kQuietMs = 2000U;  // 最近这么久有 DAP 帧时不理会文本输入
  static constexpr size_t kChunk = 512U;

  static void TaskEntry(void* arg) { static_cast<SerialMux*>(arg)->Run(); }

  static uint32_t NowMs()
  {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  }

  void Run()
  {
    LibXR::Semaphore wait_sem;
    LibXR::Semaphore read_sem;
    uint32_t last_frame_ms = 0U;
    bool seen_frame = false;
    for (;;)
    {
      // 零长度读等到队列非空（不取走字节），再按实际字节数取一块，与
      // DapSerialTransport::Poll 相同。
      LibXR::ReadOperation wait_op(wait_sem, 50U);
      if (uart_.Read(LibXR::RawData(static_cast<void*>(nullptr), 0U), wait_op) !=
          LibXR::ErrorCode::OK)
      {
        continue;
      }
      const size_t queued = uart_.read_port_->Size();
      if (queued == 0U)
      {
        continue;
      }
      const size_t take = (queued < kChunk) ? queued : kChunk;
      LibXR::ReadOperation read_op(read_sem);
      if (uart_.Read(LibXR::RawData(buf_, take), read_op) != LibXR::ErrorCode::OK)
      {
        continue;
      }

      const uint32_t frames_before = link_.GetStats().frames;
      link_.Feed(buf_, take);
      const uint32_t now = NowMs();
      if (link_.GetStats().frames != frames_before)
      {
        seen_frame = true;
        last_frame_ms = now;
        console_.Reset();
        continue;
      }
      if (seen_frame && now - last_frame_ms < kQuietMs)
      {
        continue;
      }
      for (size_t i = 0; i < take; ++i)
      {
        console_.OnByte(buf_[i]);
      }
    }
  }

  static void Write(void* ctx, const char* text, size_t len)
  {
    auto* self = static_cast<SerialMux*>(ctx);
    LibXR::Semaphore sem;
    LibXR::WriteOperation op(sem, 200);
    (void)self->uart_.Write(LibXR::ConstRawData{text, len}, op);
  }

  LinkT& link_;
  UartT& uart_;
  ConsoleCore console_;
  uint8_t buf_[kChunk] = {};
};

}  // namespace XrDap
