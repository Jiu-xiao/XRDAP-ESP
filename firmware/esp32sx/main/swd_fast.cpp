// SPDX-License-Identifier: Apache-2.0
// SWD 高速路径：每档展开约 3–10 KB，12 档共约 90 KB。多数档位留在 Flash；
// S3 将 N=10 放进 IRAM，S2 将 N=16 放进 IRAM，限制常驻代码对内部 RAM 的占用。
// 缓存未命中会拉长同步 SWD 时钟的半周期，增加采样延迟。
//
// Fast SWD path: each unrolled level takes about 3–10 KB, about 90 KB in total.
// Most levels stay in flash. S3 keeps N=10 in IRAM and S2 keeps N=16 in IRAM to
// bound the internal RAM cost. Cache misses stretch synchronous SWD half periods
// and increase sampling latency.

#include "sdkconfig.h"
#include "swd_engine.hpp"

#if CONFIG_IDF_TARGET_ESP32S2
#include <atomic>
#endif

// 读专用 GPIO 输入的 CPU 指令：S3 在 EE 扩展里，S2 是 get_gpio_in（与 IDF 的
// dedic_gpio_cpu_ll.h 一致）/ The CPU instruction that reads the dedicated-GPIO inputs:
// in the EE extension on the S3, get_gpio_in on the S2 (as in IDF's dedic_gpio_cpu_ll.h).
#if CONFIG_IDF_TARGET_ESP32S3
#define XRDAP_GET_GPIO_IN "ee.get_gpio_in"
#else
#define XRDAP_GET_GPIO_IN "get_gpio_in"
#endif

// kIramLevelN 这一档只在 S3 上放进 IRAM；S2 用下方的 N=16 特化。
// kIramLevelN is resident on S3 only; S2 uses the N=16 specialization below.
#if CONFIG_IDF_TARGET_ESP32S3
#define XRDAP_IRAM_LEVEL_ATTR IRAM_ATTR
#else
#define XRDAP_IRAM_LEVEL_ATTR
#endif

namespace XrDap
{
namespace
{
#if CONFIG_IDF_TARGET_ESP32S2
std::atomic<uint32_t> last_fast_level{UINT32_MAX};
#endif
using Request = LibXR::Debug::SwdProtocol::Request;
using Response = LibXR::Debug::SwdProtocol::Response;
using Ack = LibXR::Debug::SwdProtocol::Ack;

using LibXR::Debug::SwdProtocol::DecodeAck;
using LibXR::Debug::SwdProtocol::MakeRequest;

inline uint32_t Now() { return esp_cpu_get_cycle_count(); }

inline void Spin(uint32_t cycles)
{
  const uint32_t t0 = Now();
  while ((Now() - t0) < cycles)
  {
  }
}

inline void Out(uint32_t value) { dedic_gpio_cpu_ll_write_all(value); }

// 输出 BITS 位数据（val 低 BITS 位，低位先发），PAR 为 1 时再输出一位校验位 pb。
// 低半周期：写沿（SWCLK 低，SWDIO 为这一位）、算高电平值、N 个 nop；
// 高半周期：写沿（SWCLK 高）、在空闲指令位算下一位要输出的值、凑够 N + 2 条指令。
// 下一位直接从 val 取（extui 取一位，movnez 选 SWDIO 的位），不先展开成数组。
template <int N, int BITS, int PAR>
inline __attribute__((always_inline)) void WriteBits(uint32_t val, uint32_t dm,
                                                     uint32_t cm, uint32_t pb)
{
  uint32_t a;
  uint32_t h;
  uint32_t t;
  asm volatile(
      "extui %[t], %[v], 0, 1\n"
      "movi %[a], 0\n"
      "movnez %[a], %[dm], %[t]\n"
      ".set xr_i, 0\n"
      ".rept %c[cnt]\n"
      "wur.gpio_out %[a]\n"
      "or %[h], %[a], %[cm]\n"
      ".rept %c[n]\n nop\n .endr\n"
      "wur.gpio_out %[h]\n"
      ".if xr_i + 1 < %c[bits]\n"
      "extui %[t], %[v], xr_i + 1, 1\n"
      "movi %[a], 0\n"
      "movnez %[a], %[dm], %[t]\n"
      ".rept %c[n] - 2\n nop\n .endr\n"
      ".elseif (xr_i + 1 == %c[bits]) && %c[par]\n"
      "movi %[a], 0\n"
      "movnez %[a], %[dm], %[pb]\n"
      ".rept %c[n] - 1\n nop\n .endr\n"
      ".else\n"
      ".rept %c[n] + 1\n nop\n .endr\n"
      ".endif\n"
      ".set xr_i, xr_i + 1\n"
      ".endr\n"
      : [a] "=&r"(a), [h] "=&r"(h), [t] "=&r"(t)
      : [v] "r"(val), [dm] "r"(dm), [cm] "r"(cm), [pb] "r"(pb), [cnt] "i"(BITS + PAR),
        [bits] "i"(BITS), [par] "i"(PAR), [n] "i"(N)
      : "memory");
}

// 读 COUNT 位。前 32 位在高半周期里直接拼进 acc（srl 移到最低位、extui 取一位、slli
// 放回原位、or 合并，都放在空闲指令位里）；第 32 位起的原始输入寄存器值存进 raw[]。
// 低半周期：写沿、N 个 nop、采样；高半周期：写沿、处理采到的这一位、凑够 N + 2 条指令。
// 采样是低半周期的最后一条指令，紧挨着上升沿。
template <int N, int COUNT>
inline __attribute__((always_inline)) uint32_t ReadBits(uint32_t* raw, uint32_t vlo,
                                                        uint32_t vhi, uint32_t shift)
{
  uint32_t acc;
  uint32_t x;
  uint32_t t;
  asm volatile(
      "ssr %[sh]\n"
      "movi %[acc], 0\n"
      ".set xr_i, 0\n"
      ".rept %c[cnt]\n"
      "wur.gpio_out %[lo]\n"
      ".rept %c[n]\n nop\n .endr\n" XRDAP_GET_GPIO_IN
      " %[x]\n"
      "wur.gpio_out %[hi]\n"
      ".if xr_i < 32\n"
      "srl %[t], %[x]\n"
      "extui %[t], %[t], 0, 1\n"
      ".if xr_i > 0\n"
      "slli %[t], %[t], xr_i\n"
      ".rept %c[n] - 3\n nop\n .endr\n"
      ".else\n"
      ".rept %c[n] - 2\n nop\n .endr\n"
      ".endif\n"
      "or %[acc], %[acc], %[t]\n"
      ".else\n"
      "s32i %[x], %[p], 4*(xr_i-32)\n"
      ".rept %c[n]\n nop\n .endr\n"
      ".endif\n"
      ".set xr_i, xr_i + 1\n"
      ".endr\n"
      : [acc] "=&r"(acc), [x] "=&r"(x), [t] "=&r"(t)
      : [p] "r"(raw), [lo] "r"(vlo), [hi] "r"(vhi), [sh] "r"(shift), [cnt] "i"(COUNT),
        [n] "i"(N)
      : "memory");
  return acc;
}

template <int N>
inline __attribute__((always_inline)) LibXR::ErrorCode XferBody(const SwdFastPins& pins,
                                                                const Request& req,
                                                                Response& resp)
{
  const uint32_t cm = pins.cm;
  const uint32_t dm = pins.dm;
  const uint32_t sh = static_cast<uint32_t>(__builtin_ctz(pins.im));
  uint32_t raw[2];
  resp.ack = Ack::PROTOCOL;
  resp.rdata = 0U;
  resp.parity_ok = true;

  pins.swdio->SetOutputEnable(true);
  WriteBits<N, 8, 0>(MakeRequest(req), dm, cm, 0U);

  // 释放 SWDIO：换向时钟 + 3 位 ACK（acc 的位 0 是换向时钟，位 1 到 3 是 ACK）
  pins.swdio->SetOutputEnable(false);
  const uint32_t ack_bits = ReadBits<N, 4>(raw, dm, dm | cm, sh);
  resp.ack = DecodeAck(static_cast<uint8_t>((ack_bits >> 1) & 7U));

  if (resp.ack != Ack::OK)
  {
    if (resp.ack == Ack::NO_ACK || resp.ack == Ack::PROTOCOL)
    {
      // 与 CMSIS-DAP 一致：ACK 不是 OK、WAIT、FAULT 时目标可能仍在数据阶段，换向时钟加
      // 33 个时钟都在释放 SWDIO 的状态下发出。
      (void)ReadBits<N, 34>(raw, dm, dm | cm, sh);
    }
    else
    {
      (void)ReadBits<N, 1>(raw, dm, dm | cm, sh);  // 换向时钟，不进数据阶段
    }
    Out(dm);
    pins.swdio->SetOutputEnable(true);
    return LibXR::ErrorCode::OK;
  }

  if (req.rnw)
  {
    // 32 位数据 + 校验位 + 换向时钟；raw[0] 是校验位的采样
    const uint32_t data = ReadBits<N, 34>(raw, dm, dm | cm, sh);
    Out(dm);
    pins.swdio->SetOutputEnable(true);
    const uint32_t parity = (raw[0] >> sh) & 1U;
    resp.rdata = data;
    resp.parity_ok = (parity == static_cast<uint32_t>(__builtin_parity(data)));
  }
  else
  {
    const uint32_t data = req.wdata;
    (void)ReadBits<N, 1>(raw, dm, dm | cm, sh);  // 换向时钟，之后主机驱动 SWDIO
    pins.swdio->SetOutputEnable(true);
    // GPIO_ENABLE 读回之后输出已经生效，不再额外等待
    WriteBits<N, 32, 1>(data, dm, cm, static_cast<uint32_t>(__builtin_parity(data)));
    if (req.port == LibXR::Debug::SwdProtocol::Port::AP)
    {
      // AP 写之后多发几个低电平时钟，让 MEM-AP 忙完，否则紧接着的读会收到 WAIT
      for (uint32_t i = 0; i < pins.idle_after_ap_write; ++i)
      {
        WriteBits<N, 1, 0>(0U, dm, cm, 0U);
      }
    }
    Out(dm);
  }
  return LibXR::ErrorCode::OK;
}

// 用这一档读 32 位（64 个半周期），取 4 次里最少的一次。调用前 SWDIO 应已释放。
// 放在 flash 里的档位 / A level in flash.
template <int N>
LibXR::ErrorCode Xfer(const SwdFastPins& pins, const Request& req, Response& resp)
{
  return XferBody<N>(pins, req, resp);
}

#if CONFIG_IDF_TARGET_ESP32S2
// N=16 is the slowest unrolled level. Keep this large body resident while
// sampling shares the core with live memory queries.
template <>
IRAM_ATTR LibXR::ErrorCode Xfer<16>(const SwdFastPins& pins, const Request& req,
                                    Response& resp)
{
  return XferBody<16>(pins, req, resp);
}
#endif

/// 放进 IRAM 的档位：每个半周期 N + 2 条指令，240 MHz 下约 10 MHz / The level in IRAM:
/// N + 2 instructions per half period, about 10 MHz at 240 MHz.
constexpr int kIramLevelN = 10;

XRDAP_IRAM_LEVEL_ATTR LibXR::ErrorCode XferIram(const SwdFastPins& pins,
                                                const Request& req, Response& resp)
{
  return XferBody<kIramLevelN>(pins, req, resp);
}

template <int N>
uint32_t MeasureHalfX16(const SwdFastPins& pins)
{
  uint32_t raw[2];
  uint32_t best = UINT32_MAX;
  const uint32_t sh = static_cast<uint32_t>(__builtin_ctz(pins.im));
  for (int rep = 0; rep < 4; ++rep)
  {
    const uint32_t t0 = Now();
    (void)ReadBits<N, 32>(raw, pins.dm, pins.dm | pins.cm, sh);
    const uint32_t used = Now() - t0;
    best = (used < best) ? used : best;
  }
  Out(pins.dm);
  return (best * 16U + 32U) / 64U;
}

// 档位：每个半周期 N + 2 条指令。240 MHz 下 N = 3 约 23.7 MHz，N = 4 约 19.8 MHz，
// N = 16 约 6.7 MHz。不提供 N = 2（约 29.5 MHz）：目标在上升沿后约 45 ns 才让数据有效，
// 半周期短于约 4.5 个 CPU 周期就采不到。N = 3 裕量很小。
struct Level
{
  SwdFastXferFn xfer;
  uint32_t (*measure)(const SwdFastPins&);
};

constexpr Level kLevels[kSwdFastLevels] = {
    {&Xfer<3>, &MeasureHalfX16<3>},   {&Xfer<4>, &MeasureHalfX16<4>},
    {&Xfer<5>, &MeasureHalfX16<5>},   {&Xfer<6>, &MeasureHalfX16<6>},
    {&Xfer<7>, &MeasureHalfX16<7>},   {&Xfer<8>, &MeasureHalfX16<8>},
    {&Xfer<9>, &MeasureHalfX16<9>},   {&XferIram, &MeasureHalfX16<kIramLevelN>},
    {&Xfer<11>, &MeasureHalfX16<11>}, {&Xfer<12>, &MeasureHalfX16<12>},
    {&Xfer<14>, &MeasureHalfX16<14>}, {&Xfer<16>, &MeasureHalfX16<16>},
};
}  // namespace

SwdFastXferFn SwdFastTransferFn(size_t level)
{
#if CONFIG_IDF_TARGET_ESP32S2
  last_fast_level.store(static_cast<uint32_t>(level), std::memory_order_relaxed);
#endif
  return kLevels[level].xfer;
}

#if CONFIG_IDF_TARGET_ESP32S2
uint32_t SwdFastLastLevel() { return last_fast_level.load(std::memory_order_relaxed); }
#endif

uint32_t SwdFastMeasureHalfX16(size_t level, const SwdFastPins& pins)
{
  return kLevels[level].measure(pins);
}

}  // namespace XrDap
