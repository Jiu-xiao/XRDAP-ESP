// SPDX-License-Identifier: Apache-2.0
// XR-DAP-S3：基于 libxr 的 ESP32-S3 CMSIS-DAP 调试探针。
//   USB：Bulk v2 的 DAP 接口，外加一个 CDC 虚拟串口。
//   Wi-Fi：连上热点后提供 OpenOCD 的 TCP 后端和 USB/IP 虚拟 USB 探针；有网络客户端时
//          USB 关闭，客户端走后 USB 恢复。
//   串口：UART0 上用 libxr Topic 帧提供 CMSIS-DAP，配合电脑端 xrdap 使用（板上有 USB
//         转串口芯片时，见 config.hpp 的板型）。
// 三种传输共用同一个 DAP 实例和 XR 厂商扩展（多地址读写、指针链、探针端采样）。
//
// XR-DAP-S3: a CMSIS-DAP debug probe for the ESP32-S3 built on libxr.
//   USB:    a Bulk v2 DAP interface plus a CDC virtual serial port.
//   Wi-Fi:  once joined, serves the OpenOCD TCP backend and a USB/IP virtual USB probe;
//           USB is shut down while a network client is connected and restored after.
//   Serial: CMSIS-DAP in libxr Topic frames on UART0, for the PC-side xrdap (on boards
//           with a USB-UART chip, see the board choice in config.hpp).
// All three transports share one DAP instance and the XR vendor extension (multi-address
// access, pointer chains, probe-side sampling).
#include <cstdint>

#include "cdc_uart.hpp"
#include "config.hpp"
#include "dap_vendor.hpp"
#include "daplink_v2_profile_swd.hpp"
#include "debug/dap_serial_transport.hpp"
#include "dedic_gpio_pin.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timebase.hpp"
#include "esp_uart.hpp"
#include "esp_usb_dev.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "libxr.hpp"
#include "probe_app.hpp"
#include "probe_info.hpp"
#include "serial_link.hpp"
#include "status_panel.hpp"
#include "swd_engine.hpp"
#include "uart_console.hpp"

namespace
{
using namespace XrDap;

constexpr const char* TAG = "xrdap";

// S3 的 IN 端点只有 EP0 和 EP1 到 EP4。EP1 是 DAP（收发共用），EP2 是 CDC 数据，EP3 的
// IN 是 CDC 通知。缓冲按 64 字节对齐（DMA 缓存行），每个方向至少两倍最大包长。
alignas(64) uint8_t ep0_buffer[128];
alignas(64) uint8_t ep1_dap[4096];
alignas(64) uint8_t ep2_cdc[1024];
alignas(64) uint8_t ep3_comm[128];

using Dap = LibXR::USB::DapLinkV2Class<SwdEngine, Config::kDapPacketSize,
                                       Config::kDapPacketCount, Config::kDapPacketSize>;

// SWD 相关对象。必须在 Config::kSwdCore 上创建：专用 GPIO 只能在创建它的核上访问。
struct SwdSet
{
  DedicGpioPin swclk{Config::kSwclkGpio, /*bidirectional=*/false, /*pull_up=*/false,
                     Config::kPadDriveStrength};
  DedicGpioPin swdio{Config::kSwdioGpio, /*bidirectional=*/true, /*pull_up=*/true,
                     Config::kPadDriveStrength};
  SwdEngine swd{swclk, swdio, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U};
};

SwdSet& GetSwdSet()
{
  static SwdSet set;
  return set;
}

// 网络客户端连接期间关闭 USB，断开后重新打开，两条路不会同时驱动 DAP。USB 栈的开关在
// 核 0（USB 中断所在核）的专用任务里做，网络服务任务只等它完成。
class UsbSwitch
{
 public:
  explicit UsbSwitch(LibXR::ESP32USBDevice& usb) : usb_(usb) {}

  void Start()
  {
    xTaskCreatePinnedToCore(&UsbSwitch::TaskEntry, "usbctl", 4096, this, 6, nullptr, 0);
  }

  /// StartNetwork 的 on_client 回调 / on_client callback for StartNetwork.
  static void OnNetworkClient(bool connected, void* ctx)
  {
    static_cast<UsbSwitch*>(ctx)->Request(connected ? Command::DOWN : Command::UP);
  }

 private:
  enum class Command
  {
    DOWN,
    UP
  };

  static void TaskEntry(void* arg) { static_cast<UsbSwitch*>(arg)->Run(); }

  void Request(Command cmd)
  {
    command_ = cmd;
    xSemaphoreGive(request_);
    xSemaphoreTake(done_, portMAX_DELAY);
  }

  void Run()
  {
    for (;;)
    {
      xSemaphoreTake(request_, portMAX_DELAY);
      if (command_ == Command::DOWN && active_)
      {
        usb_.Stop(false);
        usb_.Deinit(false);
        active_ = false;
        ESP_LOGI(TAG, "网络客户端已连接，USB 关闭");
      }
      else if (command_ == Command::UP && !active_)
      {
        usb_.Init(false);
        usb_.Start(false);
        active_ = true;
        ESP_LOGI(TAG, "网络客户端已断开，USB 恢复");
      }
      xSemaphoreGive(done_);
    }
  }

  LibXR::ESP32USBDevice& usb_;
  SemaphoreHandle_t request_ = xSemaphoreCreateBinary();
  SemaphoreHandle_t done_ = xSemaphoreCreateBinary();
  volatile Command command_ = Command::UP;
  bool active_ = true;
};

}  // namespace

extern "C" void app_main(void)
{
  static LibXR::ESP32Timebase timebase;
  LibXR::PlatformInit(Config::kLibxrTimerPriority, 8192);

  // 有 SWDIO 模拟开关的板子（Lite）先把开关接到 SWDIO 引脚那一侧，再建 SWD 对象。
  if constexpr (Config::kSwdioSelectGpio >= 0)
  {
    const auto sel = static_cast<gpio_num_t>(Config::kSwdioSelectGpio);
    gpio_reset_pin(sel);
    gpio_set_level(sel, 0);
    gpio_set_direction(sel, GPIO_MODE_OUTPUT);
  }

  // 在 SWD 核上创建 SWD 对象，等它建好。
  static volatile bool swd_ready = false;
  xTaskCreatePinnedToCore(
      [](void*)
      {
        (void)GetSwdSet();
        swd_ready = true;
        vTaskDelete(nullptr);
      },
      "swdinit", 4096, nullptr, 5, nullptr, Config::kSwdCore);
  while (!swd_ready)
  {
    vTaskDelay(1);
  }

  static Dap dap(LibXR::USB::Endpoint::EPNumber::EP1, LibXR::USB::Endpoint::EPNumber::EP1,
                 GetSwdSet().swd, nullptr);

  // XR 厂商扩展挂在 DAP 类的命令钩子上，USB、网络、串口行为一致。
  static DapVendor vendor(GetSwdSet().swd, Config::kDapPacketSize);
  InstallVendor(dap, vendor, Config::kProductName, Config::kSwdCore,
                Config::kSamplingPriority);

  // libxr 在应用线程里执行 DAP 命令，循环调用 Step()。线程放在 SWD 核上，与核 0 的 USB
  // 中断分开，避免互相抢占。
  xTaskCreatePinnedToCore(
      [](void*)
      {
        for (;;)
        {
          (void)dap.Step();
        }
      },
      "dapstep", 8192, nullptr, Config::kStepPriority, nullptr, Config::kSwdCore);

  // USB
  static LibXR::USB::CDCUart cdc(LibXR::USB::Endpoint::EPNumber::EP2,
                                 LibXR::USB::Endpoint::EPNumber::EP2,
                                 LibXR::USB::Endpoint::EPNumber::EP3, 1024, 1024, 8);
  // USB 序列号 = 前缀 + 出厂 MAC（6 字节），协议栈把它转成十六进制接在前缀后面，和
  // DAP_Info 的序列号相同。
  static constexpr auto lang = LibXR::USB::DescriptorStrings::MakeLanguagePack(
      LibXR::USB::DescriptorStrings::Language::EN_US, kVendorName,
      Config::kUsbProductName, kSerialPrefix);
  static LibXR::ESP32USBDevice usb(
      {
          {LibXR::RawData{ep0_buffer, sizeof(ep0_buffer)}},
          {LibXR::RawData{ep1_dap, sizeof(ep1_dap)}},
          {LibXR::RawData{ep2_cdc, sizeof(ep2_cdc)}},
          {LibXR::RawData{ep3_comm, sizeof(ep3_comm)}, true},
      },
      LibXR::USB::DeviceDescriptor::PacketSize0::SIZE_64, Config::kUsbVid,
      Config::kUsbPid, Config::kUsbBcdDevice, {&lang}, {{&dap, &cdc}},
      LibXR::ConstRawData{ProbeMac(), 6});
  usb.Init(false);
  usb.Start(false);
  vendor.SetUsbResetSource(
      [](void* ctx) -> uint32_t
      { return static_cast<LibXR::ESP32USBDevice*>(ctx)->BusResetCount(); }, &usb);
  ESP_LOGI(TAG, "USB 已启动");

  // 串口 DAP 链路。执行 DAP 命令的线程必须在 SWD 核上（专用 GPIO 的限制），由这里创建。
  if constexpr (Config::kSerialLink)
  {
    using SerialAdapter = SerialDap<Dap>;
    using SerialLink = LibXR::DapSerialTransport<SerialAdapter, Config::kDapPacketSize>;
    static LibXR::ESP32UART uart(
        static_cast<uart_port_t>(Config::kSerialUart), Config::kSerialTxGpio,
        Config::kSerialRxGpio, LibXR::ESP32UART::PIN_NO_CHANGE,
        LibXR::ESP32UART::PIN_NO_CHANGE,
        /*rx_buffer_size=*/8192, /*tx_buffer_size=*/4096, /*tx_queue_size=*/5,
        LibXR::UART::Configuration{Config::kSerialBaud, LibXR::UART::Parity::NO_PARITY, 8,
                                   1},
        /*enable_dma=*/false);
    static SerialAdapter adapter(dap, vendor);
    static SerialLink link(adapter, uart);
    link.SetThreadStarter(
        [](void*, const SerialLink::ThreadConfig&, void (*entry)(void*), void* arg)
        {
          xTaskCreatePinnedToCore(entry, "dap_serial", 8192, arg, Config::kSerialPriority,
                                  nullptr, Config::kSwdCore);
        });
    if (link.Start() != LibXR::ErrorCode::OK)
    {
      ESP_LOGE(TAG, "串口 DAP 链路启动失败");
    }
  }

  // Wi-Fi 命令行在 USB 虚拟串口上，没有热点参数时也能用它来设置。
  static UartConsole<LibXR::USB::CDCUart> console(cdc);
  console.Start(0);

  // 有状态灯的板子（Lite）：在 Wi-Fi 连接之前就开始显示。
  StatusPanel* panel = nullptr;
  if constexpr (Config::kStatusLedGpio >= 0)
  {
    static StatusPanel status(Config::kStatusLedGpio, Config::kWifiButtonGpio,
                              vendor.Activity());
    status.Start(0);
    panel = &status;
  }

  // 网络客户端连上、断开时：关闭或恢复 USB，更新状态灯。
  struct ClientHooks
  {
    UsbSwitch& usb;
    StatusPanel* panel;

    static void OnClient(bool connected, void* ctx)
    {
      auto* self = static_cast<ClientHooks*>(ctx);
      UsbSwitch::OnNetworkClient(connected, &self->usb);
      if (self->panel != nullptr)
      {
        self->panel->SetNetworkClient(connected);
      }
    }
  };
  static UsbSwitch usb_switch(usb);
  static ClientHooks hooks{usb_switch, panel};
  if (StartNetwork(dap, vendor,
                   {Config::kChipName, Config::kUsbProductName, Config::kSwdCore,
                    &ClientHooks::OnClient, &hooks}))
  {
    usb_switch.Start();
  }
}
