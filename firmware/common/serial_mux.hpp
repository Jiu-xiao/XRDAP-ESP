// SPDX-License-Identifier: Apache-2.0
#pragma once

// 一个串口同时跑 DAP 链路和文本命令行，不需要切换模式。
//
// 读到的每一块数据先交给 DAP 链路（libxr DapSerialTransport::Feed）：它自己找帧头、校验 CRC，
// 不是帧的字节直接丢掉。这一块里没有执行任何 DAP 帧、并且最近 kQuietMs 内也没有 DAP 帧时，
// 再把这些字节交给命令行（ConsoleCore）。命令行只在收到一行人打的命令并回车后才输出，一行里
// 出现控制字符就整行丢弃，所以调试工具不会收到命令行插进来的字。
//
// 用串口终端打开这个口就能输入 help、wifi set 等命令；xrdap、xrdap-forward 照常连接。

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libxr.hpp"
#include "wifi_console.hpp"

namespace XrDap
{

template <typename LinkT, typename UartT>
class SerialMux
{
 public:
  SerialMux(LinkT& link, UartT& uart) : link_(link), uart_(uart), console_(&SerialMux::Write, this)
  {
  }

  /// 在指定的核上启动读线程（代替 link.Start()；DAP 命令在这个线程里执行）。
  void Start(int core, unsigned priority)
  {
    xTaskCreatePinnedToCore(&SerialMux::TaskEntry, "dap_serial", 8192, this, priority, nullptr,
                            core);
  }

 private:
  static constexpr uint32_t kQuietMs = 2000U;  // 最近这么久有 DAP 帧时不理会文本输入
  static constexpr size_t kChunk = 512U;

  static void TaskEntry(void* arg) { static_cast<SerialMux*>(arg)->Run(); }

  static uint32_t NowMs() { return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS); }

  void Run()
  {
    LibXR::Semaphore wait_sem;
    LibXR::Semaphore read_sem;
    uint32_t last_frame_ms = 0U;
    bool seen_frame = false;
    for (;;)
    {
      // 零长度读等到队列非空（不取走字节），再按实际字节数取一块，和 DapSerialTransport::Poll 相同
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
