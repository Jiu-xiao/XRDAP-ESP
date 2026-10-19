// SPDX-License-Identifier: Apache-2.0
#pragma once

// CMSIS-DAP over TCP。帧为 8 字节头加 DAP 数据：'D' 'A' 'P' 0，2 字节小端长度，类型
// （1 请求、2 响应），1 字节保留。与 OpenOCD 的 cmsis_dap_tcp.c 一致。同一时间只服务
// 一个客户端，请求按顺序执行并应答。
//
// CMSIS-DAP over TCP. A frame is an 8-byte header plus DAP data: 'D' 'A' 'P' 0, a 2-byte
// little-endian length, a type (1 request, 2 response) and 1 reserved byte. Compatible
// with OpenOCD cmsis_dap_tcp.c. Only one client is served at a time; requests are
// executed and answered in order.

#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace XrDap
{

/// DAP over TCP 服务 / DAP over TCP server.
/// @tparam DapT 需提供 ExecuteExternal() / Must provide ExecuteExternal().
template <typename DapT>
class DapTcpServer
{
 public:
  /// @brief 构造服务对象，不创建任务 / Construct the server without creating a task.
  /// @param dap 执行请求的 DAP 实例，生命周期需长于本对象 / DAP instance that runs the
  ///        requests; must outlive this object.
  /// @param port 监听端口 / Listening port.
  DapTcpServer(DapT& dap, uint16_t port) : dap_(dap), port_(port) {}

  /// @brief 设置连接交接回调，需在 Start() 之前调用 / Set the handover hooks; call before
  ///        Start().
  /// @param on_connect 客户端连上时在服务任务中调用 / Called in the server task when a
  ///        client connects.
  /// @param on_disconnect 客户端断开后在服务任务中调用 / Called in the server task after
  ///        the client is gone.
  /// @param ctx 传给两个回调的参数 / Argument passed to both hooks.
  /// @note USB 和 TCP 共用同一个 DAP 实例，应用用这两个回调在传输之间交接 / USB and TCP
  ///       share one DAP instance; the hooks let the application hand it over.
  void SetHooks(void (*on_connect)(void*), void (*on_disconnect)(void*), void* ctx)
  {
    on_connect_ = on_connect;
    on_disconnect_ = on_disconnect;
    hook_ctx_ = ctx;
  }

  /// @brief 在指定的核上启动服务任务（不会返回，bind/listen 失败时自行删除）/ Start the
  ///        server task pinned to a core (never returns; deletes itself if bind/listen
  ///        fails).
  /// @param core 服务任务所在的核 / Core that runs the server task.
  void Start(int core)
  {
    xTaskCreatePinnedToCore(&DapTcpServer::TaskEntry, "xrdap_tcp", 12288, this, 10,
                            nullptr, core);
  }

 private:
  static constexpr const char* TAG = "xrdap_tcp";
  static constexpr size_t kMaxPayload = 1024;

  static void TaskEntry(void* arg) { static_cast<DapTcpServer*>(arg)->Run(); }

  static bool ReadAll(int fd, uint8_t* buf, size_t n)
  {
    while (n)
    {
      const int r = recv(fd, buf, n, 0);
      if (r <= 0)
      {
        return false;
      }
      buf += r;
      n -= static_cast<size_t>(r);
    }
    return true;
  }

  static bool WriteAll(int fd, const uint8_t* buf, size_t n)
  {
    while (n)
    {
      const int r = send(fd, buf, n, 0);
      if (r <= 0)
      {
        return false;
      }
      buf += r;
      n -= static_cast<size_t>(r);
    }
    return true;
  }

  void Serve(int fd)
  {
    // 帧头和数据放在同一块缓冲里，一个响应只调用一次 send()。
    static uint8_t req[kMaxPayload];
    static uint8_t out[8 + kMaxPayload];
    for (;;)
    {
      uint8_t hdr[8];
      if (!ReadAll(fd, hdr, sizeof(hdr)))
      {
        return;
      }
      const uint16_t len = static_cast<uint16_t>(hdr[4] | (hdr[5] << 8));
      if (memcmp(hdr, "DAP\0", 4) != 0 || hdr[6] != 1 || len == 0 || len > kMaxPayload)
      {
        ESP_LOGW(TAG, "帧头无效，关闭连接");
        return;
      }
      if (!ReadAll(fd, req, len))
      {
        return;
      }

      uint16_t out_len = 0;
      (void)dap_.ExecuteExternal(req, len, out + 8, kMaxPayload, out_len);
      if (out_len == 0)
      {
        continue;  // 有的命令没有应答，例如 DAP_TransferAbort
      }
      memcpy(out, "DAP\0", 4);
      out[4] = static_cast<uint8_t>(out_len & 0xFF);
      out[5] = static_cast<uint8_t>(out_len >> 8);
      out[6] = 2;
      out[7] = 0;
      if (!WriteAll(fd, out, 8u + out_len))
      {
        return;
      }
    }
  }

  void Run()
  {
    const int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(listen_fd, 1) != 0)
    {
      ESP_LOGE(TAG, "bind/listen 失败，errno=%d", errno);
      vTaskDelete(nullptr);
      return;
    }
    ESP_LOGI(TAG, "TCP 端口 %u 已监听", (unsigned)port_);
    for (;;)
    {
      const int fd = accept(listen_fd, nullptr, nullptr);
      if (fd < 0)
      {
        continue;
      }
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
      // 客户端不发 FIN 就消失（Wi-Fi 掉线、主机死机）时，没有 keepalive 连接会一直占着
      // 唯一的位置。空闲 5 秒开始探测，每 2 秒一次，3 次无应答就断开（约 11 秒）。
      const int keep_idle = 5, keep_interval = 2, keep_count = 3;
      setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval));
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count));

      ESP_LOGI(TAG, "客户端已连接");
      if (on_connect_)
      {
        on_connect_(hook_ctx_);
      }
      Serve(fd);
      close(fd);
      ESP_LOGI(TAG, "客户端已断开");
      if (on_disconnect_)
      {
        on_disconnect_(hook_ctx_);
      }
    }
  }

  DapT& dap_;
  uint16_t port_;
  void (*on_connect_)(void*) = nullptr;
  void (*on_disconnect_)(void*) = nullptr;
  void* hook_ctx_ = nullptr;
};

}  // namespace XrDap
