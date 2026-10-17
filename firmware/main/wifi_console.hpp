// SPDX-License-Identifier: Apache-2.0
#pragma once

// 通过 USB 虚拟串口（CDC）配置 Wi-Fi 的文本命令行。
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

template <typename UartT>
class WifiConsole
{
 public:
  explicit WifiConsole(UartT& uart) : uart_(uart) {}

  /// 在指定的核上启动命令行任务。
  void Start(int core)
  {
    xTaskCreatePinnedToCore(&WifiConsole::TaskEntry, "wifi_cli", 4096, this, 4, nullptr, core);
  }

 private:
  static constexpr size_t kLineMax = 160;

  static void TaskEntry(void* arg) { static_cast<WifiConsole*>(arg)->Run(); }

  void Run()
  {
    char line[kLineMax + 1];
    size_t len = 0;
    bool last_was_cr = false;
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
      if (uart_.Read(LibXR::RawData{&c, 1}, op) != LibXR::ErrorCode::OK)
      {
        continue;
      }

      if (c == '\n' && last_was_cr)  // CR LF 只算一次换行
      {
        last_was_cr = false;
        continue;
      }
      last_was_cr = (c == '\r');

      if (c == '\r' || c == '\n')
      {
        Print("\r\n");
        line[len] = 0;
        Handle(line);
        len = 0;
        Print("> ");
      }
      else if (c == 0x08 || c == 0x7F)  // 退格
      {
        if (len > 0)
        {
          --len;
          Print("\b \b");
        }
      }
      else if (c >= 0x20 && c < 0x7F && len < kLineMax)
      {
        line[len++] = static_cast<char>(c);
        Print(reinterpret_cast<const char*>(&c), 1);
      }
    }
  }

  void Print(const char* text, size_t len = 0)
  {
    if (len == 0)
    {
      len = strlen(text);
    }
    LibXR::Semaphore sem;
    LibXR::WriteOperation op(sem, 200);
    (void)uart_.Write(LibXR::ConstRawData{text, len}, op);
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

  UartT& uart_;
};

}  // namespace XrDap
