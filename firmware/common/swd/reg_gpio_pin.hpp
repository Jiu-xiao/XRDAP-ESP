// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_cpu.h"
#include "gpio.hpp"
#include "soc/gpio_reg.h"

namespace XrDap
{

// 直接读写 GPIO 寄存器的引脚，供 SwdGeneralGPIO 使用。给没有 CPU 专用 GPIO 的芯片
// （传统 ESP32）用；接口与 DedicGpioPin 相同。
//
// A pin driven through the GPIO registers directly, for use by SwdGeneralGPIO. For chips
// without CPU dedicated GPIO (the classic ESP32); same interface as DedicGpioPin.

/// GPIO 寄存器直写的引脚，以 LibXR::GPIO 接口提供 / Pin driven through the GPIO
/// registers, exposed through the LibXR::GPIO interface.
/// 写是一次外设总线写（GPIO_OUT_W1TS/W1TC），读是一次外设总线读（GPIO_IN），比专用 GPIO
/// 慢，但任何核都能访问。只支持 GPIO0 到 GPIO31。
/// A write is one peripheral-bus write (GPIO_OUT_W1TS/W1TC) and a read one peripheral-bus
/// read (GPIO_IN): slower than dedicated GPIO, but usable from any core. Only GPIO0 to
/// GPIO31 are supported.
class RegGpioPin final : public LibXR::GPIO
{
 public:
  /// @brief 配置引脚为 GPIO 输入输出 / Configure the pin as GPIO input/output.
  /// @param gpio_num GPIO 号，0 到 31 / GPIO number, 0 to 31.
  /// @param pull_up 是否使能上拉 / Enable the pull-up.
  /// @param drive_strength 驱动强度，0 最弱，3 最强 / Drive strength, 0 weakest to 3
  ///        strongest.
  RegGpioPin(int gpio_num, bool pull_up, int drive_strength) : mask_(1U << gpio_num)
  {
    const auto pin = static_cast<gpio_num_t>(gpio_num);
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_pull_mode(pin, pull_up ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
    gpio_set_drive_capability(pin, static_cast<gpio_drive_cap_t>(drive_strength));
    OutputEnable(true);
  }

  /// 读引脚电平 / Read the pin level.
  bool Read() override { return (REG_READ(GPIO_IN_REG) & mask_) != 0U; }

  /// 写引脚电平；设置了 edge_spin_cycles 时写完后忙等 / Write the level; busy-waits
  /// afterwards when edge spin cycles are set.
  void Write(bool value) override
  {
    REG_WRITE(value ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, mask_);
    if (edge_spin_cycles_ != 0U)
    {
      // 每个沿之后等固定的 CPU 周期数，用来设定 SWCLK 频率。
      const uint32_t t0 = esp_cpu_get_cycle_count();
      while ((esp_cpu_get_cycle_count() - t0) < edge_spin_cycles_)
      {
      }
    }
  }

  /// 每次写沿之后等待的 CPU 周期数，0 表示不等待（全速） / CPU cycles to wait after each
  /// edge write; 0 means no wait (full speed).
  void SetEdgeSpinCycles(uint32_t cycles) { edge_spin_cycles_ = cycles; }

  /// 不支持中断 / Interrupts are not supported.
  LibXR::ErrorCode EnableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }
  /// 不支持中断 / Interrupts are not supported.
  LibXR::ErrorCode DisableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }

  /// 只处理方向，上下拉在构造时已固定 / Only the direction is applied; pull mode is fixed
  /// at construction.
  LibXR::ErrorCode SetConfig(Configuration config) override
  {
    OutputEnable(config.direction != Direction::INPUT);
    return LibXR::ErrorCode::OK;
  }

 private:
  void OutputEnable(bool on)
  {
    if (on == output_enabled_)
    {
      return;
    }
    output_enabled_ = on;
    REG_WRITE(on ? GPIO_ENABLE_W1TS_REG : GPIO_ENABLE_W1TC_REG, mask_);
    // 写进外设总线的写缓冲后要一段时间才生效；读回同一外设的寄存器会等写完成，之后引脚才
    // 真正切换了方向。
    (void)REG_READ(GPIO_ENABLE_REG);
  }

  uint32_t mask_;
  bool output_enabled_ = false;
  uint32_t edge_spin_cycles_ = 0U;
};

}  // namespace XrDap
