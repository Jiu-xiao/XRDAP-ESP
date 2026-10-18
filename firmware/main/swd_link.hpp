// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "debug/swd.hpp"
#include "dedic_gpio_pin.hpp"

namespace XrDap
{

/// 在 libxr 的 SWD 实现外包一层，用来设置 SWCLK 频率。
/// libxr 的 SwdGeneralGPIO 不做延时（全速），频率由 DedicGpioPin 在每个时钟沿后的
/// 周期计数等待决定：半周期的 CPU 周期数，减去一次写沿本身的开销。
class SwdWithClock final : public LibXR::Debug::Swd
{
 public:
  /// edge_write_cycles：写一次沿本身在每个 SWCLK 周期里多出的 CPU 周期数。
  /// 默认值 13 是 ESP32-S3 在 240 MHz 下用逻辑分析仪实测的（约 110 ns）。
  SwdWithClock(LibXR::Debug::Swd& inner, DedicGpioPin& swclk, uint32_t cpu_hz,
               uint32_t edge_write_cycles = 13U)
      : inner_(inner), swclk_(swclk), cpu_hz_(cpu_hz), edge_write_cycles_(edge_write_cycles)
  {
  }

  LibXR::ErrorCode SetClockHz(uint32_t hz) override
  {
    const uint32_t half = (hz == 0U) ? 0U : cpu_hz_ / (2U * hz);
    swclk_.SetEdgeSpinCycles(half > edge_write_cycles_ ? half - edge_write_cycles_ : 0U);
    return inner_.SetClockHz(hz);
  }

  void Close() override { inner_.Close(); }
  LibXR::ErrorCode LineReset() override { return inner_.LineReset(); }
  LibXR::ErrorCode EnterSwd() override { return inner_.EnterSwd(); }

  LibXR::ErrorCode Transfer(const LibXR::Debug::SwdProtocol::Request& req,
                            LibXR::Debug::SwdProtocol::Response& resp) override
  {
    return inner_.Transfer(req, resp);
  }

  void IdleClocks(uint32_t cycles) override { inner_.IdleClocks(cycles); }
  LibXR::ErrorCode SeqWriteBits(uint32_t cycles, const uint8_t* data) override
  {
    return inner_.SeqWriteBits(cycles, data);
  }
  LibXR::ErrorCode SeqReadBits(uint32_t cycles, uint8_t* data) override
  {
    return inner_.SeqReadBits(cycles, data);
  }

 private:
  LibXR::Debug::Swd& inner_;
  DedicGpioPin& swclk_;
  uint32_t cpu_hz_;
  uint32_t edge_write_cycles_;
};

}  // namespace XrDap
