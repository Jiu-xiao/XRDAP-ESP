// SPDX-License-Identifier: Apache-2.0
#pragma once

// SwdEngine 的低速路径（swd_engine.hpp）和高速路径（swd_fast_s3.cpp）共用的 SWD 编码。
//
// SWD encoding shared by the SwdEngine slow path (swd_engine.hpp) and fast path
// (swd_fast_s3.cpp).

#include <cstdint>

#include "debug/swd.hpp"

namespace XrDap::SwdBits
{

/// 输出使能是 APB 寄存器写，十几个 CPU 周期后才生效；切到输出后等这么久再发第一位数据 /
/// The output enable is an APB register write that takes effect a dozen or so CPU
/// cycles later; wait this long after switching to output before the first data bit.
constexpr uint32_t kOutputEnableSettle = 16U;

/// 8 位请求包：Start、APnDP、RnW、A[2:3]、奇偶、Stop、Park，低位先发 / 8-bit request:
/// Start, APnDP, RnW, A[2:3], parity, Stop, Park, LSB first.
inline uint32_t MakeRequest(const LibXR::Debug::SwdProtocol::Request& req)
{
  const uint32_t apndp = (req.port == LibXR::Debug::SwdProtocol::Port::AP) ? 1U : 0U;
  const uint32_t rnw = req.rnw ? 1U : 0U;
  const uint32_t a2 = req.addr2b & 1U;
  const uint32_t a3 = (req.addr2b >> 1) & 1U;
  const uint32_t par = (apndp ^ rnw ^ a2 ^ a3) & 1U;
  return 1U | (apndp << 1) | (rnw << 2) | (a2 << 3) | (a3 << 4) | (par << 5) | (1U << 7);
}

/// 3 位 ACK（低位先收）转成枚举 / Decode the 3-bit ACK (LSB first).
inline LibXR::Debug::SwdProtocol::Ack DecodeAck(uint32_t bits)
{
  using Ack = LibXR::Debug::SwdProtocol::Ack;
  switch (bits)
  {
    case 1:
      return Ack::OK;
    case 2:
      return Ack::WAIT;
    case 4:
      return Ack::FAULT;
    case 0:
      return Ack::NO_ACK;
    default:
      return Ack::PROTOCOL;
  }
}

}  // namespace XrDap::SwdBits
