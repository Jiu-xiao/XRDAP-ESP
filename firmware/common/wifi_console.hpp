// SPDX-License-Identifier: Apache-2.0
#pragma once

// 配置 Wi-Fi 的文本命令行。ConsoleCore 按字节处理输入（行编辑、执行命令），输出交给回调；
// WifiConsole 让它独占一个串口（S3 的 USB 虚拟串口、C3 的 UART0）；SerialMux（serial_mux.hpp）
// 让它和串口 DAP 链路共用一个口。
//   help                         列出命令
//   wifi set <热点> [<密码>]     保存热点参数后重启。热点或密码里有空格时用双引号括起来；
//                                不填密码表示开放网络
//   wifi status                  查看热点、是否连接、IP、信号强度
//   wifi clear                   清除热点参数后重启
// 串口终端设置：波特率任意，换行用 CR 或 LF 都可以。输入的字符会回显，密码也会显示在你自己的终端上。

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libxr.hpp"
#include "wifi_sta.hpp"

namespace XrDap
{

/// 命令行核心：一次喂一个字节。只在收到一行合法的命令并回车后才输出，所以和二进制协议
/// 共用一个口时不会往里插字：一行里出现控制字符（DAP 帧头必然带 0x00）或以 'Z'（帧起始字节）
/// 开头时，整行丢弃，不回显、不提示。
class ConsoleCore
{
 public:
  using WriteFn = void (*)(void* ctx, const char* text, size_t len);

  ConsoleCore(WriteFn write, void* ctx) : write_(write), ctx_(ctx) {}

  /// 丢弃正在输入的一行（不输出）。
  void Reset()
  {
    len_ = 0;
    junk_ = false;
    last_was_cr_ = false;
  }

  void OnByte(uint8_t c)
  {
    if (c == '\n' && last_was_cr_)  // CR LF 只算一次换行
    {
      last_was_cr_ = false;
      return;
    }
    last_was_cr_ = (c == '\r');

    if (c == '\r' || c == '\n')
    {
      const bool run = !junk_ && len_ > 0;
      const bool show = !junk_;
      line_[len_] = 0;
      len_ = 0;
      junk_ = false;
      if (show)
      {
        Print("\r\n");
        if (run)
        {
          Handle(line_);
        }
        Print("> ");
      }
      return;
    }
    if (junk_)
    {
      return;
    }
    if (c == 0x08 || c == 0x7F)  // 退格
    {
      if (len_ > 0)
      {
        --len_;
        Print("\b \b");
      }
      return;
    }
    if (c < 0x20 || (len_ == 0 && c == 'Z'))
    {
      // 不是人打的字，多半是二进制协议：整行作废，到下一个换行之前什么也不输出
      if (len_ > 0)
      {
        Print("\r\n");
      }
      len_ = 0;
      junk_ = true;
      return;
    }
    if (c < 0x7F && len_ < kLineMax)
    {
      line_[len_++] = static_cast<char>(c);
      Print(reinterpret_cast<const char*>(&c), 1);
    }
  }

 private:
  static constexpr size_t kLineMax = 160;

  void Print(const char* text, size_t len = 0)
  {
    if (len == 0)
    {
      len = strlen(text);
    }
    write_(ctx_, text, len);
  }

  // 取下一个词：以空格分隔，可以用双引号括起带空格的词。返回 false 表示没有更多的词。
  static bool NextToken(char*& p, char*& token)
  {
    while (*p == ' ')
    {
      ++p;
    }
    if (*p == 0)
    {
      return false;
    }
    if (*p == '"')
    {
      token = ++p;
      while (*p != 0 && *p != '"')
      {
        ++p;
      }
    }
    else
    {
      token = p;
      while (*p != 0 && *p != ' ')
      {
        ++p;
      }
    }
    if (*p != 0)
    {
      *p++ = 0;
    }
    return true;
  }

  void Handle(char* line)
  {
    char* p = line;
    char* cmd = nullptr;
    if (!NextToken(p, cmd))
    {
      return;
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
    {
      Print("wifi set <热点> [<密码>]   保存热点参数并重启（有空格的用双引号括起来，不填密码为开放网络）\r\n"
            "wifi status               查看连接状态\r\n"
            "wifi clear                清除热点参数并重启\r\n");
      return;
    }
    char* sub = nullptr;
    if (strcmp(cmd, "wifi") != 0 || !NextToken(p, sub))
    {
      Print("未知命令，输入 help 查看用法\r\n");
      return;
    }

    if (strcmp(sub, "status") == 0)
    {
      PrintStatus();
    }
    else if (strcmp(sub, "set") == 0)
    {
      char* ssid = nullptr;
      char* password = nullptr;
      if (!NextToken(p, ssid))
      {
        Print("用法：wifi set <热点> [<密码>]\r\n");
        return;
      }
      if (!NextToken(p, password))
      {
        password = const_cast<char*>("");
      }
      const esp_err_t err = WifiStaSave(ssid, password);
      if (err == ESP_ERR_INVALID_ARG)
      {
        Print("热点名要 1 到 32 个字节，密码要 8 到 63 个字节（或不填）\r\n");
        return;
      }
      if (err != ESP_OK)
      {
        Print("保存失败\r\n");
        return;
      }
      Print("已保存，设备重启后连接\r\n");
      Restart();
    }
    else if (strcmp(sub, "clear") == 0)
    {
      if (WifiStaClear() != ESP_OK)
      {
        Print("清除失败\r\n");
        return;
      }
      Print("已清除，设备重启后不再连接 Wi-Fi\r\n");
      Restart();
    }
    else
    {
      Print("未知命令，输入 help 查看用法\r\n");
    }
  }

  void PrintStatus()
  {
    const WifiStatus st = WifiStaGetStatus();
    char buf[160];
    if (st.ssid[0] == 0)
    {
      Print("没有保存热点参数，用 wifi set 设置\r\n");
      return;
    }
    if (!st.started)
    {
      snprintf(buf, sizeof(buf), "热点：%s\r\nWi-Fi 没有启动（刚保存的参数要重启后才生效）\r\n", st.ssid);
    }
    else if (!st.connected)
    {
      snprintf(buf, sizeof(buf), "热点：%s\r\n还没有连上，正在重连\r\n", st.ssid);
    }
    else
    {
      snprintf(buf, sizeof(buf), "热点：%s\r\n已连接，IP %u.%u.%u.%u，信号 %d dBm\r\n", st.ssid, st.ip[0],
               st.ip[1], st.ip[2], st.ip[3], (int)st.rssi);
    }
    Print(buf);
  }

  void Restart()
  {
    vTaskDelay(pdMS_TO_TICKS(500));  // 等提示发出去
    esp_restart();
  }

  WriteFn write_;
  void* ctx_;
  char line_[kLineMax + 1] = {};
  size_t len_ = 0;
  bool junk_ = false;
  bool last_was_cr_ = false;
};

/// 独占一个串口的命令行：自己开一个任务读这个口。
template <typename UartT>
class WifiConsole
{
 public:
  explicit WifiConsole(UartT& uart) : uart_(uart), core_(&WifiConsole::Write, this) {}

  /// 在指定的核上启动命令行任务。
  void Start(int core)
  {
    xTaskCreatePinnedToCore(&WifiConsole::TaskEntry, "wifi_cli", 4096, this, 4, nullptr, core);
  }

 private:
  static void TaskEntry(void* arg) { static_cast<WifiConsole*>(arg)->Run(); }

  void Run()
  {
    for (;;)
    {
      // 队列里没有数据时等一会，避免占 CPU；USB 被关闭期间读端口里始终没有数据。
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
    auto* self = static_cast<WifiConsole*>(ctx);
    LibXR::Semaphore sem;
    LibXR::WriteOperation op(sem, 200);
    (void)self->uart_.Write(LibXR::ConstRawData{text, len}, op);
  }

  UartT& uart_;
  ConsoleCore core_;
};

}  // namespace XrDap
