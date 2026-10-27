// SPDX-License-Identifier: Apache-2.0
#pragma once

// CMSIS-DAP over TCP 服务（帧格式见 dap_tcp_session.hpp）。同一时间只服务一个客户端，
// 请求按顺序执行并应答。这里只做套接字：监听、接入、保活，协议在会话类里。探针不带 USB
// 设备模型：要当 USB 探针用，电脑上运行 `xrdap share usbip`。
//
// CMSIS-DAP over TCP server (frame format in dap_tcp_session.hpp). Only one client is
// served at a time; requests are executed and answered in order. Only the sockets live
// here (listen, accept, keepalive); the protocol is in the session class. Requests run
// through XRDAP-Core's executor as Transport::TCP: while another transport owns the
// probe they are answered busy, and a closed connection ends the TCP session. The probe
// carries no USB device model: to use it as a USB probe, run `xrdap share usbip` on a
// computer.

#include <atomic>
#include <cstdint>

#include "dap_tcp_session.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "probe_config.hpp"
#include "socket_io.hpp"
#include "xrdap/session/session_owner.hpp"

namespace XrDap
{

/// DAP over TCP 服务 / DAP over TCP server.
/// @tparam ExecT XRDAP-Core 的 Executor<DapT> / XRDAP-Core's Executor<DapT>.
template <typename ExecT>
class DapTcpServer
{
 public:
  /// @brief 构造服务对象，不创建任务 / Construct the server without creating a task.
  /// @param exec 命令执行器，生命周期需长于本对象 / Command executor; must outlive this
  ///        object.
  /// @param port 监听端口 / Listening port.
  /// @param client 有客户端时置 true，可为空 / Set to true while a client is connected;
  ///        may be null.
  DapTcpServer(ExecT& exec, uint16_t port, std::atomic<bool>* client)
      : exec_{exec}, port_(port), client_(client), session_(exec_)
  {
  }

  /// @brief 在指定的核上启动服务任务 / Start the server task pinned to a core.
  /// @param core 服务任务所在的核 / Core that runs the server task.
  /// @return 任务创建失败（内存不足）时返回 false / False when the task could not be
  ///         created (out of memory).
  /// @note 只调用一次。监听失败时任务自行删除 / Call once. The task deletes itself if
  ///       it cannot listen.
  bool Start(int core)
  {
    return xTaskCreatePinnedToCore(&DapTcpServer::TaskEntry, "xrdap_tcp",
                                   Config::kNetStack, this, Config::kNetPriority, nullptr,
                                   core) == pdPASS;
  }

 private:
  /// 会话类用的执行入口：请求以 Transport::TCP 经执行器执行 / Execution entry for the
  /// session classes: requests run through the executor as Transport::TCP.
  struct TcpExec
  {
    ExecT& exec;

    int ExecuteExternal(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                        uint16_t& out)
    {
      exec.Execute(Transport::TCP, req, len, resp, cap, out);
      return 0;
    }
  };

  static void TaskEntry(void* arg) { static_cast<DapTcpServer*>(arg)->Run(); }

  static int Listen(uint16_t port)
  {
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0)
    {
      return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(fd, 1) != 0)
    {
      close(fd);
      return -1;
    }
    return fd;
  }

  static bool Configure(int fd)
  {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    // 客户端不发 FIN 就消失（Wi-Fi 掉线、主机死机）时，没有 keepalive 连接会一直占着
    // 唯一的位置。空闲 5 秒开始探测，每 2 秒一次，3 次无应答就断开（约 11 秒）。
    // A client that vanishes without a FIN (Wi-Fi drop, host crash) would hold the only
    // slot forever without keepalive. Probe after 5 s idle, every 2 s, drop after 3
    // misses (about 11 s).
    const int keep_idle = 5, keep_interval = 2, keep_count = 3;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count));
    // Keepalive cannot detect a live peer whose application stopped reading. Bound
    // a blocked response write as well; idle debugging connections remain valid.
    return Net::ConfigureSendTimeout(fd);
  }

  void Connected()
  {
    Net::connections.fetch_add(1U);
    if (client_ != nullptr)
    {
      client_->store(true);
    }
  }

  // 连接关了，TCP 是主人时它的会话结束，采样随之停止 / The connection closed; if TCP owns
  // the probe its session ends and sampling stops with it.
  void Disconnected()
  {
    exec_.exec.EndSession(Transport::TCP);
    Net::disconnections.fetch_add(1U);
    if (client_ != nullptr)
    {
      client_->store(false);
    }
  }

  void Run()
  {
    const int dap_fd = Listen(port_);
    if (dap_fd < 0)
    {
      vTaskDelete(nullptr);
      return;
    }
    for (;;)
    {
      const int fd = accept(dap_fd, nullptr, nullptr);
      if (fd < 0)
      {
        continue;
      }
      if (!Configure(fd))
      {
        close(fd);
        continue;
      }
      Net::SocketStream io{fd};
      Connected();
      session_.Serve(io);
      close(fd);
      Disconnected();
    }
  }

  TcpExec exec_;
  uint16_t port_;
  std::atomic<bool>* client_;
  DapTcpSession<TcpExec, Config::kDapPacketSize> session_;
};

}  // namespace XrDap
