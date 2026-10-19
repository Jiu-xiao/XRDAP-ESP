// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "debug/swd.hpp"
#include "dedic_gpio_pin.hpp"

// 给 libxr 的 SWD 实现加上 SWCLK 频率控制。
//
// Adds SWCLK frequency control on top of the libxr SWD implementation.

namespace XrDap
{

/// 包装 libxr 的 SWD 实现以设置 SWCLK 频率 / Wraps the libxr SWD implementation to set
/// the SWCLK frequency.
/// SwdGeneralGPIO 本身不延时（全速），频率由 DedicGpioPin 在每个时钟沿后的周期计数等待
/// 决定：半周期的 CPU 周期数减去一次写沿本身的开销。
/// SwdGeneralGPIO does not delay (full speed); the frequency comes from DedicGpioPin's
/// cycle-count wait after each edge: half-period CPU cycles minus the cost of one edge
/// write.
class SwdWithClock final : public LibXR::Debug::Swd
{
 public:
  /// @brief 包装已有的 SWD 实现 / Wrap an existing SWD implementation.
  /// @param inner 被包装的 SWD，生命周期需长于本对象 / Wrapped SWD; must outlive this
  ///        object.
  /// @param swclk SWCLK 引脚，用于设置边沿间隔 / SWCLK pin used to set the edge spacing.
  /// @param cpu_hz CPU 频率 / CPU frequency in Hz.
  /// @param edge_write_cycles 写一次沿本身在每个 SWCLK 周期多出的 CPU 周期数；默认 13 是
  ///        S3 在 240 MHz 下用逻辑分析仪实测的（约 110 ns） / Extra CPU cycles one edge
  ///        write adds per SWCLK period; the default 13 was measured on an S3 at 240 MHz
  ///        with a logic analyzer (about 110 ns).
  SwdWithClock(LibXR::Debug::Swd& inner, DedicGpioPin& swclk, uint32_t cpu_hz,
               uint32_t edge_write_cycles = 13U)
      : inner_(inner),
        swclk_(swclk),
        cpu_hz_(cpu_hz),
        edge_write_cycles_(edge_write_cycles)
  {
  }

  /// 设置 SWCLK 频率；0 表示不插入等待（全速）/ Set the SWCLK frequency; 0 inserts no
  /// wait (full speed).
  LibXR::ErrorCode SetClockHz(uint32_t hz) override
  {
    const uint32_t half = (hz == 0U) ? 0U : cpu_hz_ / (2U * hz);
    swclk_.SetEdgeSpinCycles(half > edge_write_cycles_ ? half - edge_write_cycles_ : 0U);
    return inner_.SetClockHz(hz);
  }

  /// 以下接口直接转发给被包装的 SWD / The remaining methods forward to the wrapped SWD.
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
