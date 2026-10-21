// SPDX-License-Identifier: Apache-2.0
// XR-DAP 的 ESP32-C3 版：基于 libxr 的 CMSIS-DAP 调试探针。
//   串口：芯片自带的 USB Serial/JTAG（电脑上是 CDC 串口），用 libxr Topic 帧提供
//         CMSIS-DAP，配合电脑端 xrdap 使用（xrdap share 再转给其他调试工具）；同一个口
//         也是 Wi-Fi 命令行（serial_mux.hpp）。
//   Wi-Fi：连上热点后提供 OpenOCD 的 TCP 后端和 USB/IP 虚拟 USB 探针，与 S3 版相同。
// 两种传输共用同一个 DAP 实例和 XR 厂商扩展（多地址读写、指针链、探针端采样）。C3 没有
// 能做自定义 USB 设备的 USB 控制器，所以没有 S3 版的 USB CMSIS-DAP 接口。与 S3 版共用的
// 源文件在 firmware/common 下。
//
// XR-DAP for the ESP32-C3: a CMSIS-DAP debug probe built on libxr.
//   Serial: the built-in USB Serial/JTAG (a CDC port on the PC) carries CMSIS-DAP in
//           libxr Topic frames for the PC-side xrdap (xrdap share passes it on to other
//           debug tools); the same port is also the Wi-Fi console (serial_mux.hpp).
//   Wi-Fi:  once joined, serves the OpenOCD TCP backend and a USB/IP virtual USB probe,
//           as on the S3.
// Both transports share one DAP instance and the XR vendor extension (multi-address
// access, pointer chains, probe-side sampling). The C3 has no USB controller usable for
// custom USB devices, so it lacks the S3's USB CMSIS-DAP interface. Sources shared with
// the S3 build live in firmware/common.
#include <cstdint>

#include "config.hpp"
#include "dap_vendor.hpp"
#include "daplink_v2_profile_swd.hpp"
#include "debug/dap_serial_transport.hpp"
#include "debug/swd_general_gpio.hpp"
#include "dedic_gpio_pin.hpp"
#include "esp_cdc_jtag.hpp"
#include "esp_log.h"
#include "esp_timebase.hpp"
#include "esp_uart.hpp"
#include "libxr.hpp"
#include "probe_app.hpp"
#include "restart.hpp"
#include "serial_link.hpp"
#include "serial_mux.hpp"
#include "swd_clock.hpp"
#include "uart_console.hpp"

namespace
{
using namespace XrDap;

constexpr const char* TAG = "xrdap";

using SwdLink = LibXR::Debug::SwdGeneralGPIO<DedicGpioPin, DedicGpioPin>;
// DAP 类本身是 USB 设备类，这里不挂到 USB，只用 ExecuteExternal() 执行命令，所以不需要
// S3 版循环调用 Step() 的线程；端点号用不到，随便填。
using Swd = SwdWithClock<DedicGpioPin>;
using Dap = LibXR::USB::DapLinkV2Class<Swd, Config::kDapPacketSize,
                                       Config::kDapPacketCount, Config::kDapPacketSize>;

struct SwdSet
{
  DedicGpioPin swclk{Config::kSwclkGpio, /*bidirectional=*/false, /*pull_up=*/false,
                     Config::kPadDriveStrength};
  DedicGpioPin swdio{Config::kSwdioGpio, /*bidirectional=*/true, /*pull_up=*/true,
                     Config::kPadDriveStrength};
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
  (void)RestartInit();  // 失败时 RestartLater 立即重启 / RestartLater restarts at once on
                        // failure

  // 单核芯片，专用 GPIO 在哪个任务里创建都可以用。
  static SwdSet swd_set;
  static Dap dap(LibXR::USB::Endpoint::EPNumber::EP1, LibXR::USB::Endpoint::EPNumber::EP1,
                 swd_set.swd, nullptr);

  // XR 厂商扩展挂在 DAP 类的命令钩子上，串口、网络行为一致。
  static DapVendor vendor(swd_set.swd, Config::kDapPacketSize);
  InstallVendor(dap, vendor, Config::kProductName, Config::kCore,
                Config::kSamplingPriority);

  // USB Serial/JTAG 上同时跑 DAP 链路和 Wi-Fi 命令行，自动区分 DAP 帧和人打的命令。
  // 控制台不能用这个口（sdkconfig 里控制台已关掉）。
  using SerialAdapter = SerialDap<Dap>;
  using SerialLink = LibXR::DapSerialTransport<SerialAdapter, Config::kDapPacketSize>;
  static LibXR::ESP32CDCJtag usj(
      /*rx_buffer_size=*/8192, /*tx_buffer_size=*/4096, /*tx_queue_size=*/5,
      LibXR::UART::Configuration{Config::kSerialBaud, LibXR::UART::Parity::NO_PARITY, 8,
                                 1});
  static SerialAdapter adapter(dap, vendor);
  static SerialLink link(adapter, usj);
  static SerialMux<SerialLink, LibXR::ESP32CDCJtag> mux(link, usj);
  mux.Start(Config::kCore, Config::kSerialPriority);
  ESP_LOGI(TAG, "USB Serial/JTAG 上的串口 DAP 链路已启动，SWCLK GPIO%d，SWDIO GPIO%d",
           Config::kSwclkGpio, Config::kSwdioGpio);

  // UART0 上也有一个命令行，给引出了 UART0 的开发板用。
  static LibXR::ESP32UART console_uart(
      static_cast<uart_port_t>(Config::kConsoleUart), Config::kConsoleTxGpio,
      Config::kConsoleRxGpio, LibXR::ESP32UART::PIN_NO_CHANGE,
      LibXR::ESP32UART::PIN_NO_CHANGE,
      /*rx_buffer_size=*/256, /*tx_buffer_size=*/1024, /*tx_queue_size=*/8,
      LibXR::UART::Configuration{Config::kConsoleBaud, LibXR::UART::Parity::NO_PARITY, 8,
                                 1},
      /*enable_dma=*/false);
  static UartConsole<LibXR::ESP32UART> console(console_uart);
  console.Start(Config::kCore);

  StartNetwork(
      dap, vendor,
      {Config::kChipName, Config::kUsbProductName, Config::kCore, nullptr, nullptr});
}
