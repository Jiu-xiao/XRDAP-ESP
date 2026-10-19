// SPDX-License-Identifier: Apache-2.0
#pragma once

// 阻塞套接字上的整块读写。
//
// Whole-buffer reads and writes on blocking sockets.

#include <cstddef>
#include <cstdint>

#include "lwip/sockets.h"

namespace XrDap::Net
{

/// 读满 n 字节；对方关闭或出错时返回 false / Read exactly n bytes; false when the peer
/// closes or on error.
inline bool ReadAll(int fd, uint8_t* buf, size_t n)
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

/// 写完 n 字节；出错时返回 false / Write all n bytes; false on error.
inline bool WriteAll(int fd, const uint8_t* buf, size_t n)
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

}  // namespace XrDap::Net
