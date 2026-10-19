// SPDX-License-Identifier: Apache-2.0
#pragma once

// CMSIS-DAP over TCP。帧为 8 字节头加 DAP 数据：'D' 'A' 'P' 0，2 字节小端长度，类型
// （1 请求、2 响应），1 字节保留。与 OpenOCD 的 cmsis_dap_tcp.c 一致。可选地在另一个端口
// 提供 USB/IP 虚拟设备（见 usbip_device.hpp）。两个端口同一时间只服务一个客户端，请求按
// 顺序执行并应答。
//
// CMSIS-DAP over TCP. A frame is an 8-byte header plus DAP data: 'D' 'A' 'P' 0, a 2-byte
// little-endian length, a type (1 request, 2 response) and 1 reserved byte. Compatible
// with OpenOCD cmsis_dap_tcp.c. Optionally a USB/IP virtual device is served on a second
// port (see usbip_device.hpp). Only one client is served at a time across both ports;
// requests are executed and answered in order.

#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "probe_config.hpp"
#include "socket_io.hpp"
#include "usbip_device.hpp"

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
  DapTcpServer(DapT& dap, uint16_t port) : dap_(dap), port_(port), usbip_(dap) {}

  /// @brief 在另一个端口同时提供 USB/IP 虚拟设备，需在 Start() 之前调用 / Also serve the
  ///        USB/IP virtual device on a second port; call before Start().
  /// @param port USB/IP 端口，通常是 3240 / USB/IP port, normally 3240.
  /// @param product USB 产品名，例如 "XR-DAP-C3 CMSIS-DAP"，需一直有效 / USB product
  ///        name such as "XR-DAP-C3 CMSIS-DAP"; must stay valid.
  void EnableUsbip(uint16_t port, const char* product)
  {
    usbip_port_ = port;
    usbip_.SetProduct(product);
  }

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
    if (xTaskCreatePinnedToCore(&DapTcpServer::TaskEntry, "xrdap_tcp", kStackSize, this,
                                Config::kNetPriority, nullptr, core) != pdPASS)
    {
      ESP_LOGE(TAG, "创建服务任务失败，剩余堆 %u 字节",
               (unsigned)esp_get_free_heap_size());
    }
  }

 private:
  static constexpr const char* TAG = "xrdap_tcp";
  static constexpr size_t kMaxPayload = Config::kDapPacketSize;
  static constexpr uint32_t kStackSize = 12288;

  static void TaskEntry(void* arg) { static_cast<DapTcpServer*>(arg)->Run(); }

  void Serve(int fd)
  {
    for (;;)
    {
      uint8_t hdr[8];
      if (!Net::ReadAll(fd, hdr, sizeof(hdr)))
      {
        return;
      }
      const uint16_t len = static_cast<uint16_t>(hdr[4] | (hdr[5] << 8));
      if (memcmp(hdr, "DAP\0", 4) != 0 || hdr[6] != 1 || len == 0 || len > kMaxPayload)
      {
        ESP_LOGW(TAG, "帧头无效，关闭连接");
        return;
      }
      if (!Net::ReadAll(fd, req_, len))
      {
        return;
      }

      uint16_t out_len = 0;
      (void)dap_.ExecuteExternal(req_, len, out_ + 8, kMaxPayload, out_len);
      if (out_len == 0)
      {
        continue;  // 有的命令没有应答，例如 DAP_TransferAbort
      }
      memcpy(out_, "DAP\0", 4);
      out_[4] = static_cast<uint8_t>(out_len & 0xFF);
      out_[5] = static_cast<uint8_t>(out_len >> 8);
      out_[6] = 2;
      out_[7] = 0;
      if (!Net::WriteAll(fd, out_, 8u + out_len))
      {
        return;
      }
    }
  }

  static int Listen(uint16_t port)
  {
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(fd, 1) != 0)
    {
      ESP_LOGE(TAG, "端口 %u bind/listen 失败，errno=%d", (unsigned)port, errno);
      close(fd);
      return -1;
    }
    ESP_LOGI(TAG, "TCP 端口 %u 已监听", (unsigned)port);
    return fd;
  }

  static void Configure(int fd)
  {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    // 客户端不发 FIN 就消失（Wi-Fi 掉线、主机死机）时，没有 keepalive 连接会一直占着
    // 唯一的位置。空闲 5 秒开始探测，每 2 秒一次，3 次无应答就断开（约 11 秒）。
    const int keep_idle = 5, keep_interval = 2, keep_count = 3;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count));
  }

  void Connected()
  {
    ESP_LOGI(TAG, "客户端已连接");
    if (on_connect_)
    {
      on_connect_(hook_ctx_);
    }
  }

  void Disconnected()
  {
    ESP_LOGI(TAG, "客户端已断开");
    if (on_disconnect_)
    {
      on_disconnect_(hook_ctx_);
    }
  }

  void Run()
  {
    const int dap_fd = Listen(port_);
    const int usbip_fd = usbip_port_ ? Listen(usbip_port_) : -1;
    if (dap_fd < 0 && usbip_fd < 0)
    {
      vTaskDelete(nullptr);
      return;
    }
    for (;;)
    {
      fd_set ready;
      FD_ZERO(&ready);
      if (dap_fd >= 0)
      {
        FD_SET(dap_fd, &ready);
      }
      if (usbip_fd >= 0)
      {
        FD_SET(usbip_fd, &ready);
      }
      const int max_fd = dap_fd > usbip_fd ? dap_fd : usbip_fd;
      if (select(max_fd + 1, &ready, nullptr, nullptr, nullptr) <= 0)
      {
        continue;
      }
      const bool is_usbip = usbip_fd >= 0 && FD_ISSET(usbip_fd, &ready);
      const int fd = accept(is_usbip ? usbip_fd : dap_fd, nullptr, nullptr);
      if (fd < 0)
      {
        continue;
      }
      Configure(fd);
      if (!is_usbip)
      {
        Connected();
        Serve(fd);
        close(fd);
        Disconnected();
      }
      else if (usbip_.Handshake(fd))
      {
        // 设备列表查询（usbip list -r）不算连上，不打断别的传输 / A device-list query
        // (usbip list -r) is not a connection and leaves the other transports alone.
        Connected();
        usbip_.ServeUrbs(fd);
        close(fd);
        Disconnected();
      }
      else
      {
        close(fd);
      }
    }
  }

  DapT& dap_;
  uint16_t port_;
  uint16_t usbip_port_ = 0;
  uint8_t req_[kMaxPayload] = {};
  // 帧头和数据放在同一块缓冲里，一个响应只调用一次 send()
  uint8_t out_[8 + kMaxPayload] = {};
  UsbipDevice<DapT> usbip_;
  void (*on_connect_)(void*) = nullptr;
  void (*on_disconnect_)(void*) = nullptr;
  void* hook_ctx_ = nullptr;
};

}  // namespace XrDap
