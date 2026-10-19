// SPDX-License-Identifier: Apache-2.0
#pragma once

// 专用 GPIO 上的 SWD 位带引擎，SWCLK 高低半周期等长。
//
// 和 libxr SwdGeneralGPIO 的区别：
// - 每个沿只写一次：SWCLK 和 SWDIO 都在专用 GPIO 输出寄存器里，一条指令同时写；数据随 SWCLK 下降沿一起变化。
// - 原来的做法是"写沿之后再等固定周期数"，写数据、算下一位、采样的时间都加在等待之外，高低电平不等长，
//   最高频率由较长的那半个周期决定。这里两种做法保证等长：
//   - 低速（半周期 ≥ kPacedMinHalf 个 CPU 周期）：按周期计数器上的绝对时刻发沿，下一个沿 = 上一个沿 + 半周期，
//     两个沿之间的计算都在等待时间里做完。频率准，占空比 50%。
//   - 高速（只在 ESP32-S3 上）：汇编写死每个半周期的指令：一次写沿 + 一次取数或存数 + N 个 nop，
//     高低两个半周期指令数相同。每一位要输出的值事先算好放进数组，读到的原始值先存进数组，传输完再拼。
//     N 对应的实际半周期开机时实测。
// - 读数据在上升沿之前一刻采样（CMSIS-DAP 的位置）：目标在上升沿之后才换下一位，裕量最大。
// - 空闲时钟（idle cycles）期间 SWDIO 为低（pyOCD 会发空闲时钟，高电平会被目标当成请求的开始）。
// - 读之后的换向时钟在 SWDIO 释放的状态下发出，之后才重新驱动，主机和目标不会同时驱动 SWDIO。

#include <climits>
#include <cstdint>

#include "debug/swd.hpp"
#include "dedic_gpio_pin.hpp"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "hal/dedic_gpio_cpu_ll.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
#define XRDAP_SWD_FAST_PATH 1
#else
#define XRDAP_SWD_FAST_PATH 0
#endif

namespace XrDap
{

#if XRDAP_SWD_FAST_PATH
/// 汇编路径用到的引脚信息：SWCLK、SWDIO 在专用 GPIO 输出寄存器里的位，SWDIO 在输入寄存器里的位。
struct SwdFastPins
{
  uint32_t cm;
  uint32_t dm;
  uint32_t im;
  DedicGpioPin* swdio;
};

/// 汇编路径的档位数，以及每一档的传输函数和实测半周期（1/16 CPU 周期）。代码量大，放在 flash 里，见 swd_fast_s3.cpp。
constexpr size_t kSwdFastLevels = 12;
using SwdFastXferFn = LibXR::ErrorCode (*)(const SwdFastPins&, const LibXR::Debug::SwdProtocol::Request&,
                                           LibXR::Debug::SwdProtocol::Response&);
SwdFastXferFn SwdFastTransferFn(size_t level);
uint32_t SwdFastMeasureHalfX16(size_t level, const SwdFastPins& pins);
#endif

class SwdEngine final : public LibXR::Debug::Swd
{
 public:
  using Request = LibXR::Debug::SwdProtocol::Request;
  using Response = LibXR::Debug::SwdProtocol::Response;
  using Ack = LibXR::Debug::SwdProtocol::Ack;

  /// 必须在创建 swclk、swdio 的那个核上构造和使用。
  SwdEngine(DedicGpioPin& swclk, DedicGpioPin& swdio, uint32_t cpu_hz)
      : swdio_(swdio), cpu_hz_(cpu_hz), cm_(swclk.OutMask()), dm_(swdio.OutMask()),
        im_(swdio.InMask())
  {
    Out(dm_);  // 空闲：SWCLK 低，SWDIO 高
    Drive();
    Calibrate();
    (void)SetClockHz(kDefaultHz);
  }

  LibXR::ErrorCode SetClockHz(uint32_t hz) override
  {
    if (hz == 0U)
    {
      hz = kDefaultHz;
    }
    const uint32_t want_x16 = (cpu_hz_ * 16U) / (2U * hz);  // 想要的半周期，单位 1/16 CPU 周期
    fast_ = nullptr;
#if XRDAP_SWD_FAST_PATH
    if (want_x16 < kPacedMinHalf * 16U)
    {
      // 选实际半周期不短于想要值的最快一档；请求比最快一档还快时用最快一档
      size_t pick = 0;
      for (size_t i = 0; i < kFastCount; ++i)
      {
        if (fast_half_x16_[i] >= want_x16)
        {
          pick = i;
          break;
        }
        pick = i;
      }
      // 表按 N 从小到大排列，上面的循环找到的是第一个不短于想要值的一档
      fast_ = SwdFastTransferFn(pick);
      fast_idx_ = pick;
      half_ = min_half_;  // 线复位、序列等不讲究速度的操作仍走按时刻发沿的路径
      return LibXR::ErrorCode::OK;
    }
#endif
    // 半周期 = 设定值 + 等待循环的平均迟到量，所以设定值要减去它；四舍五入到整数个 CPU 周期
    const uint32_t half = (want_x16 > late_x16_ + 8U) ? (want_x16 - late_x16_ + 8U) / 16U : 0U;
    half_ = (half < min_half_) ? min_half_ : half;
    return LibXR::ErrorCode::OK;
  }

  /// 当前一次 SWD 传输所用的实际 SWCLK 频率。
  uint32_t ActualHz() const
  {
#if XRDAP_SWD_FAST_PATH
    if (fast_ != nullptr)
    {
      return (cpu_hz_ * 8U) / fast_half_x16_[fast_idx_];
    }
#endif
    return (cpu_hz_ * 8U) / (half_ * 16U + late_x16_);
  }

  void Close() override
  {
    Out(dm_ | cm_);
    Release();
  }

  LibXR::ErrorCode LineReset() override
  {
    InvalidateSelectCache();
    Drive();
    Pacer p = Begin();
    for (uint32_t i = 0; i < kLineResetCycles; ++i)
    {
      ClockOut(p, 1U);
    }
    End(p);
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode EnterSwd() override
  {
    (void)LineReset();
    Pacer p = Begin();
    ClockOutBits(p, kJtagToSwd, 16U);
    End(p);
    (void)LineReset();
    p = Begin();
    ClockOutBits(p, 0U, 8U);  // 至少 2 个低电平空闲时钟
    End(p);
    return LibXR::ErrorCode::OK;
  }

  void IdleClocks(uint32_t cycles) override
  {
    Drive();
    Pacer p = Begin();
    for (uint32_t i = 0; i < cycles; ++i)
    {
      ClockOut(p, 0U);
    }
    End(p);
  }

  LibXR::ErrorCode SeqWriteBits(uint32_t cycles, const uint8_t* data) override
  {
    Drive();
    Pacer p = Begin();
    for (uint32_t i = 0; i < cycles; ++i)
    {
      ClockOut(p, (data[i / 8U] >> (i & 7U)) & 1U);
    }
    End(p);
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode SeqReadBits(uint32_t cycles, uint8_t* out) override
  {
    for (uint32_t i = 0; i < (cycles + 7U) / 8U; ++i)
    {
      out[i] = 0U;
    }
    Release();
    Pacer p = Begin();
    for (uint32_t i = 0; i < cycles; ++i)
    {
      if (ClockIn(p))
      {
        out[i / 8U] = static_cast<uint8_t>(out[i / 8U] | (1U << (i & 7U)));
      }
    }
    End(p);
    Drive();
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode Transfer(const Request& req, Response& resp) override
  {
#if XRDAP_SWD_FAST_PATH
    if (fast_ != nullptr)
    {
      return fast_(Pins(), req, resp);
    }
#endif
    return PacedTransfer(req, resp);
  }

 private:
  static constexpr uint32_t kDefaultHz = 1000000U;
  static constexpr uint32_t kLineResetCycles = 64U;
  static constexpr uint32_t kJtagToSwd = 0xE79EU;  // 低位先发：0x9E、0xE7
  // 输出使能是 APB 寄存器写，十几个 CPU 周期后才生效，切到输出后等这么久再发第一位数据
  static constexpr uint32_t kOutputEnableSettle = 16U;
  // 按时刻发沿的路径里，一个半周期内最长的那段计算实测约 17 个 CPU 周期（240 MHz），
  // 半周期短于这个值时高低电平就不等长了，改走汇编路径。
  static constexpr uint32_t kPacedMinHalf = 20U;

  // ------------------------------------------------------------ 按时刻发沿

  struct Pacer
  {
    uint32_t t;     // 下一个沿的时刻（CPU 周期计数）
    uint32_t half;  // 半周期
  };

  static inline uint32_t Now() { return esp_cpu_get_cycle_count(); }

  /// 写整个专用 GPIO 输出寄存器：本工程只用到 SWCLK、SWDIO 两个通道，一条指令同时改两个引脚。
  static inline __attribute__((always_inline)) void Out(uint32_t value)
  {
    dedic_gpio_cpu_ll_write_all(value);
  }

  inline void Drive() { swdio_.SetOutputEnable(true); }
  inline void Release() { swdio_.SetOutputEnable(false); }

  inline Pacer Begin() const { return Pacer{Now(), half_}; }

  static inline __attribute__((always_inline)) uint32_t WaitEdge(const Pacer& p)
  {
    uint32_t now;
    do
    {
      now = Now();
    } while (static_cast<int32_t>(now - p.t) < 0);
    return now;
  }

  /// 沿发晚了（等待循环的粒度，或被中断打断）时下一个沿从实际时刻起算，不去"追"，保证半周期不短于设定值。
  static inline __attribute__((always_inline)) void Advance(Pacer& p, uint32_t now)
  {
    p.t = ((static_cast<int32_t>(now - p.t) > 0) ? now : p.t) + p.half;
  }

  static inline __attribute__((always_inline)) void Edge(Pacer& p, uint32_t value)
  {
    const uint32_t now = WaitEdge(p);
    Out(value);
    Advance(p, now);
  }

  /// 一个时钟周期，SWDIO 由主机驱动：下降沿时放出数据，上升沿时目标采样。
  inline __attribute__((always_inline)) void ClockOut(Pacer& p, uint32_t bit) const
  {
    const uint32_t lo = dm_ & (0U - (bit & 1U));
    Edge(p, lo);
    Edge(p, lo | cm_);
  }

  inline __attribute__((always_inline)) void ClockOutBits(Pacer& p, uint32_t bits,
                                                          uint32_t count) const
  {
    for (uint32_t i = 0; i < count; ++i)
    {
      ClockOut(p, bits >> i);
    }
  }

  /// 一个时钟周期，SWDIO 由目标驱动：在上升沿之前一刻采样。
  inline __attribute__((always_inline)) bool ClockIn(Pacer& p) const
  {
    Edge(p, dm_);
    const uint32_t now = WaitEdge(p);
    const uint32_t in = dedic_gpio_cpu_ll_read_in();
    Out(dm_ | cm_);
    Advance(p, now);
    return (in & im_) != 0U;
  }

  /// 最后半个周期走完后回到空闲电平：SWCLK 低，SWDIO 高。
  inline void End(Pacer& p) const { Edge(p, dm_); }

  LibXR::ErrorCode PacedTransfer(const Request& req, Response& resp)
  {
    resp.ack = Ack::PROTOCOL;
    resp.rdata = 0U;
    resp.parity_ok = true;

    Drive();
    Pacer p = Begin();
    ClockOutBits(p, MakeRequest(req), 8U);

    // 释放 SWDIO，换向时钟，然后读 3 位 ACK
    Release();
    ClockIn(p);
    uint32_t ack = 0U;
    for (uint32_t i = 0; i < 3U; ++i)
    {
      ack |= (ClockIn(p) ? 1U : 0U) << i;
    }
    resp.ack = DecodeAck(ack);

    if (resp.ack != Ack::OK)
    {
      ClockIn(p);  // 换向时钟，不进数据阶段
      End(p);
      Drive();
      return LibXR::ErrorCode::OK;
    }

    if (req.rnw)
    {
      uint32_t data = 0U;
      for (uint32_t i = 0; i < 32U; ++i)
      {
        data |= (ClockIn(p) ? 1U : 0U) << i;
      }
      const uint32_t parity = ClockIn(p) ? 1U : 0U;
      ClockIn(p);  // 换向时钟，SWDIO 仍释放
      End(p);
      Drive();
      resp.rdata = data;
      resp.parity_ok = (parity == static_cast<uint32_t>(__builtin_parity(data)));
    }
    else
    {
      ClockIn(p);  // 换向时钟，之后主机驱动 SWDIO
      Drive();
      p.t += kOutputEnableSettle;
      ClockOutBits(p, req.wdata, 32U);
      ClockOut(p, static_cast<uint32_t>(__builtin_parity(req.wdata)));
      End(p);
    }
    return LibXR::ErrorCode::OK;
  }

  // ------------------------------------------------------------ 汇编路径（ESP32-S3，在 swd_fast_s3.cpp）
#if XRDAP_SWD_FAST_PATH
  using FastXfer = LibXR::ErrorCode (*)(const SwdFastPins&, const Request&, Response&);
  static constexpr size_t kFastCount = kSwdFastLevels;

  void CalibrateFast()
  {
    for (size_t i = 0; i < kFastCount; ++i)
    {
      fast_half_x16_[i] = SwdFastMeasureHalfX16(i, Pins());
    }
  }

  SwdFastPins Pins() const { return SwdFastPins{cm_, dm_, im_, &swdio_}; }

  uint32_t fast_half_x16_[kFastCount] = {};
  size_t fast_idx_ = 0;
  FastXfer fast_ = nullptr;
#else
  void* fast_ = nullptr;
#endif

  // ------------------------------------------------------------ 标定

  /// 用 h 作半周期跑 32 个读时钟和 32 个写时钟（共 129 个半周期），返回用掉的 CPU 周期数，取 4 次里最少的一次。
  uint32_t TimeBits(uint32_t h)
  {
    half_ = h;
    uint32_t best = UINT32_MAX;
    for (int rep = 0; rep < 4; ++rep)
    {
      Pacer p = Begin();
      const uint32_t t0 = Now();
      for (uint32_t i = 0; i < 32U; ++i)
      {
        ClockIn(p);
      }
      ClockOutBits(p, 0xA5A5A5A5U, 32U);
      End(p);
      const uint32_t used = Now() - t0;
      best = (used < best) ? used : best;
    }
    return best;
  }

  /// 开机标定，SWDIO 保持释放。按时刻发沿的路径：半周期足够长时，每个半周期 = 设定值 + 等待循环的迟到量，
  /// 总耗时减 129 × h 就是迟到量之和；取不被计算拉长的最小 h 作下限。汇编路径：实测每一档的半周期。
  void Calibrate()
  {
    Release();
    constexpr uint32_t kHalves = 129U;
    constexpr uint32_t kLong = 60U;
    const uint32_t jitter = TimeBits(kLong) - kHalves * kLong;
    late_x16_ = (jitter * 16U + kHalves / 2U) / kHalves;
    uint32_t h = 2U;
    for (; h < kLong; ++h)
    {
      if (TimeBits(h) <= kHalves * h + jitter + 8U)
      {
        break;
      }
    }
    min_half_ = h;
#if XRDAP_SWD_FAST_PATH
    CalibrateFast();
#endif
    Out(dm_);
    Drive();
  }

  // ------------------------------------------------------------ 协议

  static inline uint32_t MakeRequest(const Request& req)
  {
    const uint32_t apndp = (req.port == LibXR::Debug::SwdProtocol::Port::AP) ? 1U : 0U;
    const uint32_t rnw = req.rnw ? 1U : 0U;
    const uint32_t a2 = req.addr2b & 1U;
    const uint32_t a3 = (req.addr2b >> 1) & 1U;
    const uint32_t par = (apndp ^ rnw ^ a2 ^ a3) & 1U;
    return 1U | (apndp << 1) | (rnw << 2) | (a2 << 3) | (a3 << 4) | (par << 5) | (1U << 7);
  }

  static inline Ack DecodeAck(uint32_t bits)
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

  DedicGpioPin& swdio_;
  uint32_t cpu_hz_;
  uint32_t cm_;
  uint32_t dm_;
  uint32_t im_;
  uint32_t half_ = 0U;
  uint32_t min_half_ = 2U;
  uint32_t late_x16_ = 0U;  // 等待循环的平均迟到量，单位 1/16 CPU 周期

};

}  // namespace XrDap
