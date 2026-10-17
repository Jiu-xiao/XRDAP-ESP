// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "config.hpp"
#include "driver/dedic_gpio.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "gpio.hpp"
#include "hal/dedic_gpio_cpu_ll.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_reg.h"

namespace XrDap
{

/// ESP32-S3 的 CPU 专用 GPIO，以 LibXR::GPIO 接口提供给 SwdGeneralGPIO。
/// 读写都是单条 CPU 指令，比走 GPIO 矩阵寄存器快得多。
/// 专用 GPIO 只能在创建它的那个核上使用。
class S3DedicGpio final : public LibXR::GPIO
{
 public:
  /// bidirectional 为 true 时保留输入通路，Read() 才有效。
  S3DedicGpio(int gpio_num, bool bidirectional, bool pull_up) : gpio_(gpio_num)
  {
    gpio_reset_pin(static_cast<gpio_num_t>(gpio_));
    gpio_set_direction(static_cast<gpio_num_t>(gpio_), GPIO_MODE_INPUT_OUTPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(gpio_), pull_up ? GPIO_PULLUP_ONLY : GPIO_FLOATING);

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

    // CPU 数据通路一直保留，只用 GPIO_ENABLE 寄存器控制引脚方向，
    // 这样切换输入输出只需写一次寄存器。
    uint32_t sel = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + gpio_ * 4);
    sel = (sel | GPIO_FUNC0_OEN_SEL) & ~GPIO_FUNC0_OEN_INV_SEL;
    REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + gpio_ * 4, sel);
    OutputEnable(true);

    gpio_set_drive_capability(static_cast<gpio_num_t>(gpio_),
                              static_cast<gpio_drive_cap_t>(Config::kPadDriveStrength));
  }

  bool Read() override { return (dedic_gpio_cpu_ll_read_in() & in_mask_) != 0U; }

  void Write(bool value) override
  {
    dedic_gpio_cpu_ll_write_mask(out_mask_, value ? out_mask_ : 0U);
    if (edge_spin_cycles_ != 0U)
    {
      // 每个沿之后等固定的 CPU 周期数，用来设定 SWCLK 频率。
      const uint32_t t0 = esp_cpu_get_cycle_count();
      while ((esp_cpu_get_cycle_count() - t0) < edge_spin_cycles_)
      {
      }
    }
  }

  /// 每次写沿之后等待的 CPU 周期数，0 表示不等待（全速）。
  void SetEdgeSpinCycles(uint32_t cycles) { edge_spin_cycles_ = cycles; }

  LibXR::ErrorCode EnableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }
  LibXR::ErrorCode DisableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }

  /// 只处理方向，上下拉在构造时已固定。
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
