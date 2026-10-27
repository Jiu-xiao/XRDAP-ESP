// SPDX-License-Identifier: Apache-2.0
#pragma once

// 专用 GPIO 上的 SWD 位带引擎，SWCLK 高低半周期等长。
//
// 与 libxr SwdGeneralGPIO 的区别：
// - 每个沿只写一次：SWCLK 与 SWDIO 都在专用 GPIO 输出寄存器里，一条指令同时写，数据随
//   SWCLK 下降沿一起变化，高低电平因此等长。
// - 低速（半周期不短于开机标定的下限 min_half_）：按周期计数器上的绝对时刻发沿，下一个
//   沿 = 上一个沿 + 半周期，两沿之间的计算都在等待时间里完成。频率准，占空比 50%。
// - 高速：汇编写死每个半周期的指令，高低半周期指令数相同，每档的实际半
//   周期在开机时测得。
// - 读数据在上升沿前一刻采样（CMSIS-DAP 的位置），目标在上升沿后才换下一位，裕量最大。
// - 空闲时钟期间 SWDIO 为低：pyOCD 会发空闲时钟，高电平会被目标当成请求起始。
// - 读之后的换向时钟在 SWDIO 释放的状态下发出，之后才重新驱动，主机与目标不会同时驱动
//   SWDIO。
//
// SWD bit-bang engine on dedicated GPIO with equal SWCLK high and low half periods.
//
// Differences from libxr SwdGeneralGPIO:
// - Each edge is a single write: SWCLK and SWDIO both sit in the dedicated-GPIO output
//   register and change with one instruction, data moving together with the falling
//   SWCLK edge, so high and low times are equal.
// - Slow clocks (half period not below the boot-time calibrated min_half_): edges are
//   issued at absolute times on the cycle counter, next edge = previous edge + half
//   period, with all computation hidden in the wait. Accurate frequency, 50% duty.
// - Fast clocks: assembly fixes the instructions of every half period,
//   the same count in the high and low halves; the real half period of each level is
//   measured at boot.
// - Read data is sampled just before the rising edge (the CMSIS-DAP position); the
//   target changes the next bit after the rising edge, which gives the most margin.
// - SWDIO is low during idle clocks: pyOCD sends idle clocks and a high level would be
//   taken by the target as the start of a request.
// - The turnaround clock after a read is issued with SWDIO released and the host drives
//   again only afterwards, so host and target never drive SWDIO together.

#include <climits>
#include <cstdint>

#include "debug/swd.hpp"
#include "dedic_gpio_pin.hpp"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "hal/dedic_gpio_cpu_ll.h"

#if CONFIG_IDF_TARGET_ESP32S2
#define XRDAP_S2_SWD_IRAM IRAM_ATTR
#else
#define XRDAP_S2_SWD_IRAM
#endif

namespace XrDap
{

/// 输出使能是 APB 寄存器写，十几个 CPU 周期后才生效；切到输出后等这么久再发第一位数据 /
/// The output enable is an APB register write that takes effect a dozen or so CPU
/// cycles later; wait this long after switching to output before the first data bit.
constexpr uint32_t kOutputEnableSettle = 16U;

/// 汇编路径用到的引脚信息 / Pin information used by the assembly path.
struct SwdFastPins
{
  /// SWCLK 在专用 GPIO 输出寄存器中的位 / SWCLK bit in the dedicated-GPIO output
  /// register.
  uint32_t cm;
  /// SWDIO 在专用 GPIO 输出寄存器中的位 / SWDIO bit in the dedicated-GPIO output
  /// register.
  uint32_t dm;
  /// SWDIO 在专用 GPIO 输入寄存器中的位 / SWDIO bit in the dedicated-GPIO input register.
  uint32_t im;
  /// SWDIO 引脚对象，用于切换输出使能 / SWDIO pin object used to toggle output enable.
  DedicGpioPin* swdio;
  /// AP 写之后多发的低电平时钟数 / Low idle clocks sent after an AP write.
  uint32_t idle_after_ap_write;
};

/// 汇编路径的档位数；每档有一个传输函数和一个开机实测的半周期 / Number of assembly
/// levels; each has a transfer function and a half period measured at boot.
constexpr size_t kSwdFastLevels = 12;
/// 汇编路径的传输函数类型，必须在 SWD 核上调用 / Transfer function type of the assembly
/// path; call on the SWD core only.
using SwdFastXferFn = LibXR::ErrorCode (*)(const SwdFastPins&,
                                           const LibXR::Debug::SwdProtocol::Request&,
                                           LibXR::Debug::SwdProtocol::Response&);
/// 取某一档的传输函数，level 需小于 kSwdFastLevels；多数档位在 Flash，平台的常驻档位见
/// swd_fast.cpp / Get a transfer function; level must be below kSwdFastLevels.
/// Most levels live in flash; see swd_fast.cpp for the platform's resident level.
SwdFastXferFn SwdFastTransferFn(size_t level);
/// 测量某一档的半周期，单位 1/16 CPU 周期；调用前 SWDIO 应已释放，结束时 SWCLK 低 /
/// Measure the half period of a level in 1/16 CPU cycle; SWDIO must be released before
/// the call and SWCLK is left low.
uint32_t SwdFastMeasureHalfX16(size_t level, const SwdFastPins& pins);

#if CONFIG_IDF_TARGET_ESP32S2
/// 最近一次选用的高速档位索引；尚未选用时 UINT32_MAX。低速模式不会更新此诊断值。
/// Last selected fast-level index, or UINT32_MAX before selection. Paced mode
/// does not update this diagnostic value.
uint32_t SwdFastLastLevel();
#endif

/// 专用 GPIO 上的 SWD 位带引擎 / SWD bit-bang engine on dedicated GPIO.
/// @note 非线程安全；所有方法只能由同一个任务在创建 swclk、swdio 的核上调用 / Not
///       thread-safe; call every method from one task on the core that created swclk and
///       swdio.
class SwdEngine final : public LibXR::Debug::Swd
{
 public:
  using Request = LibXR::Debug::SwdProtocol::Request;
  using Response = LibXR::Debug::SwdProtocol::Response;
  using Ack = LibXR::Debug::SwdProtocol::Ack;

  /// @brief 构造引擎并做开机标定（会占用数毫秒的 CPU）/ Construct the engine and run the
  ///        boot calibration (busy for a few milliseconds).
  /// @param swclk SWCLK 引脚，仅输出 / SWCLK pin, output only.
  /// @param swdio SWDIO 引脚，需带输入通路 / SWDIO pin; must have the input path.
  /// @param cpu_hz CPU 频率 / CPU frequency in Hz.
  /// @note 两个引脚的生命周期需长于本对象，且必须在创建它们的核上构造 / Both pins must
  ///       outlive this object, and it must be constructed on the core that created
  ///       them.
  SwdEngine(DedicGpioPin& swclk, DedicGpioPin& swdio, uint32_t cpu_hz)
      : swdio_(swdio),
        cpu_hz_(cpu_hz),
        cm_(swclk.OutMask()),
        dm_(swdio.OutMask()),
        im_(swdio.InMask())
  {
    Out(dm_);  // 空闲：SWCLK 低，SWDIO 高
    Drive();
    Calibrate();
    (void)SetClockHz(kDefaultHz);
  }

  /// 设置 SWCLK 频率；0 取默认 1 MHz，其余限制在 1 kHz 到半个 CPU 频率之间 / Set the
  /// SWCLK frequency; 0 selects the default 1 MHz and the rest is clamped between 1 kHz
  /// and half the CPU frequency.
  LibXR::ErrorCode SetClockHz(uint32_t hz) override
  {
    if (hz == 0U)
    {
      hz = kDefaultHz;
    }
    // 主机发来的是任意 32 位数：先限制在 1 kHz 到半个 CPU 时钟之内，再用 64 位计算，
    // 避免乘法回绕和除 0。
    const uint64_t hz64 = (hz < 1000U) ? 1000U : (hz > cpu_hz_ / 2U ? cpu_hz_ / 2U : hz);
    const uint32_t want_x16 =
        static_cast<uint32_t>((static_cast<uint64_t>(cpu_hz_) * 16U) /
                              (2U * hz64));  // 想要的半周期，单位 1/16 CPU 周期
    fast_ = nullptr;
    // 想要的半周期比按时刻发沿的下限还短时，走汇编路径。
    if (want_x16 < min_half_ * 16U)
    {
      // 选实际半周期不短于想要值的最快一档；请求比最快一档还快时用最快一档。
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
      fast_ = SwdFastTransferFn(pick);
      idle_ap_write_ = IdleClocksFor((cpu_hz_ * 8U) / fast_half_x16_[pick]);
      half_ = min_half_;  // 线复位、序列等操作仍走按时刻发沿的路径
      return LibXR::ErrorCode::OK;
    }
    // 四舍五入到整数个 CPU 周期；Advance 不累计迟到量，平均半周期就是 half_。
    const uint32_t half = (want_x16 + 8U) / 16U;
    half_ = (half < min_half_) ? min_half_ : half;
    idle_ap_write_ = IdleClocksFor(cpu_hz_ / (2U * half_));
    return LibXR::ErrorCode::OK;
  }

  /// 关闭：SWCLK 与 SWDIO 都置高并释放 SWDIO / Close: drive SWCLK and SWDIO high, then
  /// release SWDIO.
  void Close() override
  {
    Out(dm_ | cm_);
    Release();
  }

  /// 线复位：SWDIO 为高时发 64 个时钟 / Line reset: 64 clocks with SWDIO high.
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

  /// 从 JTAG 切换到 SWD：线复位、0xE79E、线复位、至少 2 个低电平空闲时钟 / Switch from
  /// JTAG to SWD: line reset, 0xE79E, line reset, then at least 2 low idle clocks.
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

  /// 发 cycles 个 SWDIO 为低的空闲时钟 / Send cycles idle clocks with SWDIO low.
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

  /// 输出 cycles 位，低位先发；data 至少 (cycles + 7) / 8 字节 / Send cycles bits, LSB
  /// first; data must hold at least (cycles + 7) / 8 bytes.
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

  /// 读 cycles 位，低位在前；out 至少 (cycles + 7) / 8 字节，函数先将其清零 / Read cycles
  /// bits, LSB first; out must hold at least (cycles + 7) / 8 bytes and is cleared
  /// first.
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

  /// 执行一次 SWD 传输；协议层的 ACK/校验结果写入 resp，返回值只表示执行本身 / Run one
  /// SWD transfer; ACK and parity results go into resp, the return value only reports the
  /// execution itself.
  LibXR::ErrorCode XRDAP_S2_SWD_IRAM Transfer(const Request& req, Response& resp) override
  {
#if CONFIG_IDF_TARGET_ESP32S2
    // S2 shares one core with Wi-Fi/lwIP. An ISR in the middle of a paced SWD
    // request can stretch a half-cycle far enough for the target to lose the
    // request framing; after that every read reports ACK=PROTOCOL until a line
    // reset happens. Keep one request electrically contiguous. The section is
    // bounded to a single SWD transfer and never waits on a task or socket.
    portENTER_CRITICAL(&transfer_mux_);
#endif
    LibXR::ErrorCode ec;
    if (fast_ != nullptr)
    {
      ec = fast_(Pins(), req, resp);
    }
    else
    {
      ec = PacedTransfer(req, resp);
    }
#if CONFIG_IDF_TARGET_ESP32S2
    portEXIT_CRITICAL(&transfer_mux_);
#endif
    return ec;
  }

 private:
  static constexpr uint32_t kDefaultHz = 1000000U;
  static constexpr uint32_t kLineResetCycles = 64U;
  static constexpr uint32_t kJtagToSwd = 0xE79EU;  // 低位先发：0x9E、0xE7
  static constexpr int32_t kLateSlack = 4;
  // MEM-AP 写完 TAR 后要忙一小段时间，紧接着读 DRW 会收到 WAIT，白白多一次完整传输。
  // 因此 AP 写之后多发 2 个时钟加约 0.35 µs 的低电平空闲时钟。
  static constexpr uint32_t kApWriteIdleNs = 350U;
  static uint32_t IdleClocksFor(uint32_t hz)
  {
    return 2U + (hz / 1000U * kApWriteIdleNs + 999999U) / 1000000U;
  }

  // ------------------------------------------------------------ 按时刻发沿

  struct Pacer
  {
    uint32_t t;     // 下一个沿的时刻（CPU 周期计数）
    uint32_t half;  // 半周期
  };

  static inline __attribute__((always_inline)) uint32_t Now()
  {
    return esp_cpu_get_cycle_count();
  }

  // 写整个专用 GPIO 输出寄存器：本工程只用到 SWCLK、SWDIO 两个通道，一条指令同时改
  // 两个引脚。
  static inline __attribute__((always_inline)) void Out(uint32_t value)
  {
    dedic_gpio_cpu_ll_write_all(value);
  }

  inline __attribute__((always_inline)) void Drive() { swdio_.SetOutputEnable(true); }
  inline __attribute__((always_inline)) void Release() { swdio_.SetOutputEnable(false); }

  inline __attribute__((always_inline)) Pacer Begin() const
  {
    return Pacer{Now(), half_};
  }

  static inline __attribute__((always_inline)) uint32_t WaitEdge(const Pacer& p)
  {
    uint32_t now;
    do
    {
      now = Now();
    } while (static_cast<int32_t>(now - p.t) < 0);
    return now;
  }

  // 下一个沿 = 这个沿的目标时刻 + 半周期：等待循环粒度造成的几个周期的迟到不累计，平均
  // 半周期正好是设定值。迟到超过 kLateSlack（计算没做完，或被中断打断）时从实际时刻起算，
  // 不去追赶，避免出现很短的半周期。
  static inline __attribute__((always_inline)) void Advance(Pacer& p, uint32_t now)
  {
    p.t = ((static_cast<int32_t>(now - p.t) > kLateSlack) ? now : p.t) + p.half;
  }

  static inline __attribute__((always_inline)) void Edge(Pacer& p, uint32_t value)
  {
    const uint32_t now = WaitEdge(p);
    Out(value);
    Advance(p, now);
  }

  // 一个时钟周期，SWDIO 由主机驱动：下降沿放出数据，上升沿目标采样。
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

  // 一个时钟周期，SWDIO 由目标驱动：在上升沿之前一刻采样。
  inline __attribute__((always_inline)) bool ClockIn(Pacer& p) const
  {
    Edge(p, dm_);
    const uint32_t now = WaitEdge(p);
    const uint32_t in = dedic_gpio_cpu_ll_read_in();
    Out(dm_ | cm_);
    Advance(p, now);
    return (in & im_) != 0U;
  }

  // 最后半个周期走完后回到空闲电平：SWCLK 低，SWDIO 高。
  inline __attribute__((always_inline)) void End(Pacer& p) const { Edge(p, dm_); }

  // Keep parity local even in Debug builds: a compiler runtime helper in flash
  // would defeat the bounded IRAM path.
  static inline __attribute__((always_inline)) uint32_t Parity(uint32_t v)
  {
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return v & 1U;
  }

  // Force these tiny protocol operations inline even at -Og. An out-of-line
  // constexpr helper would need a flash call/literal from the S2 IRAM section.
  static inline __attribute__((always_inline)) uint8_t RequestByte(const Request& req)
  {
    const uint8_t ap = req.port == LibXR::Debug::SwdProtocol::Port::AP ? 1U : 0U;
    const uint8_t read = req.rnw ? 1U : 0U;
    const uint8_t a2 = req.addr2b & 1U;
    const uint8_t a3 = (req.addr2b >> 1U) & 1U;
    return 0x81U | (ap << 1U) | (read << 2U) | (a2 << 3U) | (a3 << 4U) |
           ((ap ^ read ^ a2 ^ a3) << 5U);
  }

  static inline __attribute__((always_inline)) Ack DecodeAck(uint32_t bits)
  {
    return (bits == 0U || bits == 1U || bits == 2U || bits == 4U) ? static_cast<Ack>(bits)
                                                                  : Ack::PROTOCOL;
  }

  LibXR::ErrorCode XRDAP_S2_SWD_IRAM PacedTransfer(const Request& req, Response& resp)
  {
    resp.ack = Ack::PROTOCOL;
    resp.rdata = 0U;
    resp.parity_ok = true;

    Drive();
    Pacer p = Begin();
    ClockOutBits(p, RequestByte(req), 8U);

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
      if (resp.ack == Ack::NO_ACK || resp.ack == Ack::PROTOCOL)
      {
        // 与 CMSIS-DAP 一致：ACK 不是 OK、WAIT、FAULT 时目标可能仍在数据阶段，释放
        // SWDIO 后再空转 33 个时钟。
        for (uint32_t i = 0; i < 33U; ++i)
        {
          ClockIn(p);
        }
      }
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
      resp.parity_ok = (parity == Parity(data));
    }
    else
    {
      ClockIn(p);  // 换向时钟，之后主机驱动 SWDIO
      Drive();
      p.t += kOutputEnableSettle;
      ClockOutBits(p, req.wdata, 32U);
      ClockOut(p, Parity(req.wdata));
      if (req.port == LibXR::Debug::SwdProtocol::Port::AP)
      {
        for (uint32_t i = 0; i < idle_ap_write_; ++i)
        {
          ClockOut(p, 0U);
        }
      }
      End(p);
    }
    return LibXR::ErrorCode::OK;
  }

  // ------------------------------------------------- 汇编路径（见 swd_fast.cpp）
  using FastXfer = LibXR::ErrorCode (*)(const SwdFastPins&, const Request&, Response&);
  static constexpr size_t kFastCount = kSwdFastLevels;

  void CalibrateFast()
  {
    for (size_t i = 0; i < kFastCount; ++i)
    {
      fast_half_x16_[i] = SwdFastMeasureHalfX16(i, Pins());
    }
  }

  inline __attribute__((always_inline)) SwdFastPins Pins() const
  {
    return SwdFastPins{cm_, dm_, im_, &swdio_, idle_ap_write_};
  }

  uint32_t fast_half_x16_[kFastCount] = {};
  FastXfer fast_ = nullptr;

  // ------------------------------------------------------------ 标定

  // 用 h 作半周期跑 32 个读时钟和 32 个写时钟（含收尾共 129 个半周期），返回用掉的 CPU
  // 周期数，取 4 次里最少的一次。
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

  // 开机标定，结束时 SWDIO 重新驱动。按时刻发沿的路径：半周期足够长时总耗时约为
  // 128 × h，取总耗时不超过此值太多的最小 h 作下限 min_half_；汇编路径：逐档实测半周期。
  void Calibrate()
  {
    Release();
    // 32 个读时钟 + 32 个写时钟 = 128 个半周期，外加开头结尾几个周期的迟到。
    constexpr uint32_t kHalves = 128U;
    uint32_t h = 2U;
    for (; h < 60U; ++h)
    {
      if (TimeBits(h) <= kHalves * h + 16U)
      {
        break;
      }
    }
    min_half_ = h;
    CalibrateFast();
    Out(dm_);
    Drive();
  }

  DedicGpioPin& swdio_;
  uint32_t cpu_hz_;
  uint32_t cm_;
  uint32_t dm_;
  uint32_t im_;
  uint32_t half_ = 0U;
  uint32_t idle_ap_write_ = 0U;
  uint32_t min_half_ = 2U;
#if CONFIG_IDF_TARGET_ESP32S2
  portMUX_TYPE transfer_mux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};

}  // namespace XrDap

#undef XRDAP_S2_SWD_IRAM
