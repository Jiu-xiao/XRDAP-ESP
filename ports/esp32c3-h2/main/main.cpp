// SPDX-License-Identifier: Apache-2.0
// XR-DAP 的 ESP32-C3 / ESP32-H2 版：基于 libxr 的 CMSIS-DAP 调试探针。
//   串口：芯片自带的 USB Serial/JTAG（电脑上是 CDC 串口），用 libxr Topic 帧提供 CMSIS-DAP，
//         配合电脑端的 xrdap-forward 给 OpenOCD 用。两种芯片都有。
//   Wi-Fi：只有 C3。连上热点后在 TCP 端口提供 CMSIS-DAP，帧格式与 S3 版相同；
//         热点参数用 UART0 上的命令行设置。
//   两种传输共用同一个 DAP 实例和 XR 厂商扩展（多地址读写、指针链、探针端采样）。
// 这两种芯片没有能做自定义 USB 设备的 USB 控制器，所以没有 S3 版的 USB CMSIS-DAP 接口。
// 与 S3 版共用的源文件在 firmware/main 下。
#include <cstdint>

#include "config.hpp"
#include "dap_vendor.hpp"
#include "daplink_v2_profile_swd.hpp"
#include "debug/dap_serial_transport.hpp"
#include "debug/swd_general_gpio.hpp"
#include "dedic_gpio_pin.hpp"
#include "esp_log.h"
#include "esp_timebase.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libxr.hpp"
#include "serial_link.hpp"
#include "swd_link.hpp"

#if CONFIG_IDF_TARGET_ESP32C3
#include "dap_tcp.hpp"
#include "esp_cdc_jtag.hpp"
#include "esp_uart.hpp"
#include "serial_mux.hpp"
#include "wifi_console.hpp"
#include "wifi_sta.hpp"
#elif CONFIG_IDF_TARGET_ESP32H2
#include "esp_cdc_jtag_h2.hpp"
#else
#error "这个工程只支持 ESP32-C3 和 ESP32-H2"
#endif

namespace
{
using namespace XrDap;

constexpr const char* TAG = "xrdap";

using SwdLink = LibXR::Debug::SwdGeneralGPIO<DedicGpioPin, DedicGpioPin>;
// DAP 类本身是 USB 设备类，这里不挂到 USB 上，只用它的 ExecuteExternal() 执行命令，
// 所以不需要 S3 版那个循环调用 Step() 的线程。端点号用不到，随便填。
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

#if CONFIG_IDF_TARGET_ESP32C3
// TCP 客户端连上时：之前串口发起的采样作废；客户端走后：TCP 发起的采样停止。
// 没有 USB，不需要 S3 版那样开关 USB。
struct TcpHandover
{
  static void OnConnect(void* ctx)
  {
    auto* vendor = static_cast<DapVendor*>(ctx);
    vendor->OnTransportClosed();
    vendor->SetTransport(DapVendor::Transport::TCP);
  }

  static void OnDisconnect(void* ctx) { static_cast<DapVendor*>(ctx)->OnTransportClosed(); }
};
#endif
}  // namespace

extern "C" void app_main(void)
{
  static LibXR::ESP32Timebase timebase;
  LibXR::PlatformInit(Config::kLibxrTimerPriority, 4096);

  // 单核芯片，专用 GPIO 在哪个任务里创建都可以用。
  static SwdSet swd_set;
  static Dap dap(LibXR::USB::Endpoint::EPNumber::EP1, LibXR::USB::Endpoint::EPNumber::EP1,
                 swd_set.swd, nullptr);

  // XR 厂商扩展（0x80 到 0x85）挂在 DAP 类的命令钩子上，串口、TCP 行为一致。
  static DapVendor vendor(swd_set.swd, Config::kDapPacketSize);
  vendor.Begin(Config::kCore, Config::kSamplingPriority);
  dap.SetCommandHook(&DapVendor::Hook, &vendor);

  // 串口 DAP 链路，走 USB Serial/JTAG。控制台不能用它（sdkconfig 里第二控制台已关掉）。
  using SerialAdapter = SerialDap<Dap>;
  using SerialLink = LibXR::DapSerialTransport<SerialAdapter, Config::kDapPacketSize>;
  static LibXR::ESP32CDCJtag usj(
      /*rx_buffer_size=*/8192, /*tx_buffer_size=*/4096, /*tx_queue_size=*/5,
      LibXR::UART::Configuration{Config::kSerialBaud, LibXR::UART::Parity::NO_PARITY, 8, 1});
  static SerialAdapter adapter(dap, vendor);
  static SerialLink link(adapter, usj);
#if CONFIG_IDF_TARGET_ESP32C3
  // C3：这个 USB 口同时是 DAP 链路和 Wi-Fi 命令行，自动区分 DAP 帧和人打的命令（serial_mux.hpp）。
  // 用串口终端打开它就能 wifi set；没有引出 UART0 的板子也能配网。
  static SerialMux<SerialLink, LibXR::ESP32CDCJtag> mux(link, usj);
  mux.Start(Config::kCore, Config::kSerialPriority);
#else
  link.SetThreadStarter(
      [](void*, const SerialLink::ThreadConfig&, void (*entry)(void*), void* arg)
      {
        xTaskCreatePinnedToCore(entry, "dap_serial", 8192, arg, Config::kSerialPriority, nullptr,
                                Config::kCore);
      });
  if (link.Start() != LibXR::ErrorCode::OK)
  {
    ESP_LOGE(TAG, "串口 DAP 链路启动失败");
  }
#endif
  ESP_LOGI(TAG, "USB Serial/JTAG 上的串口 DAP 链路已启动，SWCLK GPIO%d，SWDIO GPIO%d",
           Config::kSwclkGpio, Config::kSwdioGpio);

#if CONFIG_IDF_TARGET_ESP32C3
  // Wi-Fi 和 TCP。Wi-Fi 最多等 20 秒；没连上时 Wi-Fi 仍会在后台重连，TCP 服务照常启动。
  // 没有热点参数时 Wi-Fi 不启动，只提供串口。
  const esp_err_t wifi = WifiStaStart(20000);
  // UART0 上也保留一个命令行，给引出了 UART0 的开发板用。没有热点参数时也能用它来设置。
  static LibXR::ESP32UART console_uart(
      static_cast<uart_port_t>(Config::kConsoleUart), Config::kConsoleTxGpio,
      Config::kConsoleRxGpio, LibXR::ESP32UART::PIN_NO_CHANGE, LibXR::ESP32UART::PIN_NO_CHANGE,
      /*rx_buffer_size=*/256, /*tx_buffer_size=*/1024, /*tx_queue_size=*/8,
      LibXR::UART::Configuration{Config::kConsoleBaud, LibXR::UART::Parity::NO_PARITY, 8, 1},
      /*enable_dma=*/false);
  static WifiConsole<LibXR::ESP32UART> console(console_uart);
  console.Start(Config::kCore);
  if (wifi == ESP_OK || wifi == ESP_ERR_TIMEOUT)
  {
    static DapTcpServer<Dap> server(dap, Config::kTcpPort);
    server.SetHooks(&TcpHandover::OnConnect, &TcpHandover::OnDisconnect, &vendor);
    server.Start(Config::kCore);
    ESP_LOGI(TAG, "TCP 服务已启动，端口 %u，剩余堆 %u 字节", (unsigned)Config::kTcpPort,
             (unsigned)esp_get_free_heap_size());
  }
  else
  {
    ESP_LOGW(TAG, "Wi-Fi 未启动（%s），只提供串口", esp_err_to_name(wifi));
  }
#endif
}
