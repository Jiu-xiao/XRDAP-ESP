// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "driver/dedic_gpio.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "gpio.hpp"
#include "hal/dedic_gpio_cpu_ll.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_reg.h"
#include "soc/soc_caps.h"

namespace XrDap
{

// 把一个引脚配成 CPU 专用 GPIO，读写是单条 CPU 指令，供 SwdGeneralGPIO 使用。
//
// Configures a pin as a CPU dedicated GPIO whose reads and writes are single CPU
// instructions, for use by SwdGeneralGPIO.

/// CPU 专用 GPIO，以 LibXR::GPIO 接口提供 / CPU dedicated GPIO exposed through the
/// LibXR::GPIO interface.
/// 读写都是单条 CPU 指令（S3 为 Xtensa GPIO 指令，C3 为 RISC-V CSR 读写），比走 GPIO 矩阵
/// 快得多。专用 GPIO 只能在创建它的核上访问，对象需在目标核的任务里构造。
/// Reads and writes are single CPU instructions (Xtensa GPIO ops on S3, RISC-V CSR
/// accesses on C3), much faster than the GPIO matrix. A dedicated GPIO is only accessible
/// from the core that created it, so construct the object in a task on the target core.
class DedicGpioPin final : public LibXR::GPIO
{
 public:
  /// @brief 配置引脚并创建专用 GPIO 通路，失败时 ESP_ERROR_CHECK 中止 / Configure the pin
  ///        and create the dedicated path; aborts via ESP_ERROR_CHECK on failure.
  /// @param gpio_num GPIO 号 / GPIO number.
  /// @param bidirectional 为 true 时保留输入通路，Read() 才有效 / Keep the input path so
  ///        Read() is valid.
  /// @param pull_up 是否使能上拉 / Enable the pull-up.
  /// @param drive_strength 驱动强度，0 最弱，3 最强 / Drive strength, 0 weakest to 3
  ///        strongest.
  DedicGpioPin(int gpio_num, bool bidirectional, bool pull_up, int drive_strength)
      : gpio_(gpio_num), enable_bit_(1U << (gpio_num % 32))
  {
#if SOC_GPIO_PIN_COUNT > 32
    if (gpio_num >= 32)
    {
      // GPIO32 以后的输出使能在第二组寄存器里
      enable_set_ = GPIO_ENABLE1_W1TS_REG;
      enable_clr_ = GPIO_ENABLE1_W1TC_REG;
      enable_ = GPIO_ENABLE1_REG;
    }
#endif
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

    // CPU 数据通路一直保留，只用 GPIO_ENABLE 寄存器控制引脚方向，
    // 这样切换输入输出只需写一次寄存器。OEN_SEL 置 1 表示输出使能取自 GPIO_ENABLE，
    // 不取外设信号；三种芯片上这一位的含义相同，位号不同，用宏取。
    uint32_t sel = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + gpio_ * 4);
    sel = (sel | GPIO_FUNC0_OEN_SEL) & ~GPIO_FUNC0_OEN_INV_SEL;
    REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + gpio_ * 4, sel);
    OutputEnable(true);

    gpio_set_drive_capability(static_cast<gpio_num_t>(gpio_),
                              static_cast<gpio_drive_cap_t>(drive_strength));
  }

  /// 读引脚电平，仅 bidirectional 时有效 / Read the pin level; valid only when
  /// bidirectional.
  bool Read() override { return (dedic_gpio_cpu_ll_read_in() & in_mask_) != 0U; }

  /// 写引脚电平 / Write the level.
  void Write(bool value) override
  {
    dedic_gpio_cpu_ll_write_mask(out_mask_, value ? out_mask_ : 0U);
  }

  /// 专用 GPIO 输出寄存器中本引脚的位，供 SwdEngine 一条指令同时写两个引脚 / Bit of this
  /// pin in the dedicated-GPIO output register, so SwdEngine can drive two pins with one
  /// instruction.
  uint32_t OutMask() const { return out_mask_; }
  /// 专用 GPIO 输入寄存器中本引脚的位，非 bidirectional 时为 0 / Bit of this pin in the
  /// dedicated-GPIO input register; 0 when not bidirectional.
  uint32_t InMask() const { return in_mask_; }

  /// 打开或关闭输出驱动；写 GPIO_ENABLE 后约十几个 CPU 周期才生效 / Enable or disable the
  /// output driver; takes about a dozen CPU cycles to take effect after the
  /// GPIO_ENABLE write.
  void SetOutputEnable(bool on) { OutputEnable(on); }

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
      return;  // 已经是这个状态，省掉一次外设总线写加读回
    }
    output_enabled_ = on;
    REG_WRITE(on ? enable_set_ : enable_clr_, enable_bit_);
    // GPIO_ENABLE 在外设总线上，写先进写缓冲，总线忙时要几十个 CPU 周期才生效，此前引脚
    // 仍被上拉。读回同一外设的寄存器会等写完成，调用方再等一小段，引脚就切换好了。
    (void)REG_READ(enable_);
  }

  int gpio_;
  uint32_t enable_bit_;
  uint32_t enable_set_ = GPIO_ENABLE_W1TS_REG;
  uint32_t enable_clr_ = GPIO_ENABLE_W1TC_REG;
  uint32_t enable_ = GPIO_ENABLE_REG;
  bool output_enabled_ = false;
  dedic_gpio_bundle_handle_t bundle_ = nullptr;
  uint32_t out_mask_ = 0U;
  uint32_t in_mask_ = 0U;
};

}  // namespace XrDap
