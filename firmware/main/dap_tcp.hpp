#pragma once

// CMSIS-DAP over TCP: "DAP\0" + LE16 length + type (1 request, 2 response) + reserved.
// Same framing as OpenOCD's cmsis_dap_tcp.c and RMLink. One client at a time; every request
// frame is executed synchronously by the libxr DAP command processor and answered in order.

#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace XrDap
{

inline void (*g_dump_hook)(void*) = nullptr;
inline void* g_dump_arg = nullptr;

template <typename DapT>
class DapTcpServer
{
 public:
  DapTcpServer(DapT& dap, uint16_t port) : dap_(dap), port_(port) {}

  /// Called when a client connects / after it is gone (the connection is served by this task). The
  /// application uses them to hand the DAP instance over from USB to TCP and back.
  void SetHooks(void (*on_connect)(void*), void (*on_disconnect)(void*), void* ctx)
  {
    on_connect_ = on_connect;
    on_disconnect_ = on_disconnect;
    hook_ctx_ = ctx;
  }

  void Start(int core)
  {
    xTaskCreatePinnedToCore(&DapTcpServer::TaskEntry, "xrdap_tcp", 12288, this, 10, nullptr,
                            core);
  }

 private:
  static constexpr const char* TAG = "xrdap_tcp";
  static constexpr size_t kMaxPayload = 1024;

  // Per-command timing, printed when a client disconnects (debug aid for the SWD sweep).
  struct Stat
  {
    uint32_t n = 0;
    uint64_t us = 0;
    uint32_t bytes = 0;
  };
  Stat stats_[256];
  void Account(uint8_t cmd, uint16_t len, int64_t us)
  {
    stats_[cmd].n++;
    stats_[cmd].us += static_cast<uint64_t>(us);
    stats_[cmd].bytes += len;
  }
  void DumpStats()
  {
    for (int c = 0; c < 256; ++c)
    {
      if (stats_[c].n)
      {
        ESP_LOGI(TAG, "cmd 0x%02x: n=%u avg=%u us avg_req=%u B", c, (unsigned)stats_[c].n,
                 (unsigned)(stats_[c].us / stats_[c].n), (unsigned)(stats_[c].bytes / stats_[c].n));
        stats_[c] = Stat();
      }
    }
  }

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
    // Header and payload share one buffer so a response leaves in a single send().
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
        ESP_LOGW(TAG, "bad frame header, closing");
        return;
      }
      if (!ReadAll(fd, req, len))
      {
        return;
      }

      uint16_t out_len = 0;
      const int64_t t0 = esp_timer_get_time();
      (void)dap_.ExecuteExternal(req, len, out + 8, kMaxPayload, out_len);
      Account(req[0], len, esp_timer_get_time() - t0);
#if XRDAP_TCP_ACKLOG
      {
        // DAP_Transfer (0x05): resp = cmd, count, ack; DAP_TransferBlock (0x06): resp = cmd, count(2), ack
        static uint32_t logged = 0;
        const bool is_t = req[0] == 0x05 && out_len >= 3;
        const bool is_b = req[0] == 0x06 && out_len >= 4;
        if ((is_t || is_b) && logged < 300)
        {
          const uint8_t ack = is_t ? out[8 + 2] : out[8 + 3];
          const uint16_t cnt = is_t ? out[8 + 1] : static_cast<uint16_t>(out[8 + 1] | (out[8 + 2] << 8));
          const uint16_t want = is_t ? req[2] : static_cast<uint16_t>(req[2] | (req[3] << 8));
          if ((ack & 7) != 1 && (ack & 7) != 2)  // WAIT (2) is a normal answer while the flash is busy
          {
            ++logged;
            ESP_LOGW(TAG, "bad ack cmd=0x%02x ack=0x%02x count=%u of %u req_len=%u", req[0], ack, (unsigned)cnt,
                     (unsigned)want, (unsigned)len);
          }
        }
      }
#endif
      if (out_len == 0)
      {
        continue;  // e.g. DAP_TransferAbort has no response
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
    const int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(ls, 1) != 0)
    {
      ESP_LOGE(TAG, "bind/listen failed errno=%d", errno);
      vTaskDelete(nullptr);
      return;
    }
    ESP_LOGI(TAG, "CMSIS-DAP TCP listening on %u", (unsigned)port_);
    for (;;)
    {
      const int fd = accept(ls, nullptr, nullptr);
      if (fd < 0)
      {
        continue;
      }
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
      // A client that disappears without a FIN/RST (Wi-Fi drop, crashed or sleeping host) would
      // otherwise hold the only connection slot forever: probe after 5 s idle, every 2 s, give up
      // after 3 misses (a dead peer is dropped in about 11 s, an idle live one keeps answering).
      const int keep_idle = 5, keep_intvl = 2, keep_cnt = 3;
      setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_intvl, sizeof(keep_intvl));
      setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt));
      ESP_LOGI(TAG, "client connected");
      if (on_connect_)
      {
        on_connect_(hook_ctx_);
      }
      Serve(fd);
      DumpStats();
      if (g_dump_hook)
      {
        g_dump_hook(g_dump_arg);
      }
      close(fd);
      ESP_LOGI(TAG, "client closed");
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
