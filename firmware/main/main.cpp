// SPDX-License-Identifier: Apache-2.0
// XR-DAP-S3：基于 libxr 的 ESP32-S3 CMSIS-DAP 调试探针。
//   USB：Bulk v2 的 DAP 接口，外加一个 CDC 虚拟串口。
//   Wi-Fi：连上热点后在 TCP 端口提供 CMSIS-DAP。有 TCP 客户端时 USB 关闭，客户端走后 USB 恢复。
#include <cstdint>

#include "cdc_uart.hpp"
#include "config.hpp"
#include "dap_tcp.hpp"
#include "daplink_v2_profile_swd.hpp"
#include "debug/swd_general_gpio.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timebase.hpp"
#include "esp_usb_dev.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "libxr.hpp"
#include "s3_dedic_gpio.hpp"
#include "swd_link.hpp"
#include "wifi_console.hpp"
#include "wifi_sta.hpp"

namespace
{
using namespace XrDap;

constexpr const char* TAG = "xrdap";

// ESP32-S3 的 IN 端点只有 EP0 和 EP1 到 EP4。EP1 是 DAP（收发共用），EP2 是 CDC 数据，
// EP3 的 IN 是 CDC 通知。缓冲 64 字节对齐（DMA 缓存行），每个方向至少两倍最大包长。
alignas(64) uint8_t ep0_buffer[128];
alignas(64) uint8_t ep1_dap[4096];
alignas(64) uint8_t ep2_cdc[1024];
alignas(64) uint8_t ep3_comm[128];

using SwdLink = LibXR::Debug::SwdGeneralGPIO<S3DedicGpio, S3DedicGpio>;
using Dap = LibXR::USB::DapLinkV2Class<SwdWithClock, Config::kDapPacketSize, 4u,
                                       Config::kDapPacketSize>;

// SWD 相关对象。必须在 Config::kSwdCore 这个核上创建，专用 GPIO 只能在创建它的核上使用。
struct SwdSet
{
  S3DedicGpio swclk{Config::kSwclkGpio, /*bidirectional=*/false, /*pull_up=*/false};
  S3DedicGpio swdio{Config::kSwdioGpio, /*bidirectional=*/true, /*pull_up=*/true};
  // 延时参数填 0：SwdGeneralGPIO 走全速路径，SWCLK 频率由 swd_link.hpp 里设定。
  SwdLink link{swclk, swdio, 0u};
  SwdWithClock swd{link, swclk, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U};
};

SwdSet& GetSwdSet()
{
  static SwdSet set;
  return set;
}

// 在 TCP 客户端连接期间关闭 USB，客户端断开后重新打开。
// USB 栈在核 0（USB 中断所在的核）上操作，TCP 任务只等它完成。
class UsbSwitch
{
 public:
  explicit UsbSwitch(LibXR::ESP32USBDevice& usb) : usb_(usb) {}

  void Start()
  {
    xTaskCreatePinnedToCore(&UsbSwitch::TaskEntry, "usbctl", 4096, this, 6, nullptr, 0);
  }

  void Down() { Request(Command::DOWN); }
  void Up() { Request(Command::UP); }

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
        ESP_LOGI(TAG, "TCP 客户端已连接，USB 关闭");
      }
      else if (command_ == Command::UP && !active_)
      {
        usb_.Init(false);
        usb_.Start(false);
        active_ = true;
        ESP_LOGI(TAG, "TCP 客户端已断开，USB 恢复");
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
  LibXR::PlatformInit(3, 8192);

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

  // libxr 在应用线程里执行 DAP 命令：循环调用 Step()。线程放在 SWD 核上，
  // 和 USB 中断分开，避免互相抢占。
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
  // 序列号 = 前缀 + 芯片唯一 ID。ESP32-S3 用 eFuse 里的出厂 MAC（6 字节）做唯一 ID，协议栈把它转成十六进制接在前缀后面。
  static uint8_t uid[6];
  ESP_ERROR_CHECK(esp_efuse_mac_get_default(uid));
  static constexpr auto lang = LibXR::USB::DescriptorStrings::MakeLanguagePack(
      LibXR::USB::DescriptorStrings::Language::EN_US, "XRobot", "XR-DAP-S3",
      Config::kUsbSerialPrefix);
  static LibXR::ESP32USBDevice usb(
      {
          {LibXR::RawData{ep0_buffer, sizeof(ep0_buffer)}},
          {LibXR::RawData{ep1_dap, sizeof(ep1_dap)}},
          {LibXR::RawData{ep2_cdc, sizeof(ep2_cdc)}},
          {LibXR::RawData{ep3_comm, sizeof(ep3_comm)}, true},
      },
      LibXR::USB::DeviceDescriptor::PacketSize0::SIZE_64, Config::kUsbVid, Config::kUsbPid, 0x0100,
      {&lang}, {{&dap, &cdc}}, LibXR::ConstRawData{uid, sizeof(uid)});
  usb.Init(false);
  usb.Start(false);
  ESP_LOGI(TAG, "USB 已启动");

  // Wi-Fi 和 TCP。Wi-Fi 最多等 20 秒；没连上时 Wi-Fi 仍会在后台重连，TCP 服务照常启动。
  // 没有热点参数时 Wi-Fi 不启动，只提供 USB。
  const esp_err_t wifi = WifiStaStart(20000);
  // 通过 USB 虚拟串口配置 Wi-Fi。没有热点参数时也能用它来设置。
  static WifiConsole<LibXR::USB::CDCUart> console(cdc);
  console.Start(0);
  if (wifi == ESP_OK || wifi == ESP_ERR_TIMEOUT)
  {
    static UsbSwitch usb_switch(usb);
    usb_switch.Start();
    static DapTcpServer<Dap> server(dap, Config::kTcpPort);
    server.SetHooks([](void* sw) { static_cast<UsbSwitch*>(sw)->Down(); },
                    [](void* sw) { static_cast<UsbSwitch*>(sw)->Up(); }, &usb_switch);
    server.Start(Config::kSwdCore);
    ESP_LOGI(TAG, "TCP 服务已启动，端口 %u，剩余堆 %u 字节", (unsigned)Config::kTcpPort,
             (unsigned)esp_get_free_heap_size());
  }
  else
  {
    ESP_LOGW(TAG, "Wi-Fi 未启动（%s），只提供 USB", esp_err_to_name(wifi));
  }
}
