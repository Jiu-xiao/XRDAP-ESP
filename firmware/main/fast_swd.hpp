#pragma once

// Hand-written SWD engine for the ESP32-S3 dedicated GPIO, to find out how fast the probe side can
// really go. Everything in a transfer is a straight line of `wur.gpio_out` / `ee.get_gpio_in`
// instructions (one bundle bit per pin, whole output register written at once), no GPIO objects, no
// virtual calls, no loops over bits. `H` extra `nop`s after every edge slow the clock down in
// single-cycle steps; H = -1 uses a cycle-counter spin of `hold_cycles_` instead.
//
// Differences from LibXR::Debug::SwdGeneralGPIO (deliberate):
//  - data changes together with the falling SWCLK edge (one write per edge instead of two);
//  - the turnaround clock after a read happens before the pad is switched to output (CMSIS-DAP
//    order), so host and target never drive SWDIO at the same time.

#include <cstdint>

#include "debug/swd.hpp"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "soc/gpio_struct.h"
#include "s3_dedic_gpio.hpp"

namespace XrDap
{

class FastSwd final : public LibXR::Debug::Swd
{
 public:
  using Request = LibXR::Debug::SwdProtocol::Request;
  using Response = LibXR::Debug::SwdProtocol::Response;
  using Ack = LibXR::Debug::SwdProtocol::Ack;
  using XferFn = LibXR::ErrorCode (*)(FastSwd&, const Request&, Response&);

  FastSwd(S3DedicGpio& swclk, S3DedicGpio& swdio, uint32_t cpu_hz)
      : swclk_(swclk), swdio_(swdio), cpu_hz_(cpu_hz)
  {
    cm_ = swclk.OutMask();
    dm_ = swdio.OutMask();
    im_ = swdio.InMask();
    dio_bit_ = 1U << swdio.GpioNum();
    SetHoldCycles(80);
    oe_settle_ = 16;
    Put(dm_);
    swdio_.SetOutputEnable(true);
  }

  /// Number of nops after every SWCLK/SWDIO edge (only the unrolled sizes below are available).
  bool SetHold(int nops)
  {
    switch (nops)
    {
      case 0: xfer_ = &Xfer<0>; break;
      case 1: xfer_ = &Xfer<1>; break;
      case 2: xfer_ = &Xfer<2>; break;
      case 3: xfer_ = &Xfer<3>; break;
      case 4: xfer_ = &Xfer<4>; break;
      case 5: xfer_ = &Xfer<5>; break;
      case 6: xfer_ = &Xfer<6>; break;
      case 7: xfer_ = &Xfer<7>; break;
      case 8: xfer_ = &Xfer<8>; break;
      case 10: xfer_ = &Xfer<10>; break;
      case 12: xfer_ = &Xfer<12>; break;
      case 16: xfer_ = &Xfer<16>; break;
      default: return false;
    }
    hold_nops_ = nops;
    dyn_ = false;
    return true;
  }

  /// Late sampling: sample `late` nops after the rising edge instead of before it.
  bool SetHoldLate(int nops, int late)
  {
    switch (nops * 10 + late)
    {
      case 31: xfer_ = &Xfer<3, 1>; break;
      case 32: xfer_ = &Xfer<3, 2>; break;
      case 33: xfer_ = &Xfer<3, 3>; break;
      case 21: xfer_ = &Xfer<2, 1>; break;
      case 22: xfer_ = &Xfer<2, 2>; break;
      case 23: xfer_ = &Xfer<2, 3>; break;
      case 24: xfer_ = &Xfer<2, 4>; break;
      case 11: xfer_ = &Xfer<1, 1>; break;
      case 12: xfer_ = &Xfer<1, 2>; break;
      case 13: xfer_ = &Xfer<1, 3>; break;
      case 14: xfer_ = &Xfer<1, 4>; break;
      case 1: xfer_ = &Xfer<0, 1>; break;
      case 2: xfer_ = &Xfer<0, 2>; break;
      case 3: xfer_ = &Xfer<0, 3>; break;
      case 4: xfer_ = &Xfer<0, 4>; break;
      default: return false;
    }
    hold_nops_ = nops;
    dyn_ = false;
    return true;
  }

  /// Choose the pacing and the sampling position: `dyn` selects cycle-counter pacing (`h` is then set with
  /// SetHoldCycles), otherwise `h` nops per edge; `mode` = -1 sample after the falling edge, 0 before the rising
  /// edge, 1 after the rising edge.
  bool SetPos(bool dyn, int h, int mode)
  {
    if (dyn)
    {
      hold_cycles_ = static_cast<uint32_t>(h);
      dyn_ = true;
      switch (mode)
      {
        case -1: xfer_ = &Xfer<-1, -1>; return true;
        case 0: xfer_ = &Xfer<-1, 0>; return true;
        case 1: xfer_ = &Xfer<-1, 1>; return true;
        default: return false;
      }
    }
    dyn_ = false;
    hold_nops_ = h;
#define XR_POS_CASE(N)                                       \
  case N:                                                    \
    switch (mode)                                            \
    {                                                        \
      case -1: xfer_ = &Xfer<N, -1>; return true;            \
      case 0: xfer_ = &Xfer<N, 0>; return true;              \
      case 1: xfer_ = &Xfer<N, 1>; return true;              \
      default: return false;                                 \
    }
    switch (h)
    {
      XR_POS_CASE(0)
      XR_POS_CASE(1)
      XR_POS_CASE(2)
      XR_POS_CASE(3)
      XR_POS_CASE(4)
      XR_POS_CASE(5)
      XR_POS_CASE(6)
      XR_POS_CASE(8)
      XR_POS_CASE(10)
      default: return false;
    }
#undef XR_POS_CASE
  }

  /// Cycle-counter pacing: wait this many CPU cycles after every edge.
  void SetHoldCycles(uint32_t cycles)
  {
    hold_cycles_ = cycles;
    xfer_ = &Xfer<-1>;
    dyn_ = true;
  }

  LibXR::ErrorCode SetClockHz(uint32_t hz) override
  {
    ++dbg[0];
    dbg_last_hz = hz;
    clock_hz_ = hz;
#ifdef XRDAP_FAST_HI_MODE
    // test: requests >= 16 MHz run at 2 nops per edge (~17 MHz), sampling position from the macro
    if (hz >= 16000000U) { SetPos(false, 2, XRDAP_FAST_HI_MODE); }
    else
#endif
    if (hz >= 14500000U) { SetPos(false, 3, 0); }
    else if (hz >= 13000000U) { SetPos(false, 4, 0); }
    else if (hz >= 11500000U) { SetPos(false, 5, 0); }
    else if (hz >= 10000000U) { SetPos(false, 6, 0); }
    else if (hz >= 8500000U) { SetPos(false, 8, 0); }
    else if (hz >= 7500000U) { SetPos(false, 10, 0); }
    else
    {
      const uint32_t half = (hz == 0U) ? 0U : cpu_hz_ / (2U * hz);
      SetPos(true, static_cast<int>(half > kBaseHalfCycles ? half - kBaseHalfCycles : 0U), 0);
    }
    return LibXR::ErrorCode::OK;
  }

  void Close() override
  {
    ++dbg[1];
    Put(dm_ | cm_);
    swdio_.SetOutputEnable(false);
  }

  LibXR::ErrorCode LineReset() override
  {
    ++dbg[2];
    InvalidateSelectCache();
    swdio_.SetOutputEnable(true);
    for (uint32_t i = 0; i < 64U; ++i)
    {
      Bit(1U);
    }
    Put(dm_);
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode EnterSwd() override
  {
    (void)LineReset();
    WriteByte(0x9EU);
    WriteByte(0xE7U);
    (void)LineReset();
    Put(0U);
    WriteByte(0x00U);
    Put(dm_);
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode Transfer(const Request& req, Response& resp) override
  {
    ++dbg[3];
    return xfer_(*this, req, resp);
  }

  void IdleClocks(uint32_t cycles) override
  {
    ++dbg[4];
    swdio_.SetOutputEnable(true);
    for (uint32_t i = 0; i < cycles; ++i)
    {
      Bit(1U);
    }
    Put(0U);
  }

  LibXR::ErrorCode SeqWriteBits(uint32_t cycles, const uint8_t* data) override
  {
    swdio_.SetOutputEnable(true);
    for (uint32_t i = 0; i < cycles; ++i)
    {
      Bit((data[i / 8U] >> (i & 7U)) & 1U);
    }
    Put(0U);
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode SeqReadBits(uint32_t cycles, uint8_t* out) override
  {
    for (uint32_t i = 0; i < (cycles + 7U) / 8U; ++i)
    {
      out[i] = 0;
    }
    swdio_.SetOutputEnable(false);
    for (uint32_t i = 0; i < cycles; ++i)
    {
      Put(dm_);
      Spin();
      const uint32_t x = Get();
      Put(dm_ | cm_);
      Spin();
      if ((x & im_) != 0U)
      {
        out[i / 8U] = static_cast<uint8_t>(out[i / 8U] | (1U << (i & 7U)));
      }
    }
    Put(0U);
    return LibXR::ErrorCode::OK;
  }

  /// CPU cycles to wait after switching SWDIO to output before the first write data bit
  /// (the GPIO enable register is an APB write and takes effect late).
  void SetOeSettleCycles(uint32_t c) { oe_settle_ = c; }
  /// CPU cycles to wait after releasing SWDIO (end of the request) before the turnaround clock.
  void SetReleaseSettleCycles(uint32_t c) { rel_settle_ = c; }

  volatile uint32_t dbg[8] = {};
  volatile uint32_t dbg_last_hz = 0;
  int HoldNops() const { return hold_nops_; }
  uint32_t HoldCycles() const { return hold_cycles_; }

 private:
  static constexpr uint32_t kBaseHalfCycles = 11U;  // measured on the board, see results file

  static inline void Put(uint32_t v) { asm volatile("wur.gpio_out %0" ::"r"(v) : "memory"); }
  static inline uint32_t Get()
  {
    uint32_t x;
    asm volatile("ee.get_gpio_in %0" : "=r"(x));
    return x;
  }

  template <int H>
  static inline __attribute__((always_inline)) void Hold(uint32_t dyn_cycles)
  {
    if constexpr (H > 0)
    {
#pragma GCC unroll 32
      for (int i = 0; i < H; ++i)
      {
        asm volatile("nop");
      }
    }
    else if constexpr (H < 0)
    {
      const uint32_t t0 = esp_cpu_get_cycle_count();
      while ((esp_cpu_get_cycle_count() - t0) < dyn_cycles)
      {
      }
    }
  }

  inline void Spin() const { (dyn_ ? Hold<-1>(hold_cycles_) : Hold<0>(0U)); }

  /// One clock with SWDIO driven to `d` (generic path for sequences).
  inline void Bit(uint32_t d)
  {
    const uint32_t lo = dm_ & (0U - d);
    Put(lo);
    Spin();
    Put(lo | cm_);
    Spin();
  }

  void WriteByte(uint8_t b)
  {
    swdio_.SetOutputEnable(true);
    for (int i = 0; i < 8; ++i)
    {
      Bit((b >> i) & 1U);
    }
  }

  static inline uint8_t MakeReq(bool apndp, bool rnw, uint8_t addr2b)
  {
    const uint8_t a2 = addr2b & 1U;
    const uint8_t a3 = (addr2b >> 1) & 1U;
    const uint8_t par = static_cast<uint8_t>((apndp ^ rnw ^ a2 ^ a3) & 1U);
    return static_cast<uint8_t>(1U | (static_cast<uint8_t>(apndp) << 1) |
                                (static_cast<uint8_t>(rnw) << 2) | (a2 << 3) | (a3 << 4) |
                                (par << 5) | (1U << 7));
  }

  static inline Ack DecodeAck(uint32_t bits)
  {
    switch (bits)
    {
      case 1: return Ack::OK;
      case 2: return Ack::WAIT;
      case 4: return Ack::FAULT;
      case 0: return Ack::NO_ACK;
      default: return Ack::PROTOCOL;
    }
  }

  /// One clocked read bit. LATE = -1: sample right after the falling edge (A); 0: sample just before the
  /// rising edge (B); 1: sample right after the rising edge (C).
  template <int H, int LATE>
  static inline __attribute__((always_inline)) uint32_t ReadBit(uint32_t cm, uint32_t dm, uint32_t dyn)
  {
    uint32_t x;
    Put(dm);
    if constexpr (LATE < 0)
    {
      x = Get();
      Hold<H>(dyn);
      Put(dm | cm);
      Hold<H>(dyn);
    }
    else if constexpr (LATE == 0)
    {
      Hold<H>(dyn);
      x = Get();
      Put(dm | cm);
      Hold<H>(dyn);
    }
    else
    {
      Hold<H>(dyn);
      Put(dm | cm);
      Hold<LATE - 1>(dyn);
      x = Get();
      Hold<H>(dyn);
    }
    return x;
  }

  template <int H, int LATE = 0>
  static LibXR::ErrorCode Xfer(FastSwd& s, const Request& req, Response& resp)
  {
    const uint32_t cm = s.cm_;
    const uint32_t dm = s.dm_;
    const uint32_t im = s.im_;
    const uint32_t dyn = s.hold_cycles_;
    const uint8_t rb = MakeReq(req.port == LibXR::Debug::SwdProtocol::Port::AP, req.rnw,
                               req.addr2b);
    resp.ack = Ack::PROTOCOL;
    resp.rdata = 0U;
    resp.parity_ok = true;

    // request byte, LSB first, SWDIO driven
    ::GPIO.enable_w1ts = s.dio_bit_;
#pragma GCC unroll 8
    for (int i = 0; i < 8; ++i)
    {
      const uint32_t lo = dm & (0U - ((rb >> i) & 1U));
      Put(lo);
      Hold<H>(dyn);
      Put(lo | cm);
      Hold<H>(dyn);
    }
    // release SWDIO, turnaround clock
    ::GPIO.enable_w1tc = s.dio_bit_;
    if (s.rel_settle_ != 0U)
    {
      const uint32_t t0 = esp_cpu_get_cycle_count();
      while ((esp_cpu_get_cycle_count() - t0) < s.rel_settle_)
      {
      }
    }
    Put(dm);
    Hold<H>(dyn);
    Put(dm | cm);
    Hold<H>(dyn);

    // ACK, 3 bits, sampled in the low phase
    uint32_t ack = 0;
#pragma GCC unroll 3
    for (int i = 0; i < 3; ++i)
    {
      const uint32_t x = ReadBit<H, LATE>(cm, dm, dyn);
      ack |= ((x & im) != 0U ? 1U : 0U) << i;
    }
    resp.ack = DecodeAck(ack);

    if (resp.ack != Ack::OK)
    {
      // turnaround clock, then take the line back
      Put(dm);
      Hold<H>(dyn);
      Put(dm | cm);
      Hold<H>(dyn);
      ::GPIO.enable_w1ts = s.dio_bit_;
      Put(dm);
      return LibXR::ErrorCode::OK;
    }

    if (req.rnw)
    {
      uint32_t data = 0;
#pragma GCC unroll 32
      for (int i = 0; i < 32; ++i)
      {
        const uint32_t x = ReadBit<H, LATE>(cm, dm, dyn);
        data |= ((x & im) != 0U ? 1U : 0U) << i;
      }
      const uint32_t xp = ReadBit<H, LATE>(cm, dm, dyn);
      const uint32_t par = (xp & im) != 0U ? 1U : 0U;
      // turnaround clock with the line released, then drive it again
      Put(dm);
      Hold<H>(dyn);
      Put(dm | cm);
      Hold<H>(dyn);
      ::GPIO.enable_w1ts = s.dio_bit_;
      Put(dm);
      resp.rdata = data;
      resp.parity_ok = par == static_cast<uint32_t>(__builtin_parity(data));
    }
    else
    {
      // turnaround clock with the line released, then drive the data
      Put(dm);
      Hold<H>(dyn);
      Put(dm | cm);
      Hold<H>(dyn);
      ::GPIO.enable_w1ts = s.dio_bit_;
      if (s.oe_settle_ != 0U)
      {
        const uint32_t t0 = esp_cpu_get_cycle_count();
        while ((esp_cpu_get_cycle_count() - t0) < s.oe_settle_)
        {
        }
      }
      const uint32_t data = req.wdata;
#pragma GCC unroll 32
      for (int i = 0; i < 32; ++i)
      {
        const uint32_t lo = dm & (0U - ((data >> i) & 1U));
        Put(lo);
        Hold<H>(dyn);
        Put(lo | cm);
        Hold<H>(dyn);
      }
      const uint32_t plo = dm & (0U - static_cast<uint32_t>(__builtin_parity(data)));
      Put(plo);
      Hold<H>(dyn);
      Put(plo | cm);
      Hold<H>(dyn);
      Put(dm);
    }
    return LibXR::ErrorCode::OK;
  }

  S3DedicGpio& swclk_;
  S3DedicGpio& swdio_;
  uint32_t cpu_hz_;
  uint32_t cm_ = 0, dm_ = 0, im_ = 0, dio_bit_ = 0;
  uint32_t clock_hz_ = 0;
  uint32_t hold_cycles_ = 0;
  uint32_t oe_settle_ = 0;
  uint32_t rel_settle_ = 0;
  int hold_nops_ = 0;
  bool dyn_ = false;
  XferFn xfer_ = nullptr;
};

}  // namespace XrDap
