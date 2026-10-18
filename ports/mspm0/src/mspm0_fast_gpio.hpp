// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <ti/driverlib/dl_gpio.h>

#include <cstdint>

#include "gpio.hpp"

namespace XrDap
{

/// SWD 位带用的 MSPM0 GPIO，以 LibXR::GPIO 接口提供给 SwdGeneralGPIO。
/// 读写直接操作 DOUTSET/DOUTCLR/DIN，切换方向只写一次 DOESET/DOECLR；类是 final，
/// SwdGeneralGPIO 里的调用会被编译器内联，不走虚函数。
/// libxr 的 MSPM0GPIO::SetConfig 每次都重写 PINCM 并清输出，SWDIO 每个读写转换都要
/// 调用它，太慢，所以这里单独实现。
class Mspm0FastGpio final : public LibXR::GPIO
{
 public:
  /// 引脚设成 GPIO 功能、输入缓冲常开（输出时也能读回），初始为高电平的输入。
  Mspm0FastGpio(GPIO_Regs* port, uint32_t pin_mask, uint32_t pincm, bool pull_up)
      : port_(port), mask_(pin_mask)
  {
    DL_GPIO_initDigitalInputFeatures(
        pincm, DL_GPIO_INVERSION_DISABLE,
        pull_up ? DL_GPIO_RESISTOR_PULL_UP : DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    port_->DOUTSET31_0 = mask_;
    port_->DOECLR31_0 = mask_;
  }

  bool Read() override { return (port_->DIN31_0 & mask_) != 0U; }

  void Write(bool value) override
  {
    if (value)
    {
      port_->DOUTSET31_0 = mask_;
    }
    else
    {
      port_->DOUTCLR31_0 = mask_;
    }
  }

  LibXR::ErrorCode EnableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }
  LibXR::ErrorCode DisableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }

  /// 只处理方向；上下拉在构造时已固定（SwdGeneralGPIO 采样时要的上拉由构造参数给）。
  /// 开漏按推挽处理。
  LibXR::ErrorCode SetConfig(Configuration config) override
  {
    if (config.direction == Direction::INPUT)
    {
      port_->DOECLR31_0 = mask_;
    }
    else
    {
      port_->DOESET31_0 = mask_;
    }
    return LibXR::ErrorCode::OK;
  }

 private:
  GPIO_Regs* port_;
  uint32_t mask_;
};

/// 目标板 nRESET：模拟开漏。写 0 时输出低，写 1 时关闭输出、靠上拉回到高电平；
/// 读的是引脚实际电平（目标自己拉低复位时也能读到）。
class Mspm0OpenDrainGpio final : public LibXR::GPIO
{
 public:
  Mspm0OpenDrainGpio(GPIO_Regs* port, uint32_t pin_mask, uint32_t pincm)
      : port_(port), mask_(pin_mask)
  {
    DL_GPIO_initDigitalInputFeatures(pincm, DL_GPIO_INVERSION_DISABLE,
                                     DL_GPIO_RESISTOR_PULL_UP, DL_GPIO_HYSTERESIS_DISABLE,
                                     DL_GPIO_WAKEUP_DISABLE);
    port_->DOUTCLR31_0 = mask_;
    port_->DOECLR31_0 = mask_;
  }

  bool Read() override { return (port_->DIN31_0 & mask_) != 0U; }

  void Write(bool value) override
  {
    if (value)
    {
      port_->DOECLR31_0 = mask_;
    }
    else
    {
      port_->DOUTCLR31_0 = mask_;
      port_->DOESET31_0 = mask_;
    }
  }

  LibXR::ErrorCode EnableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }
  LibXR::ErrorCode DisableInterrupt() override { return LibXR::ErrorCode::NOT_SUPPORT; }

  LibXR::ErrorCode SetConfig(Configuration config) override
  {
    (void)config;
    return LibXR::ErrorCode::OK;
  }

 private:
  GPIO_Regs* port_;
  uint32_t mask_;
};

}  // namespace XrDap
