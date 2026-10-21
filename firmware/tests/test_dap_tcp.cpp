// SPDX-License-Identifier: Apache-2.0
// CMSIS-DAP over TCP：判据是 OpenOCD src/jtag/drivers/cmsis_dap_tcp.c 的帧格式（签名
// "DAP\0"、16 位小端长度、类型 1 请求 / 2 响应、1 字节保留）/ CMSIS-DAP over TCP: the
// oracle is the frame format of OpenOCD src/jtag/drivers/cmsis_dap_tcp.c (signature
// "DAP\0", 16-bit little-endian length, type 1 request / 2 response, 1 reserved byte).
#include "dap_tcp_session.hpp"
#include "fakes.hpp"

namespace
{
using Test::Bytes;
using Test::FakeDap;
using Test::MemoryStream;

constexpr uint16_t kMax = 1024;

Bytes Frame(uint8_t type, const Bytes& data)
{
  Bytes f = {'D',
             'A',
             'P',
             0,
             static_cast<uint8_t>(data.size()),
             static_cast<uint8_t>(data.size() >> 8),
             type,
             0};
  Test::Append(f, data);
  return f;
}

Bytes Serve(FakeDap& dap, const Bytes& input)
{
  XrDap::DapTcpSession<FakeDap, kMax> session(dap);
  MemoryStream io;
  io.in = input;
  session.Serve(io);
  return io.out;
}
}  // namespace

TEST(tcp_request_gets_response_frame)
{
  FakeDap dap;
  CHECK_BYTES(Serve(dap, Frame(1, {0x00, 0xFE})), Frame(2, {0x00, 0xFE}));
}

TEST(tcp_requests_answered_in_order)
{
  FakeDap dap;
  Bytes in = Frame(1, {0x00, 0x01});
  Test::Append(in, Frame(1, {0x00, 0x02}));
  Bytes want = Frame(2, {0x00, 0x01});
  Test::Append(want, Frame(2, {0x00, 0x02}));
  CHECK_BYTES(Serve(dap, in), want);
}

TEST(tcp_command_without_response_sends_nothing)
{
  FakeDap dap;
  Bytes in = Frame(1, {0x07});
  Test::Append(in, Frame(1, {0x00, 0x01}));
  CHECK_BYTES(Serve(dap, in), Frame(2, {0x00, 0x01}));
  CHECK(dap.requests.size() == 2);
}

TEST(tcp_length_is_little_endian)
{
  FakeDap dap;
  Bytes data(0x0123, 0x5A);
  data[0] = 0x00;
  const Bytes out = Serve(dap, Frame(1, data));
  CHECK(out.size() == 8 + 0x0123);
  CHECK(out[4] == 0x23 && out[5] == 0x01);
}

TEST(tcp_invalid_headers_close_without_executing)
{
  const Bytes bad[] = {
      {'D', 'A', 'X', 0, 2, 0, 1, 0, 0x00, 0x01},  // 签名 / signature
      Frame(2, {0x00, 0x01}),                      // 不是请求 / not a request
      Frame(1, {}),                                // 长度 0 / length 0
  };
  for (const Bytes& in : bad)
  {
    FakeDap dap;
    CHECK(Serve(dap, in).empty());
    CHECK(dap.requests.empty());
  }
}

TEST(tcp_oversized_request_closes)
{
  FakeDap dap;
  CHECK(Serve(dap, Frame(1, Bytes(kMax + 1, 0))).empty());
  CHECK(dap.requests.empty());
}

TEST(tcp_truncated_request_closes)
{
  FakeDap dap;
  Bytes in = Frame(1, {0x00, 0x01, 0x02});
  in.pop_back();
  CHECK(Serve(dap, in).empty());
  CHECK(dap.requests.empty());
}
