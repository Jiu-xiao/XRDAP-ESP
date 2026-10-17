#pragma once

// Transparent Swd wrapper that counts SWD transactions and CPU cycles spent in them.
// Debug aid: lets us split "time on the wire" from "time in the DAP command processor".

#include <cstdint>

#include "debug/swd.hpp"
#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "s3_dedic_gpio.hpp"

namespace XrDap
{

class ProfiledSwd final : public LibXR::Debug::Swd
{
 public:
  ProfiledSwd(LibXR::Debug::Swd& inner, S3DedicGpio& swclk, uint32_t cpu_hz)
      : inner_(inner), swclk_(swclk), cpu_hz_(cpu_hz)
  {
  }

  LibXR::ErrorCode SetClockHz(uint32_t hz) override
  {
    clock_hz_ = hz;
    // The inner driver always runs its no-delay path (loops_per_us = 0); the SWCLK frequency
    // is set by the edge spin of the GPIO object: half period in CPU cycles, minus the
    // natural cost of one edge write.
    const uint32_t half = (hz == 0U) ? 0U : cpu_hz_ / (2U * hz);
    swclk_.SetEdgeSpinCycles(half > kEdgeWriteCycles ? half - kEdgeWriteCycles : 0U);
    return inner_.SetClockHz(hz);
  }
  void Close() override { inner_.Close(); }
  LibXR::ErrorCode LineReset() override { return inner_.LineReset(); }
  LibXR::ErrorCode EnterSwd() override { return inner_.EnterSwd(); }

  LibXR::ErrorCode Transfer(const LibXR::Debug::SwdProtocol::Request& req,
                            LibXR::Debug::SwdProtocol::Response& resp) override
  {
    const uint32_t t0 = esp_cpu_get_cycle_count();
    const LibXR::ErrorCode ec = inner_.Transfer(req, resp);
    cycles_ += esp_cpu_get_cycle_count() - t0;
    ++transfers_;
    if (xPortInIsrContext())
    {
      ++isr_transfers_;
    }
    core_mask_ |= 1U << esp_cpu_get_core_id();
    if (resp.ack != LibXR::Debug::SwdProtocol::Ack::OK)
    {
      ++not_ok_;
    }
    return ec;
  }

  void IdleClocks(uint32_t cycles) override { inner_.IdleClocks(cycles); }
  LibXR::ErrorCode SeqWriteBits(uint32_t cycles, const uint8_t* d) override
  {
    return inner_.SeqWriteBits(cycles, d);
  }
  LibXR::ErrorCode SeqReadBits(uint32_t cycles, uint8_t* d) override
  {
    return inner_.SeqReadBits(cycles, d);
  }

  /// Transactions executed in interrupt context, all transactions, cores seen (bit mask).
  void IsrStats(uint32_t& isr, uint32_t& total, uint32_t& cores) const
  {
    isr = isr_transfers_;
    total = transfers_total_ + transfers_;
    cores = core_mask_;
  }

  /// Prints and clears the counters. 46 SWCLK cycles = one OK read/write transaction.
  void Dump(const char* tag)
  {
    if (transfers_ == 0U)
    {
      return;
    }
    const uint32_t cyc = cycles_ / transfers_;
    ESP_LOGI(tag,
             "SWD clock_req=%u Hz: %u transfers (%u not-OK), avg %u cpu cycles = %u ns each "
             "=> implied %u kHz for 46-clock transfers",
             (unsigned)clock_hz_, (unsigned)transfers_, (unsigned)not_ok_, (unsigned)cyc,
             (unsigned)(cyc * 1000U / 240U), (unsigned)(46U * 240000U / cyc));
    transfers_total_ += transfers_;
    transfers_ = 0U;
    cycles_ = 0U;
    not_ok_ = 0U;
  }

 private:
  static constexpr uint32_t kEdgeWriteCycles = 13U;  // measured with a logic analyzer: ~110 ns extra per SWCLK period
  LibXR::Debug::Swd& inner_;
  S3DedicGpio& swclk_;
  uint32_t cpu_hz_;
  uint32_t clock_hz_ = 0U;
  uint32_t transfers_ = 0U;
  uint32_t not_ok_ = 0U;
  uint32_t cycles_ = 0U;
  uint32_t isr_transfers_ = 0U;
  uint32_t transfers_total_ = 0U;
  uint32_t core_mask_ = 0U;
};

}  // namespace XrDap
