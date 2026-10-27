// SPDX-License-Identifier: Apache-2.0
// XR-DAP-S3 / XR-DAP-S2：基于 libxr 的 ESP32-S3、ESP32-S2 CMSIS-DAP 调试探针（芯片原生
// USB OTG；板型见 config.hpp）。
//   USB：Bulk v2 的 DAP 接口，外加一个 CDC 虚拟串口。
//   Wi-Fi：连上热点后提供 OpenOCD 的 TCP 后端和 USB/IP 虚拟 USB 探针。
//   串口：UART0 上用 libxr Topic 帧提供 CMSIS-DAP，配合电脑端 xrdap 使用（板上有 USB
//         转串口芯片时，见 config.hpp 的板型）。
// 三种传输经 XRDAP-Core 的同一个执行器驱动 DAP 和 XR 厂商扩展（多地址读写、指针链、
// 探针端采样）；同一时间只有一条传输拥有探针，别的传输收到“忙”。
//
// XR-DAP-S3 / XR-DAP-S2: a CMSIS-DAP debug probe for the ESP32-S3 and ESP32-S2 built on
// libxr (the chip's native USB OTG; boards in config.hpp).
//   USB:    a Bulk v2 DAP interface plus a CDC virtual serial port.
//   Wi-Fi:  once joined, serves the OpenOCD TCP backend and a USB/IP virtual USB probe.
//   Serial: CMSIS-DAP in libxr Topic frames on UART0, for the PC-side xrdap (on boards
//           with a USB-UART chip, see the board choice in config.hpp).
// All three transports drive DAP and the XR vendor extension (multi-address access,
// pointer chains, probe-side sampling) through XRDAP-Core's one executor; one transport
// owns the probe at a time and the others get busy.
#include <atomic>
#include <cstdint>
#include <cstdlib>

#include "cdc_uart.hpp"
#include "config.hpp"
#include "daplink_v2_profile_swd.hpp"
#include "dedic_gpio_pin.hpp"
#include "driver/gpio.h"
#include "esp_gpio.hpp"
#include "esp_timebase.hpp"
#include "esp_uart.hpp"
#include "esp_usb_dev.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "heap_report.hpp"
#include "hold_button.hpp"
#include "libxr.hpp"
#include "probe_app.hpp"
#include "probe_info.hpp"
#include "restart.hpp"
#include "status_panel.hpp"
#include "swd_engine.hpp"
#include "uart_console.hpp"
#include "xrdap/probe/family.hpp"
#include "xrdap/serial/serial_link.hpp"

namespace
{
using namespace XrDap;

// S3 的 IN 端点只有 EP0 和 EP1 到 EP4。EP1 是 DAP（收发共用），EP2 是 CDC 数据，EP3 的
// IN 是 CDC 通知。缓冲按 64 字节对齐（DMA 缓存行），每个方向至少两倍最大包长。
alignas(64) uint8_t ep0_buffer[128];
alignas(64) uint8_t ep1_dap[4096];
alignas(64) uint8_t ep2_cdc[1024];
alignas(64) uint8_t ep3_comm[128];

void RequireStartup(bool ready)
{
  if (!ready)
  {
    // 资源创建失败不能继续初始化或无限等待。
    // A failed allocation must not continue startup or wait forever.
    abort();
  }
}

using DapBase =
    LibXR::USB::DapLinkV2Class<SwdEngine, Config::kDapPacketSize, Config::kDapPacketCount,
                               Config::kDapPacketSize>;

// USB 的 DAP 接口，另外数 USB 总线复位：计数变了，USB 的会话就结束（拔线、主机复位）/
// The USB DAP interface, which also counts USB bus resets: a changed count ends the USB
// session (unplug, host reset).
class DapLink final : public DapBase
{
 public:
  using DapBase::DapBase;

  /// 总线复位次数，任何任务可读 / Bus reset count; readable from any task.
  uint32_t ResetCount() const { return resets_.load(std::memory_order_relaxed); }

  // libxr 在 USB 的 owner 上下文（中断或应用线程）里调用 / Called by libxr in the USB
  // owner context (an interrupt or an application thread).
  void OnDeviceEvent(bool in_isr, LibXR::USB::DeviceEvent event,
                     uint8_t endpoint) override
  {
    if (event == LibXR::USB::DeviceEvent::RESET)
    {
      resets_.fetch_add(1U, std::memory_order_relaxed);
    }
    DapBase::OnDeviceEvent(in_isr, event, endpoint);
  }

 private:
  std::atomic<uint32_t> resets_{0U};
};

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

bool StartSerialLink(ProbeCore<DapLink>& probe)
{
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
  // ThreadStarter 同步调用，状态保留到固件退出；任务本身不读写此标志。
  // ThreadStarter runs synchronously; this state outlives the link and is not shared
  // with its worker task.
  static bool task_created = false;
  link.SetThreadStarter(
      [](void* ctx, const Link::ThreadConfig&, void (*entry)(void*), void* arg)
      {
        const BaseType_t created =
            xTaskCreatePinnedToCore(entry, "dap_serial", 8192, arg,
                                    Config::kSerialPriority, nullptr, Config::kSwdCore);
        *static_cast<bool*>(ctx) = created == pdPASS;
      },
      &task_created);
  const LibXR::ErrorCode started = link.Start();
  return started == LibXR::ErrorCode::OK && task_created;
}

}  // namespace

extern "C" void app_main(void)
{
  static LibXR::ESP32Timebase timebase;
  LibXR::PlatformInit(Config::kLibxrTimerPriority, Config::kLibxrTimerStack);
  // 失败时 RestartLater 立即重启 / RestartLater restarts at once on failure.
  (void)RestartInit();
  // NVS 在命令行、0x86、状态灯之前初始化一次；失败时它们报存储错误 / NVS is set up once
  // before the console, 0x86 and the status light; on failure they report storage
  // errors.
  (void)WifiStaInitStorage();

  // 有 SWDIO 模拟开关的板子（Lite）先把开关接到 SWDIO 引脚那一侧，再建 SWD 对象。
  if constexpr (Config::kSwdioSelectGpio >= 0)
  {
    const auto sel = static_cast<gpio_num_t>(Config::kSwdioSelectGpio);
    gpio_reset_pin(sel);
    gpio_set_level(sel, 0);
    gpio_set_direction(sel, GPIO_MODE_OUTPUT);
  }

  // 在 SWD 核上创建对象，用专用静态信号量同步，不占用 app_main 的任务通知。
  // Construct on the SWD core; a dedicated static semaphore leaves task notifications
  // available to app_main's other startup helpers and bounds the wait.
  static StaticSemaphore_t swd_ready_storage;
  const SemaphoreHandle_t swd_ready = xSemaphoreCreateBinaryStatic(&swd_ready_storage);
  RequireStartup(swd_ready != nullptr);
  const BaseType_t swd_created = xTaskCreatePinnedToCore(
      [](void* ctx)
      {
        (void)GetSwdSet();
        const BaseType_t notified = xSemaphoreGive(static_cast<SemaphoreHandle_t>(ctx));
        RequireStartup(notified == pdTRUE);
        vTaskDelete(nullptr);
      },
      "swdinit", 4096, swd_ready, 5, nullptr, Config::kSwdCore);
  RequireStartup(swd_created == pdPASS);
  const BaseType_t swd_signalled = xSemaphoreTake(swd_ready, pdMS_TO_TICKS(5000));
  RequireStartup(swd_signalled == pdTRUE);

  // 目标 nRESET（有的板子才有）：开漏，先写高再设方向，上电时不复位目标 / Target nRESET
  // (on boards that have it): open drain, written high before the direction is set, so
  // power-up does not reset the target.
  LibXR::GPIO* nreset = nullptr;
  if constexpr (Config::kNresetGpio >= 0)
  {
    static LibXR::ESP32GPIO pin(static_cast<gpio_num_t>(Config::kNresetGpio));
    (void)pin.Write(true);
    (void)pin.SetConfig(
        {LibXR::GPIO::Direction::OUTPUT_OPEN_DRAIN, LibXR::GPIO::Pull::UP});
    nreset = &pin;
  }

  static DapLink dap(LibXR::USB::Endpoint::EPNumber::EP1,
                     LibXR::USB::Endpoint::EPNumber::EP1, GetSwdSet().swd, nreset);

  // 会话主人、厂商扩展和执行器；所有传输经执行器驱动 DAP / Session owner, vendor
  // extension and executor; every transport drives DAP through the executor.
  static ProbeCore<DapLink> probe(dap, GetSwdSet().swd);
  InstallProbe(probe, dap, WifiStaSettings(), Config::kProductName, Config::kSwdCore,
               Config::kSamplingPriority);

  // USB 的 DAP 命令在应用线程里经执行器执行。线程放在 SWD 核上，与核 0 的 USB 中断分开。
  // 等命令时不占执行轮次，别的传输照常执行 / USB DAP commands run through the executor in
  // an application thread on the SWD core, apart from the USB interrupt on core 0.
  // Waiting for a command takes no executor turn, so the other transports run meanwhile.
  const BaseType_t dap_created = xTaskCreatePinnedToCore(
      [](void*)
      {
        for (;;)
        {
          (void)probe.exec.StepUsb(UINT32_MAX);
        }
      },
      "dapstep", Config::kStepStack, nullptr, Config::kStepPriority, nullptr,
      Config::kSwdCore);
#if CONFIG_IDF_TARGET_ESP32S2
  RequireStartup(dap_created == pdPASS);
#else
  (void)dap_created;
#endif
  // S3 保留原来的降级行为：此任务失败不阻止串口和网络启动。
  // Preserve S3 degradation: failure of USB DAP does not prevent UART or network use.

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
      LibXR::USB::DeviceDescriptor::PacketSize0::SIZE_64, Family::kUsbVid,
      Family::kUsbPid, Config::kUsbBcdDevice, {&lang}, {{&dap, &cdc}},
      LibXR::ConstRawData{ProbeMac(), 6});
  usb.Init(false);
  usb.Start(false);
  // USB 总线复位（拔线、主机复位）结束 USB 的会话 / A USB bus reset (unplug, host reset)
  // ends the USB session.
  probe.session.SetUsbResetSource([](void* ctx) -> uint32_t
                                  { return static_cast<DapLink*>(ctx)->ResetCount(); },
                                  &dap);

  // 串口 DAP 链路。执行 DAP 命令的线程必须在 SWD 核上（专用 GPIO 的限制），由这里创建。
  if constexpr (Config::kSerialLink)
  {
    // 默认 S3 的串口为可选传输，失败时 USB 和网络仍可用。
    // The default S3 UART is optional; USB and network remain available on failure.
    (void)StartSerialLink(probe);
  }

  // Wi-Fi 命令行在 USB 虚拟串口上，没有热点参数时也能用它来设置。
  // 命令行的 heap 命令 / The console's heap command.
  static EspHeapReport heap_report;
#if CONFIG_IDF_TARGET_ESP32S2
  heap_report.SetExtraReport(
      +[](ConsoleWriteFn write, void* ctx, void* probe_ctx)
      {
        WriteProbeDiagnostics<DapLink>(write, ctx, probe_ctx);
        char line[64];
        const int n = snprintf(line, sizeof(line), "swd last_fast_level=%lu\r\n",
                               static_cast<unsigned long>(SwdFastLastLevel()));
        if (n > 0 && static_cast<size_t>(n) < sizeof(line))
        {
          write(ctx, line, static_cast<size_t>(n));
        }
      },
      &probe);
#else
  heap_report.SetExtraReport(&WriteProbeDiagnostics<DapLink>, &probe);
#endif
  static UartConsole<LibXR::USB::CDCUart> console(cdc, WifiStaSettings(), &heap_report);
  const bool console_started = console.Start(0);
#if CONFIG_IDF_TARGET_ESP32S2
  RequireStartup(console_started);
#else
  (void)console_started;
#endif
  // S3 的命令行创建失败不影响 DAP、状态灯和网络。
  // A failed S3 console task does not disable DAP, status indicators or networking.

  // Wi-Fi 清除按键（只有 Lite 板有）：按满即清除并重启 / Wi-Fi clear button (Lite board
  // only): clear and restart once the hold time is reached.
  static HoldButton* button = nullptr;
  if constexpr (Config::kWifiButtonGpio >= 0)
  {
    static HoldButton hold(
        Config::kWifiButtonGpio, Config::kWifiClearHoldMs,
        [](void*)
        {
          WifiSettings& wifi = WifiStaSettings();
          if (wifi.Clear(EraseSource::BUTTON))
          {
            wifi.RestartLater();
          }
        },
        nullptr);
    if (hold.Start(0))
    {
      button = &hold;
    }
  }

  // 状态灯在 Wi-Fi 连接之前就开始显示；输入都在这里取 / The status light starts before
  // Wi-Fi joins; all of its inputs are taken here.
  static std::atomic<bool> net_client{false};
  static StatusLed led(Config::kStatusLedGpio);
  static StatusPanel panel(
      led, probe.activity,
      [](void*)
      {
        return LinkInputs{net_client.load(), usb.IsConfigured(), WifiStaGetLink(),
                          RestartPending(), button != nullptr && button->Held()};
      },
      nullptr);
  (void)panel.Start(0);

  (void)StartNetwork(
      probe, {Config::kChipName, Config::kSwdCore, &net_client});
}
