// SPDX-License-Identifier: Apache-2.0
#pragma once

// 各芯片相同的启动步骤：挂厂商扩展，启动 Wi-Fi、TCP/USB-IP 服务和 mDNS。各芯片的
// main.cpp 只做自己特有的部分（SWD 对象、S3 的 USB 设备、串口链路和命令行）。
//
// Start-up steps shared by every chip: install the vendor extension, start Wi-Fi, the
// TCP/USB-IP server and mDNS. Each chip's main.cpp only does its own parts (the SWD
// objects, the S3 USB device, the serial link and the console).

#include <cstdint>

#include "dap_tcp.hpp"
#include "dap_vendor.hpp"
#include "mdns_service.hpp"
#include "probe_config.hpp"
#include "probe_info.hpp"
#include "sampler_task.hpp"
#include "wifi_command.hpp"
#include "wifi_sta.hpp"

namespace XrDap
{

/// @brief 设置 DAP_Info 身份，挂上厂商扩展（含 0x86 Wi-Fi 命令）并启动采样任务 / Set the
///        DAP_Info identity, hook up the vendor extension (with the 0x86 Wi-Fi command)
///        and start the sampling task.
/// @param wifi 0x86 读写的 Wi-Fi 设置，生命周期需长于 vendor / Wi-Fi settings behind
///        0x86; must outlive vendor.
/// @param product DAP_Info 产品名，例如 "XR-DAP-S3" / DAP_Info product, e.g. "XR-DAP-S3".
/// @param core SWD 所在的核 / Core that owns the SWD pins.
/// @param sampling_priority 采样任务优先级 / Sampling task priority.
/// @note 在 app_main 里、DAP 命令开始处理之前调用一次 / Call once from app_main before
///       DAP commands are processed.
template <typename DapT>
void InstallVendor(DapT& dap, DapVendor& vendor, WifiSettings& wifi, const char* product,
                   int core, unsigned sampling_priority)
{
  SetProbeInfo(dap, ProbeIdentity{product, product, ProbeSerial(), kFirmwareVersion});
  vendor.SetExtraCommand(WifiCommand::kCmd, WifiCommand::kFeature, &WifiCommand::Handle,
                         &wifi);
  StartSamplerTask(vendor, core, sampling_priority);
  dap.SetCommandHook(&DapVendor::Hook, &vendor);
}

/// 网络服务的参数 / Network service options.
struct NetworkOptions
{
  /// mDNS TXT 里的芯片名，例如 "esp32s3" / Chip name for the mDNS TXT, e.g. "esp32s3".
  const char* chip;
  /// USB/IP 虚拟设备的产品名，例如 "XR-DAP-S3 CMSIS-DAP" / Product name of the USB/IP
  /// virtual device, e.g. "XR-DAP-S3 CMSIS-DAP".
  const char* usb_product;
  /// 服务任务所在的核，须能访问 SWD / Core for the server task; must reach the SWD pins.
  int core;
  /// @brief 网络客户端连上（true）和断开（false）时额外调用，可为空 / Extra call when a
  ///        network client connects (true) or leaves (false); may be null.
  void (*on_client)(bool connected, void* ctx);
  /// 传给 on_client 的参数 / Argument passed to on_client.
  void* ctx;
};

/// @brief 连 Wi-Fi，启动 TCP（OpenOCD）和 USB/IP 服务及 mDNS / Join Wi-Fi and start the
///        TCP (OpenOCD) and USB/IP services plus mDNS.
/// @return Wi-Fi 没有启动（没有热点参数）或服务任务建不起来时返回 false / False when
///         Wi-Fi is not started (no credentials) or the server task cannot be created.
/// @note 最多阻塞 Config::kWifiWaitMs；没连上时 Wi-Fi 在后台重连，服务照常启动 / Blocks
///       for up to Config::kWifiWaitMs; if not yet joined, Wi-Fi keeps reconnecting in
///       the background and the services start anyway.
template <typename DapT>
bool StartNetwork(DapT& dap, DapVendor& vendor, const NetworkOptions& opt)
{
  const esp_err_t wifi = WifiStaStart(Config::kWifiWaitMs);
  if (wifi != ESP_OK && wifi != ESP_ERR_TIMEOUT)
  {
    return false;
  }

  // 网络客户端接管探针：别的传输发起的采样作废；客户端走后它发起的采样停止。
  struct Handover
  {
    DapVendor& vendor;
    NetworkOptions opt;

    static void OnConnect(void* ctx)
    {
      auto* self = static_cast<Handover*>(ctx);
      self->vendor.OnNetworkClientConnected();
      if (self->opt.on_client != nullptr)
      {
        self->opt.on_client(true, self->opt.ctx);
      }
    }

    static void OnDisconnect(void* ctx)
    {
      auto* self = static_cast<Handover*>(ctx);
      self->vendor.OnTransportClosed();
      if (self->opt.on_client != nullptr)
      {
        self->opt.on_client(false, self->opt.ctx);
      }
    }
  };
  static Handover handover{vendor, opt};
  static DapTcpServer<DapT> server(dap, Config::kTcpPort);
  server.SetHooks(&Handover::OnConnect, &Handover::OnDisconnect, &handover);
  server.EnableUsbip(Config::kUsbipPort, opt.usb_product, ProbeSerial());
  if (!server.Start(opt.core))
  {
    return false;
  }
  MdnsStart(opt.chip, Config::kTcpPort);
  return true;
}

}  // namespace XrDap
