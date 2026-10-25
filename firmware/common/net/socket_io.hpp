// SPDX-License-Identifier: Apache-2.0
#pragma once

// 阻塞套接字上的整块读写，作为 DapTcpSession 和 UsbipDevice 的字节流。
//
// Whole-buffer reads and writes on a blocking socket, the byte stream for DapTcpSession
// and UsbipDevice.

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "lwip/sockets.h"

namespace XrDap::Net
{

inline std::atomic<uint32_t> socket_errors{0U};
inline std::atomic<uint32_t> last_socket_error{0U};
inline std::atomic<uint32_t> connections{0U};
inline std::atomic<uint32_t> disconnections{0U};

inline bool ConfigureSendTimeout(int fd)
{
  const timeval timeout{5, 0};
  return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

/// 阻塞套接字字节流 / Byte stream over a blocking socket.
struct SocketStream
{
  /// 已连接的套接字 / Connected socket.
  int fd;

  /// 读满 n 字节；对方关闭或出错时返回 false / Read exactly n bytes; false when the peer
  /// closes or on error.
  bool ReadAll(uint8_t* buf, size_t n)
  {
    while (n)
    {
      const int r = recv(fd, buf, n, 0);
      if (r <= 0)
      {
        if (r < 0 && errno == EINTR)
        {
          continue;
        }
        if (r < 0)
        {
          last_socket_error.store(static_cast<uint32_t>(errno));
          socket_errors.fetch_add(1U);
        }
        return false;
      }
      buf += r;
      n -= static_cast<size_t>(r);
    }
    return true;
  }

  /// 写完 n 字节；出错时返回 false / Write all n bytes; false on error.
  bool WriteAll(const uint8_t* buf, size_t n)
  {
    while (n)
    {
      const int r = send(fd, buf, n, 0);
      if (r <= 0)
      {
        if (r < 0 && errno == EINTR)
        {
          continue;
        }
        last_socket_error.store(static_cast<uint32_t>(r < 0 ? errno : 0));
        socket_errors.fetch_add(1U);
        return false;
      }
      buf += r;
      n -= static_cast<size_t>(r);
    }
    return true;
  }
};

}  // namespace XrDap::Net
