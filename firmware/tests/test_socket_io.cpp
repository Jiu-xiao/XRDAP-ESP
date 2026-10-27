// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <cstdio>
#include <vector>

#include "socket_io.hpp"

int main()
{
  using namespace XrDap::Net;
  int fd[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) return 1;
  if (!ConfigureSendTimeout(fd[0])) return 2;
  timeval timeout{};
  socklen_t length = sizeof(timeout);
  if (getsockopt(fd[0], SOL_SOCKET, SO_SNDTIMEO, &timeout, &length) != 0 ||
      timeout.tv_sec != 5)
    return 3;
  timeout = {0, 100000};  // Exercise the same timeout path without a ten-second test.
  setsockopt(fd[0], SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  const int small = 4096;
  setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
  SocketStream stream{fd[0]};
  const auto start = std::chrono::steady_clock::now();
  // Peer is alive but never reads. Keepalive alone cannot release this writer.
  const std::vector<uint8_t> bytes(1U << 20, 0x5A);
  const bool sent = stream.WriteAll(bytes.data(), bytes.size());
  const auto elapsed = std::chrono::steady_clock::now() - start;
  if (sent || socket_errors.load() != 1 || last_socket_error.load() == 0 ||
      elapsed > std::chrono::seconds(12))
    return 4;
  close(fd[0]);
  close(fd[1]);
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) return 5;
  stream.fd = fd[0];
  close(fd[1]);
  uint8_t byte = 0;
  // Drain the other direction's EOF; an ordinary FIN is not an I/O error.
  const auto failures = socket_errors.load();
  if (stream.ReadAll(&byte, 1) || socket_errors.load() != failures) return 6;
  close(fd[0]);
  std::puts("stalled writer is bounded; peer close returns without a read error");
}
