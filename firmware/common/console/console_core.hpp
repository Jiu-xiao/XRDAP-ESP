// SPDX-License-Identifier: Apache-2.0
#pragma once

// 配置 Wi-Fi 的文本命令行。ConsoleCore 按字节处理输入（行编辑、执行命令），输出交给回调，
// 设置经 WifiSettings 读写，不依赖
// ESP-IDF；UartConsole（uart_console.hpp）让它独占一个串口（S3 的 USB 虚拟串口、C3 的
// UART0）；SerialMux （serial_mux.hpp）让它与串口 DAP 链路共用一个口。
//   help                         列出命令
//   wifi set <热点> [<密码>]     保存热点参数后重启。热点或密码含空格时用双引号括起来；
//                                不填密码表示开放网络
//   wifi status                  查看热点、是否连接、IP、信号强度
//   wifi clear                   清除热点参数后重启
// 串口终端：波特率任意，换行用 CR 或 LF 均可。输入会回显，密码也会显示在你自己的终端上。
//
// Text console for configuring Wi-Fi. ConsoleCore processes input byte by byte (line
// editing, command execution), sends output through a callback and reaches the settings
// through WifiSettings, without ESP-IDF; UartConsole (uart_console.hpp) gives it a whole
// serial port (S3 USB CDC, C3 UART0); SerialMux (serial_mux.hpp) shares one port
// with the serial DAP link.
//   help                         list commands
//   wifi set <ssid> [<password>] save credentials and restart. Quote values containing
//                                spaces; omit the password for an open network
//   wifi status                  show SSID, connection state, IP, RSSI
//   wifi clear                   clear credentials and restart
// Serial terminal: any baud rate, CR or LF line ending. Input is echoed, including the
// password, on your own terminal.

#include <cstddef>
#include <cstdint>

#include "wifi_settings.hpp"

namespace XrDap
{

/// 命令行核心，一次喂一个字节 / Console core fed one byte at a time.
/// 只在收到一行合法命令并回车后才输出，与二进制协议共用一个口时不会往里插字：行内出现
/// 控制字符（DAP 帧头必带 0x00）或以 'Z'（帧起始字节）开头时整行丢弃，不回显、不提示。
/// Prints only after a valid line ends with Enter, so it never injects bytes into a
/// shared binary protocol: a line containing control characters (a DAP header always has
/// 0x00) or starting with 'Z' (frame start byte) is dropped silently, without echo or
/// prompt.
/// @note 非线程安全，只能由同一个任务调用 / Not thread-safe; use from a single task.
class ConsoleCore
{
 public:
  /// 输出回调，由命令行所在任务同步调用 / Output callback, invoked synchronously from the
  /// task that feeds the console.
  using WriteFn = void (*)(void* ctx, const char* text, size_t len);

  /// @param settings Wi-Fi 设置，生命周期需长于本对象 / Wi-Fi settings; must outlive this
  ///        object.
  /// @param write 输出回调 / Output callback.
  /// @param ctx 传给 write 的参数 / Argument passed to write.
  ConsoleCore(WifiSettings& settings, WriteFn write, void* ctx)
      : settings_(settings), write_(write), ctx_(ctx)
  {
  }

  /// 丢弃正在输入的一行，不产生输出 / Discard the line being typed without any output.
  void Reset();

  /// @brief 处理一个输入字节：行编辑，遇到换行则执行命令 / Handle one input byte: edit
  ///        the line and run the command on a line ending.
  /// @note 保存或清除热点参数的命令约 0.5 秒后重启设备 / Commands that save or clear the
  ///       credentials restart the device after about 0.5 s.
  void OnByte(uint8_t c);

 private:
  static constexpr size_t kLineMax = 160;

  void Print(const char* text, size_t len = 0);
  void Handle(char* line);
  void PrintStatus();

  WifiSettings& settings_;
  WriteFn write_;
  void* ctx_;
  char line_[kLineMax + 1] = {};
  size_t len_ = 0;
  bool junk_ = false;
  bool last_was_cr_ = false;
};

}  // namespace XrDap
