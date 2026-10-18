// SPDX-License-Identifier: Apache-2.0
// XR-DAP-MSPM0：基于 libxr 的 MSPM0G3507 CMSIS-DAP 调试探针（LP-MSPM0G3507）。
//   MSPM0G3507 没有 USB，DAP 走 UART0：用 libxr Topic 帧承载 CMSIS-DAP（请求 topic
//   dap_req、响应 dap_resp，载荷为 u16 长度 + DAP 包），经 XDS110 的反向通道串口到电脑，
//   电脑上的 xrdap-forward 转成 TCP 给 OpenOCD 的 cmsis-dap tcp 后端。
//   SWD 用两个 GPIO 位带（libxr SwdGeneralGPIO），nRESET 用一个模拟开漏的 GPIO。
//   裸机（libxr system none），没有 RTOS：主循环就是串口传输的轮询循环。
//   ESP32-S3 版的 XR 厂商扩展（0x80 到 0x85，依赖 FreeRTOS 任务和 esp_timer）没有移植。
#include <ti/driverlib/m0p/dl_factoryregion.h>

#include <cstdint>

#include "config.hpp"
#include "daplink_v2_profile_swd.hpp"
#include "debug/dap_serial_transport.hpp"
#include "debug/swd_general_gpio.hpp"
#include "libxr.hpp"
#include "mspm0_fast_gpio.hpp"
#include "mspm0_timebase.hpp"
#include "mspm0_uart.hpp"
#include "ti_msp_dl_config.h"

namespace
{
using namespace XrDap;

using SwdLink = LibXR::Debug::SwdGeneralGPIO<Mspm0FastGpio, Mspm0FastGpio>;
using Dap = LibXR::USB::DapLinkV2Class<SwdLink, Config::kDapPacketSize, Config::kDapPacketCount,
                                       Config::kDapPacketSize, Config::kDapQueuedRequestBuffer>;
using SerialLink = LibXR::DapSerialTransport<Dap, Config::kDapPacketSize>;

// UART 的 DMA 缓冲。发送是两个半区，接收是 DMA 循环环。
alignas(8) uint8_t uart_tx_storage[2U * Config::kUartTxHalf];
alignas(8) uint8_t uart_rx_ring[Config::kUartRxRing];

// DAP_Info 序列号：前缀 + 8 位十六进制 TRACEID。
char serial_string[sizeof(Config::kSerialPrefix) + 8U];

void MakeSerialString(uint32_t trace_id)
{
  constexpr char kHex[] = "0123456789ABCDEF";
  size_t pos = 0U;
  for (; Config::kSerialPrefix[pos] != '\0'; ++pos)
  {
    serial_string[pos] = Config::kSerialPrefix[pos];
  }
  for (int shift = 28; shift >= 0; shift -= 4)
  {
    serial_string[pos++] = kHex[(trace_id >> shift) & 0xFU];
  }
  serial_string[pos] = '\0';
}

// UART0 的资源绑定：发送 DMA 通道 3（基本通道），接收 DMA 通道 0（完整通道，循环模式，
// 开半满中断）。模板参数里的检查和 MSPM0_UART_EXTEND_INIT 宏相同。
LibXR::MSPM0UART::Resources Uart0Resources()
{
  return LibXR::Detail::MakeMSPM0ExtendUartResources<
      UART_0_INST_INT_IRQN, DMA_CH_UART0_TX_CHAN_ID, UART_0_INST_INT_IRQN, true,
      DMA_CH_UART0_RX_CHAN_ID, UART_0_INST_INT_IRQN, true,
      /*ExtendCapable=*/true, /*FullRxChannel=*/DMA_CH_UART0_RX_CHAN_ID < DMA_SYS_N_DMA_FULL_CHANNEL,
      /*HalfRxInterrupt=*/true>(UART_0_INST, UART_0_INST_FREQUENCY,
                                static_cast<uint8_t>(DMA_UART0_TX_TRIG),
                                static_cast<uint8_t>(DMA_UART0_RX_TRIG));
}
}  // namespace

// 共享的 DMA 中断：只有 UART0 的接收通道开了 DMA 中断（发送完成走 UART 中断）。
extern "C" void DMA_IRQHandler(void) { LibXR::MSPM0UART::OnDmaInterrupt(); }

int main(void)
{
  // 时钟切到 PLL 之前读 FACTORYREGION（MSPM0Gx51x 的 FLASH_ERR_01 在 2 等待周期下读
  // 出错；G3507 不在勘误范围，按同样的顺序做没有坏处）。
  const uint32_t trace_id = DL_FactoryRegion_getTraceID();

  SYSCFG_DL_init();

  static LibXR::MSPM0Timebase timebase;
  LibXR::PlatformInit();

  MakeSerialString(trace_id);

  // SWD 引脚：SWCLK 推挽输出（构造后由 SwdGeneralGPIO 设成输出），SWDIO 带上拉，
  // nRESET 模拟开漏。
  static Mspm0FastGpio swclk(GPIO_SWD_PORT, GPIO_SWD_SWCLK_PIN, GPIO_SWD_SWCLK_IOMUX, false);
  static Mspm0FastGpio swdio(GPIO_SWD_PORT, GPIO_SWD_SWDIO_PIN, GPIO_SWD_SWDIO_IOMUX, true);
  static Mspm0OpenDrainGpio nreset(GPIO_SWD_PORT, GPIO_SWD_NRESET_PIN, GPIO_SWD_NRESET_IOMUX);
  static SwdLink swd(swclk, swdio, Config::kSwdLoopsPerUs);

  // DAP 实例只用 ExecuteExternal（同步执行一条命令），不绑定 USB；端点号不会被用到。
  static Dap dap(LibXR::USB::Endpoint::EPNumber::EP1, LibXR::USB::Endpoint::EPNumber::EP1, swd,
                 &nreset);
  dap.SetInfoStrings({"XRobot", "XR-DAP-MSPM0 CMSIS-DAP", serial_string, "2.0.0", nullptr,
                      nullptr, "TI", "LP-MSPM0G3507", "0.1.0"});

  static LibXR::MSPM0UART uart(
      Uart0Resources(), LibXR::RawData{uart_tx_storage, sizeof(uart_tx_storage)},
      LibXR::RawData{uart_rx_ring, sizeof(uart_rx_ring)}, Config::kUartTxQueueOps,
      Config::kUartRxQueue,
      LibXR::UART::Configuration{Config::kSerialBaud, LibXR::UART::Parity::NO_PARITY, 8U, 1U});

  // 接收 DMA 的中断和 UART 中断同一优先级（驱动要求，低于 SysTick）。
  NVIC_SetPriority(DMA_INT_IRQn, 1U);
  NVIC_EnableIRQ(DMA_INT_IRQn);

  // system none 下 Start() 直接在当前上下文里跑传输循环，不返回。
  static SerialLink link(dap, uart);
  (void)link.Start();

  for (;;)
  {
  }
}
