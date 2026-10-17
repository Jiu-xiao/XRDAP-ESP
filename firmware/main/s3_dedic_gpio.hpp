#pragma once

// ESP32-S3 CPU-dedicated GPIO exposed through the LibXR::GPIO interface, so that
// LibXR::Debug::SwdGeneralGPIO can toggle SWCLK/SWDIO with single-cycle CPU
// instructions instead of GPIO matrix register writes.

#include <cstdint>

#include "driver/dedic_gpio.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "gpio.hpp"
#include "hal/dedic_gpio_cpu_ll.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_reg.h"

#ifndef XRDAP_DRIVE_CAP
#define XRDAP_DRIVE_CAP 1
#endif

namespace XrDap
{

/// One dedicated-GPIO channel. `bidirectional` keeps the input route so Read() works.
class S3DedicGpio final : public LibXR::GPIO
{
 public:
  S3DedicGpio(int gpio_num, bool bidirectional, bool pull_up) : gpio_(gpio_num)
  {
    gpio_reset_pin(static_cast<gpio_num_t>(gpio_));
    gpio_set_direction(static_cast<gpio_num_t>(gpio_), GPIO_MODE_INPUT_OUTPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(gpio_),
                       pull_up ? GPIO_PULLUP_ONLY : GPIO_FLOATING);

    int gpios[1] = {gpio_num};
    dedic_gpio_bundle_config_t cfg = {};
    cfg.gpio_array = gpios;
    cfg.array_size = 1;
    cfg.flags.out_en = 1;
    cfg.flags.in_en = bidirectional ? 1 : 0;
    ESP_ERROR_CHECK(dedic_gpio_new_bundle(&cfg, &bundle_));
    ESP_ERROR_CHECK(dedic_gpio_get_out_mask(bundle_, &out_mask_));
    if (bidirectional)
    {
      ESP_ERROR_CHECK(dedic_gpio_get_in_mask(bundle_, &in_mask_));
    }

    // Keep the CPU data route installed and gate the pad with GPIO_ENABLE only,
    // so a direction change is one register write (same trick RMLink uses).
    uint32_t sel = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + gpio_ * 4);
    sel = (sel | GPIO_FUNC0_OEN_SEL) & ~GPIO_FUNC0_OEN_INV_SEL;
    REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + gpio_ * 4, sel);
    OutputEnable(true);
    // 0 = weakest (~5 mA) .. 3 = strongest (~40 mA); the IDF default is 2. Push-pull edges
    // from the default level ring on bare jumper wires and caused SWD ack errors, so the pads
    // are one step weaker (see the oscilloscope comparison in results-20261016.md).
    gpio_set_drive_capability(static_cast<gpio_num_t>(gpio_),
                              static_cast<gpio_drive_cap_t>(XRDAP_DRIVE_CAP));
  }

  bool Read() override
  {
    return (dedic_gpio_cpu_ll_read_in() & in_mask_) != 0U;
  }

  void Write(bool value) override
  {
    dedic_gpio_cpu_ll_write_mask(out_mask_, value ? out_mask_ : 0U);
    if (edge_spin_cycles_ != 0U)
    {
      // SWCLK pacing: hold each edge for a fixed number of CPU cycles. This replaces the
      // coarse BusyLoop half-period delay of SwdGeneralGPIO (one iteration is ~9 cycles plus
      // a call), so the clock is smooth from ~1 MHz up to the natural no-delay rate.
      const uint32_t t0 = esp_cpu_get_cycle_count();
      while ((esp_cpu_get_cycle_count() - t0) < edge_spin_cycles_)
      {
      }
    }
  }

  /// Dedicated-GPIO channel masks and the pad number, for hand-written fast paths.
  uint32_t OutMask() const { return out_mask_; }
  uint32_t InMask() const { return in_mask_; }
  int GpioNum() const { return gpio_; }
  void SetOutputEnable(bool on) { OutputEnable(on); }

  /// Cycles to wait after every edge written through this object (0 = no pacing).
  void SetEdgeSpinCycles(uint32_t cycles) { edge_spin_cycles_ = cycles; }

  LibXR::ErrorCode EnableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }
  LibXR::ErrorCode DisableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }

  /// Only the direction matters; pull is fixed at construction.
  LibXR::ErrorCode SetConfig(Configuration config) override
  {
    OutputEnable(config.direction != Direction::INPUT);
    return LibXR::ErrorCode::OK;
  }

 private:
  void OutputEnable(bool on)
  {
    if (on)
    {
      ::GPIO.enable_w1ts = 1U << gpio_;
    }
    else
    {
      ::GPIO.enable_w1tc = 1U << gpio_;
    }
  }

  int gpio_;
  dedic_gpio_bundle_handle_t bundle_ = nullptr;
  uint32_t out_mask_ = 0U;
  uint32_t in_mask_ = 0U;
  uint32_t edge_spin_cycles_ = 0U;
};

}  // namespace XrDap
