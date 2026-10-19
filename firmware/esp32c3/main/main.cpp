// SPDX-License-Identifier: Apache-2.0
// XR-DAP 的 ESP32-C3 版：基于 libxr 的 CMSIS-DAP 调试探针。
//   串口：芯片自带的 USB Serial/JTAG（电脑上是 CDC 串口），用 libxr Topic 帧提供
//         CMSIS-DAP，配合电脑端 xrdap-forward 给 OpenOCD 用；同一个口也是 Wi-Fi
//         命令行（serial_mux.hpp）。
//   Wi-Fi：连上热点后在 TCP 端口提供 CMSIS-DAP，帧格式与 S3 版相同。
// 两种传输共用同一个 DAP 实例和 XR 厂商扩展（多地址读写、指针链、探针端采样）。C3 没有
// 能做自定义 USB 设备的 USB 控制器，所以没有 S3 版的 USB CMSIS-DAP 接口。与 S3 版共用的
// 源文件在 firmware/common 下。
//
// XR-DAP for the ESP32-C3: a CMSIS-DAP debug probe built on libxr.
//   Serial: the built-in USB Serial/JTAG (a CDC port on the PC) carries CMSIS-DAP in
//           libxr Topic frames for OpenOCD via the PC-side xrdap-forward; the same port
//           is also the Wi-Fi console (serial_mux.hpp).
//   Wi-Fi:  once joined, CMSIS-DAP is served on a TCP port with the same framing as S3.
// Both transports share one DAP instance and the XR vendor extension (multi-address
// access, pointer chains, probe-side sampling). The C3 has no USB controller usable for
// custom USB devices, so it lacks the S3's USB CMSIS-DAP interface. Sources shared with
// the S3 build live in firmware/common.
#include <cstdint>

#include "config.hpp"
#include "dap_tcp.hpp"
#include "dap_vendor.hpp"
#include "daplink_v2_profile_swd.hpp"
#include "debug/dap_serial_transport.hpp"
#include "debug/swd_general_gpio.hpp"
#include "dedic_gpio_pin.hpp"
#include "esp_cdc_jtag.hpp"
#include "esp_log.h"
#include "esp_timebase.hpp"
#include "esp_uart.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libxr.hpp"
#include "mdns_service.hpp"
#include "probe_info.hpp"
#include "serial_link.hpp"
#include "serial_mux.hpp"
#include "swd_link.hpp"
#include "wifi_command.hpp"
#include "wifi_console.hpp"
#include "wifi_sta.hpp"

namespace
{
using namespace XrDap;

constexpr const char* TAG = "xrdap";

using SwdLink = LibXR::Debug::SwdGeneralGPIO<DedicGpioPin, DedicGpioPin>;
// DAP 类本身是 USB 设备类，这里不挂到 USB，只用 ExecuteExternal() 执行命令，所以不需要
// S3 版循环调用 Step() 的线程；端点号用不到，随便填。
using Dap = LibXR::USB::DapLinkV2Class<SwdWithClock, Config::kDapPacketSize, 4u,
                                       Config::kDapPacketSize>;

struct SwdSet
{
  DedicGpioPin swclk{Config::kSwclkGpio, /*bidirectional=*/false, /*pull_up=*/false,
                     Config::kPadDriveStrength};
  DedicGpioPin swdio{Config::kSwdioGpio, /*bidirectional=*/true, /*pull_up=*/true,
                     Config::kPadDriveStrength};
  // 延时参数填 0：SwdGeneralGPIO 走全速路径，SWCLK 频率由 swd_link.hpp 里设定。
  SwdLink link{swclk, swdio, 0u};
  SwdWithClock swd{link, swclk, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U,
                   Config::kEdgeWriteCycles};
};

// TCP 客户端连上时，串口发起的采样作废；客户端走后，TCP 发起的采样停止。
struct TcpHandover
{
  static void OnConnect(void* ctx)
  {
    auto* vendor = static_cast<DapVendor*>(ctx);
    vendor->OnTransportClosed();
    vendor->SetTransport(DapVendor::Transport::TCP);
  }

  static void OnDisconnect(void* ctx)
  {
    static_cast<DapVendor*>(ctx)->OnTransportClosed();
  }
};
}  // namespace

extern "C" void app_main(void)
{
  static LibXR::ESP32Timebase timebase;
  LibXR::PlatformInit(Config::kLibxrTimerPriority, 4096);

  // 单核芯片，专用 GPIO 在哪个任务里创建都可以用。
  static SwdSet swd_set;
  static Dap dap(LibXR::USB::Endpoint::EPNumber::EP1, LibXR::USB::Endpoint::EPNumber::EP1,
                 swd_set.swd, nullptr);

  // DAP_Info 里报探针的序列号（前缀 + 出厂 MAC），上位机靠它认出同一块探针。
  SetProbeInfo(dap, "XR-DAP-C3");

  // XR 厂商扩展（0x80 到 0x85，加 0x86 Wi-Fi 配置）挂在 DAP 类的命令钩子上，串口、TCP
  // 行为一致。
  static DapVendor vendor(swd_set.swd, Config::kDapPacketSize);
  WifiCommand::Install(vendor);
  vendor.Begin(Config::kCore, Config::kSamplingPriority);
  dap.SetCommandHook(&DapVendor::Hook, &vendor);

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

  // Wi-Fi 和 TCP。Wi-Fi 最多等 20 秒；没连上时 Wi-Fi 仍会在后台重连，TCP 服务照常启动。
  // 没有热点参数时 Wi-Fi 不启动，只提供串口。
  const esp_err_t wifi = WifiStaStart(20000);
  // UART0 上也有一个命令行，给引出了 UART0 的开发板用。
  static LibXR::ESP32UART console_uart(
      static_cast<uart_port_t>(Config::kConsoleUart), Config::kConsoleTxGpio,
      Config::kConsoleRxGpio, LibXR::ESP32UART::PIN_NO_CHANGE,
      LibXR::ESP32UART::PIN_NO_CHANGE,
      /*rx_buffer_size=*/256, /*tx_buffer_size=*/1024, /*tx_queue_size=*/8,
      LibXR::UART::Configuration{Config::kConsoleBaud, LibXR::UART::Parity::NO_PARITY, 8,
                                 1},
      /*enable_dma=*/false);
  static WifiConsole<LibXR::ESP32UART> console(console_uart);
  console.Start(Config::kCore);
  if (wifi == ESP_OK || wifi == ESP_ERR_TIMEOUT)
  {
    static DapTcpServer<Dap> server(dap, Config::kTcpPort);
    server.SetHooks(&TcpHandover::OnConnect, &TcpHandover::OnDisconnect, &vendor);
    server.Start(Config::kCore);
    MdnsStart("esp32c3", Config::kTcpPort);
    ESP_LOGI(TAG, "TCP 服务已启动，端口 %u，剩余堆 %u 字节", (unsigned)Config::kTcpPort,
             (unsigned)esp_get_free_heap_size());
  }
  else
  {
    ESP_LOGW(TAG, "Wi-Fi 未启动（%s），只提供串口", esp_err_to_name(wifi));
  }
}
