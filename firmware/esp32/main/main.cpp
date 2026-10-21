// SPDX-License-Identifier: Apache-2.0
// XR-DAP 的传统 ESP32 版：基于 libxr 的 CMSIS-DAP 调试探针。
//   串口：UART0 接板上的 USB 转串口芯片，用 libxr Topic 帧提供 CMSIS-DAP，配合电脑端
//         xrdap 使用；同一个口也是 Wi-Fi 命令行（serial_mux.hpp）。
//   Wi-Fi：连上热点后提供 OpenOCD 的 TCP 后端和 USB/IP 虚拟 USB 探针，与 S3、C3 相同。
// 两种传输经 XRDAP-Core 的同一个执行器驱动 DAP，同一时间只有一条传输拥有探针。传统
// ESP32 没有 USB 控制器，也没有 CPU 专用 GPIO：SWD 经 GPIO 寄存器驱动（RegGpioPin）。
//
// XR-DAP for the classic ESP32: a CMSIS-DAP debug probe built on libxr.
//   Serial: UART0, wired to the on-board USB-UART chip, carries CMSIS-DAP in libxr Topic
//           frames for the PC-side xrdap; the same port is also the Wi-Fi console
//           (serial_mux.hpp).
//   Wi-Fi:  once joined, serves the OpenOCD TCP backend and a USB/IP virtual USB probe,
//           as on the S3 and C3.
// Both transports drive DAP through XRDAP-Core's one executor; one transport owns the
// probe at a time. The classic ESP32 has no USB controller and no CPU dedicated GPIO: SWD
// is driven through the GPIO registers (RegGpioPin).
#include <cstdint>

#include "config.hpp"
#include "daplink_v2_profile_swd.hpp"
#include "debug/swd_general_gpio.hpp"
#include "esp_timebase.hpp"
#include "esp_uart.hpp"
#include "libxr.hpp"
#include "probe_app.hpp"
#include "reg_gpio_pin.hpp"
#include "restart.hpp"
#include "serial_mux.hpp"
#include "swd_clock.hpp"
#include "xrdap/serial/serial_link.hpp"

namespace
{
using namespace XrDap;

using SwdLink = LibXR::Debug::SwdGeneralGPIO<RegGpioPin, RegGpioPin>;
using Swd = SwdWithClock<RegGpioPin>;
// DAP 类本身是 USB 设备类，这里不挂到 USB，只用 ExecuteExternal()
// 执行命令；端点号用不到。
using DapLink =
    LibXR::USB::DapLinkV2Class<Swd, Config::kDapPacketSize, Config::kDapPacketCount,
                               Config::kDapPacketSize>;

struct SwdSet
{
  RegGpioPin swclk{Config::kSwclkGpio, /*pull_up=*/false, Config::kPadDriveStrength};
  RegGpioPin swdio{Config::kSwdioGpio, /*pull_up=*/true, Config::kPadDriveStrength};
  // 延时参数填 0：SwdGeneralGPIO 走全速路径，SWCLK 频率由 swd_clock.hpp 里设定。
  SwdLink link{swclk, swdio, 0u};
  Swd swd{link, swclk, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U,
          Config::kEdgeWriteCycles};
};

}  // namespace

extern "C" void app_main(void)
{
  static LibXR::ESP32Timebase timebase;
  LibXR::PlatformInit(Config::kLibxrTimerPriority, 4096);
  // 失败时 RestartLater 立即重启 / RestartLater restarts at once on failure.
  (void)RestartInit();
  // NVS 在命令行、0x86、状态灯之前初始化一次；失败时它们报存储错误 / NVS is set up once
  // before the console, 0x86 and the status light; on failure they report storage
  // errors.
  (void)WifiStaInitStorage();

  // GPIO 寄存器在哪个核上都能访问，SWD 对象直接在这里创建。
  static SwdSet swd_set;
  static DapLink dap(LibXR::USB::Endpoint::EPNumber::EP1,
                     LibXR::USB::Endpoint::EPNumber::EP1, swd_set.swd, nullptr);

  // 会话主人、厂商扩展和执行器；串口和网络都经执行器驱动 DAP / Session owner, vendor
  // extension and executor; serial and network both drive DAP through the executor.
  static ProbeCore<DapLink> probe(dap, swd_set.swd);
  InstallProbe(probe, dap, WifiStaSettings(), Config::kProductName, Config::kSwdCore,
               Config::kSamplingPriority);

  // UART0 上同时跑 DAP 链路和 Wi-Fi 命令行，自动区分 DAP 帧和人打的命令。
  using Link = SerialLink<Executor<DapLink>, Config::kDapPacketSize>;
  static LibXR::ESP32UART uart(
      static_cast<uart_port_t>(Config::kSerialUart), Config::kSerialTxGpio,
      Config::kSerialRxGpio, LibXR::ESP32UART::PIN_NO_CHANGE,
      LibXR::ESP32UART::PIN_NO_CHANGE,
      /*rx_buffer_size=*/8192, /*tx_buffer_size=*/4096, /*tx_queue_size=*/5,
      LibXR::UART::Configuration{Config::kSerialBaud, LibXR::UART::Parity::NO_PARITY, 8,
                                 1},
      /*enable_dma=*/false);
  static Link link(probe.exec, uart);
  static SerialMux<Link, LibXR::ESP32UART> mux(link, uart, WifiStaSettings());
  mux.Start(Config::kSwdCore, Config::kSerialPriority);

  (void)StartNetwork(
      probe, {Config::kChipName, Config::kUsbProductName, Config::kSwdCore, nullptr});
}
