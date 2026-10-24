// SPDX-License-Identifier: Apache-2.0
#include "console_core.hpp"

#include <cstdio>
#include <cstring>

namespace XrDap
{
namespace
{
// 取下一个词：以空格分隔，可以用双引号括起带空格的词。返回 false 表示没有更多的词。
bool NextToken(char*& p, char*& token)
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

const char* EraseSourceText(EraseSource source)
{
  switch (source)
  {
    case EraseSource::BUTTON:
      return "按键";
    case EraseSource::CONSOLE:
      return "命令行 wifi clear";
    case EraseSource::VENDOR_COMMAND:
      return "DAP 命令 0x86";
    case EraseSource::STORAGE_REPAIR:
      return "NVS 损坏后擦除";
    default:
      return "无";
  }
}

const char* ResetReasonText(ResetReason reason)
{
  switch (reason)
  {
    case ResetReason::POWER_ON:
      return "上电";
    case ResetReason::SOFTWARE:
      return "软件重启";
    case ResetReason::WATCHDOG:
      return "看门狗";
    case ResetReason::PANIC:
      return "异常";
    case ResetReason::BROWNOUT:
      return "欠压";
    case ResetReason::OTHER:
      return "其他";
    default:
      return "未知";
  }
}
}  // namespace

void ConsoleCore::Reset()
{
  len_ = 0;
  junk_ = false;
  last_was_cr_ = false;
}

void ConsoleCore::OnByte(uint8_t c)
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

void ConsoleCore::Print(const char* text, size_t len)
{
  if (len == 0)
  {
    len = strlen(text);
  }
  write_(ctx_, text, len);
}

void ConsoleCore::Handle(char* line)
{
  char* p = line;
  char* cmd = nullptr;
  if (!NextToken(p, cmd))
  {
    return;
  }
  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
  {
    Print(
        "wifi set <热点> [<密码>]   "
        "保存热点参数并重启（有空格的用双引号括起来，不填密码为开放网络）\r\n"
        "wifi status               查看连接状态\r\n"
        "wifi clear                清除热点参数并重启\r\n"
        "heap                      查看空闲堆和各任务栈的剩余量\r\n");
    return;
  }
  if (strcmp(cmd, "heap") == 0 && heap_ != nullptr)
  {
    heap_->Write(write_, ctx_);
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
    const WifiSettings::SaveResult result = settings_.Save(ssid, password);
    if (result == WifiSettings::SaveResult::INVALID)
    {
      Print("热点名要 1 到 32 个字节，密码要 8 到 63 个字节（或不填）\r\n");
      return;
    }
    if (result != WifiSettings::SaveResult::OK)
    {
      Print("保存失败\r\n");
      return;
    }
    Print("已保存，设备重启后连接\r\n");
    settings_.RestartLater();
  }
  else if (strcmp(sub, "clear") == 0)
  {
    if (!settings_.Clear(EraseSource::CONSOLE))
    {
      Print("清除失败\r\n");
      return;
    }
    Print("已清除，设备重启后不再连接 Wi-Fi\r\n");
    settings_.RestartLater();
  }
  else
  {
    Print("未知命令，输入 help 查看用法\r\n");
  }
}

void ConsoleCore::PrintStatus()
{
  const WifiStatus st = settings_.Status();
  char buf[160];
  if (st.storage_repaired)
  {
    Print("NVS 损坏，本次启动时已擦除重建\r\n");
  }
  snprintf(buf, sizeof(buf), "上次清除：%s\r\n上次复位：%s\r\n",
           EraseSourceText(st.last_erase), ResetReasonText(st.reset_reason));
  Print(buf);
  if (st.watchdog_task[0][0] != 0)
  {
    if (st.watchdog_task[1][0] != 0)
    {
      snprintf(buf, sizeof(buf), "看门狗超时时在运行：核 0 %.15s，核 1 %.15s\r\n",
               st.watchdog_task[0], st.watchdog_task[1]);
    }
    else
    {
      snprintf(buf, sizeof(buf), "看门狗超时时在运行：%.15s\r\n", st.watchdog_task[0]);
    }
    Print(buf);
  }
  if (st.ssid[0] == 0)
  {
    Print("没有保存热点参数，用 wifi set 设置\r\n");
    return;
  }
  if (!st.started)
  {
    snprintf(buf, sizeof(buf),
             "热点：%s\r\nWi-Fi 没有启动（刚保存的参数要重启后才生效）\r\n", st.ssid);
  }
  else if (!st.connected)
  {
    snprintf(buf, sizeof(buf), "热点：%s\r\n还没有连上，正在重连\r\n", st.ssid);
  }
  else
  {
    snprintf(buf, sizeof(buf), "热点：%s\r\n已连接，IP %u.%u.%u.%u，信号 %d dBm\r\n",
             st.ssid, st.ip[0], st.ip[1], st.ip[2], st.ip[3], (int)st.rssi);
  }
  Print(buf);
}

}  // namespace XrDap
