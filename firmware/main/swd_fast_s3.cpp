// SPDX-License-Identifier: Apache-2.0
// SwdEngine 的高速路径（只在 ESP32-S3 上）：汇编写死每个半周期的指令，高低两个半周期指令数相同。
// 每一档展开后代码量较大，放在 flash 里（linker.lf 只把 main.cpp 放进 IRAM）。指令缓存未命中时
// 只会把某一个半周期拉长，SWD 是同步协议，不影响正确性。

#include "swd_engine.hpp"

#if XRDAP_SWD_FAST_PATH

namespace XrDap
{
namespace
{
using Request = LibXR::Debug::SwdProtocol::Request;
using Response = LibXR::Debug::SwdProtocol::Response;
using Ack = LibXR::Debug::SwdProtocol::Ack;

// 输出使能是 APB 寄存器写，十几个 CPU 周期后才生效，切到输出后等这么久再发第一位数据
constexpr uint32_t kOutputEnableSettle = 16U;

inline uint32_t Now() { return esp_cpu_get_cycle_count(); }

inline void Spin(uint32_t cycles)
{
  const uint32_t t0 = Now();
  while ((Now() - t0) < cycles)
  {
  }
}

inline void Out(uint32_t value) { dedic_gpio_cpu_ll_write_all(value); }

/// 输出 COUNT 位：lo[i] 是第 i 位在 SWCLK 低时的输出寄存器值，高电平时的值 = lo[i] | cm。
/// 低半周期：写沿、算高电平值、N 个 nop；高半周期：写沿、取下一位、N 个 nop。两段都是 N + 2 条指令。
template <int N, int COUNT>
inline __attribute__((always_inline)) void WriteFast(const uint32_t* lo, uint32_t cm)
{
  uint32_t a;
  uint32_t h;
  asm volatile(
      "l32i %[a], %[p], 0\n"
      ".set xr_i, 1\n"
      ".rept %c[cnt]\n"
      "wur.gpio_out %[a]\n"
      "or %[h], %[a], %[cm]\n"
      ".rept %c[n]\n nop\n .endr\n"
      "wur.gpio_out %[h]\n"
      ".if xr_i < %c[cnt]\n"
      "l32i %[a], %[p], 4*xr_i\n"
      ".else\n"
      "nop\n"
      ".endif\n"
      ".rept %c[n]\n nop\n .endr\n"
      ".set xr_i, xr_i + 1\n"
      ".endr\n"
      : [a] "=&r"(a), [h] "=&r"(h)
      : [p] "r"(lo), [cm] "r"(cm), [cnt] "i"(COUNT), [n] "i"(N)
      : "memory");
}

/// 读 COUNT 位，原始输入寄存器值存进 raw[]。
/// 低半周期：写沿、N 个 nop、采样；高半周期：写沿、存数、N 个 nop。两段都是 N + 2 条指令，采样紧挨着上升沿。
template <int N, int COUNT>
inline __attribute__((always_inline)) void ReadFast(uint32_t* raw, uint32_t vlo, uint32_t vhi)
{
  uint32_t x;
  asm volatile(
      ".set xr_i, 0\n"
      ".rept %c[cnt]\n"
      "wur.gpio_out %[lo]\n"
      ".rept %c[n]\n nop\n .endr\n"
      "ee.get_gpio_in %[x]\n"
      "wur.gpio_out %[hi]\n"
      "s32i %[x], %[p], 4*xr_i\n"
      ".rept %c[n]\n nop\n .endr\n"
      ".set xr_i, xr_i + 1\n"
      ".endr\n"
      : [x] "=&r"(x)
      : [p] "r"(raw), [lo] "r"(vlo), [hi] "r"(vhi), [cnt] "i"(COUNT), [n] "i"(N)
      : "memory");
}

inline uint32_t Pack(const uint32_t* raw, uint32_t count, uint32_t im)
{
  uint32_t v = 0U;
  for (uint32_t i = 0; i < count; ++i)
  {
    v |= ((raw[i] & im) != 0U ? 1U : 0U) << i;
  }
  return v;
}

inline uint32_t MakeRequest(const Request& req)
{
  const uint32_t apndp = (req.port == LibXR::Debug::SwdProtocol::Port::AP) ? 1U : 0U;
  const uint32_t rnw = req.rnw ? 1U : 0U;
  const uint32_t a2 = req.addr2b & 1U;
  const uint32_t a3 = (req.addr2b >> 1) & 1U;
  const uint32_t par = (apndp ^ rnw ^ a2 ^ a3) & 1U;
  return 1U | (apndp << 1) | (rnw << 2) | (a2 << 3) | (a3 << 4) | (par << 5) | (1U << 7);
}

inline Ack DecodeAck(uint32_t bits)
{
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

template <int N>
LibXR::ErrorCode Xfer(const SwdFastPins& pins, const Request& req, Response& resp)
{
  const uint32_t cm = pins.cm;
  const uint32_t dm = pins.dm;
  uint32_t bits[33];
  uint32_t raw[34];
  resp.ack = Ack::PROTOCOL;
  resp.rdata = 0U;
  resp.parity_ok = true;

  const uint32_t rq = MakeRequest(req);
  for (uint32_t i = 0; i < 8U; ++i)
  {
    bits[i] = ((rq >> i) & 1U) ? dm : 0U;
  }
  pins.swdio->SetOutputEnable(true);
  WriteFast<N, 8>(bits, cm);

  // 释放 SWDIO：换向时钟 + 3 位 ACK
  pins.swdio->SetOutputEnable(false);
  ReadFast<N, 4>(raw, dm, dm | cm);
  resp.ack = DecodeAck(Pack(raw + 1, 3U, pins.im));

  if (resp.ack != Ack::OK)
  {
    ReadFast<N, 1>(raw, dm, dm | cm);  // 换向时钟，不进数据阶段
    Out(dm);
    pins.swdio->SetOutputEnable(true);
    return LibXR::ErrorCode::OK;
  }

  if (req.rnw)
  {
    // 32 位数据 + 校验位 + 换向时钟
    ReadFast<N, 34>(raw, dm, dm | cm);
    Out(dm);
    pins.swdio->SetOutputEnable(true);
    const uint32_t data = Pack(raw, 32U, pins.im);
    const uint32_t parity = (raw[32] & pins.im) != 0U ? 1U : 0U;
    resp.rdata = data;
    resp.parity_ok = (parity == static_cast<uint32_t>(__builtin_parity(data)));
  }
  else
  {
    const uint32_t data = req.wdata;
    for (uint32_t i = 0; i < 32U; ++i)
    {
      bits[i] = ((data >> i) & 1U) ? dm : 0U;
    }
    bits[32] = __builtin_parity(data) ? dm : 0U;
    ReadFast<N, 1>(raw, dm, dm | cm);  // 换向时钟，之后主机驱动 SWDIO
    pins.swdio->SetOutputEnable(true);
    Spin(kOutputEnableSettle);
    WriteFast<N, 33>(bits, cm);
    Out(dm);
  }
  return LibXR::ErrorCode::OK;
}

/// 用这一档读 32 位（64 个半周期），取 4 次里最少的一次。调用前 SWDIO 应已释放。
template <int N>
uint32_t MeasureHalfX16(const SwdFastPins& pins)
{
  uint32_t raw[32];
  uint32_t best = UINT32_MAX;
  for (int rep = 0; rep < 4; ++rep)
  {
    const uint32_t t0 = Now();
    ReadFast<N, 32>(raw, pins.dm, pins.dm | pins.cm);
    const uint32_t used = Now() - t0;
    best = (used < best) ? used : best;
  }
  Out(pins.dm);
  return (best * 16U + 32U) / 64U;
}

// 档位：每个半周期 N + 2 条指令。240 MHz 下 N = 4 约 20 MHz，N = 16 约 6.7 MHz。
struct Level
{
  SwdFastXferFn xfer;
  uint32_t (*measure)(const SwdFastPins&);
};

constexpr Level kLevels[kSwdFastLevels] = {
    {&Xfer<4>, &MeasureHalfX16<4>},   {&Xfer<5>, &MeasureHalfX16<5>},
    {&Xfer<6>, &MeasureHalfX16<6>},   {&Xfer<7>, &MeasureHalfX16<7>},
    {&Xfer<8>, &MeasureHalfX16<8>},   {&Xfer<9>, &MeasureHalfX16<9>},
    {&Xfer<10>, &MeasureHalfX16<10>}, {&Xfer<11>, &MeasureHalfX16<11>},
    {&Xfer<12>, &MeasureHalfX16<12>}, {&Xfer<14>, &MeasureHalfX16<14>},
    {&Xfer<16>, &MeasureHalfX16<16>},
};
}  // namespace

SwdFastXferFn SwdFastTransferFn(size_t level) { return kLevels[level].xfer; }

uint32_t SwdFastMeasureHalfX16(size_t level, const SwdFastPins& pins)
{
  return kLevels[level].measure(pins);
}

}  // namespace XrDap

#endif
