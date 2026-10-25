// SPDX-License-Identifier: Apache-2.0
#pragma once

// 各芯片相同的启动步骤：XRDAP-Core 的会话主人、厂商扩展和命令执行器，采样任务，Wi-Fi、
// TCP/USB-IP 服务和 mDNS。各芯片的 main.cpp 只做自己特有的部分（SWD 对象、S2 和 S3 的 USB
// 设备、串口链路和命令行）。所有传输经同一个执行器驱动 DAP：同一时间只有一条传输拥有
// 探针，别的传输收到“忙”，DAP_Info 照常回答（XRDAP-Core docs/protocol.md 3.1）。
//
// Start-up steps shared by every chip: XRDAP-Core's session owner, vendor extension and
// command executor, the sampling task, Wi-Fi, the TCP/USB-IP server and mDNS. Each
// chip's main.cpp only does its own parts (the SWD objects, the S2 and S3 USB device, the
// serial link and the console). Every transport drives DAP through the one executor: one
// transport owns the probe at a time, the others get busy, and DAP_Info is answered as
// usual (XRDAP-Core docs/protocol.md 3.1).

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "console_core.hpp"
#include "dap_tcp.hpp"
#include "mdns_service.hpp"
#include "probe_config.hpp"
#include "probe_info.hpp"
#include "sampler_task.hpp"
#include "wifi_command.hpp"
#include "wifi_sta.hpp"
#include "xrdap/session/executor.hpp"

namespace XrDap
{

/// XRDAP-Core 的探针对象：会话主人、厂商扩展、DAP 活动和命令执行器 / XRDAP-Core probe
/// objects: session owner, vendor extension, DAP activity and command executor.
/// @tparam DapT libxr DAP 类 / libxr DAP class.
template <typename DapT>
struct ProbeCore
{
  /// @brief 构造并把执行器的命令钩子装到 dap 上 / Construct and install the executor's
  ///        command hook on dap.
  /// @note app_main 里构造一次；dap 和 swd 的生命周期需长于本对象 / Construct once in
  ///       app_main; dap and swd must outlive this object.
  ProbeCore(DapT& dap, LibXR::Debug::Swd& swd)
      : vendor(swd, session), exec(dap, session, vendor, activity)
  {
  }

  SessionOwner session;  ///< 哪条传输拥有探针 / Which transport owns the probe.
  DapVendor vendor;      ///< 0x80 到 0x85 和附加的 0x86 / 0x80 to 0x85 plus 0x86.
  DapActivity activity;  ///< 给状态灯的 DAP 活动 / DAP activity for the status light.
  Executor<DapT> exec;   ///< 唯一的命令执行器 / The single command executor.
};

/// Extra lines in CDC heap output; no DAP command, session acquisition or SWD access.
template <typename DapT>
void WriteProbeDiagnostics(ConsoleWriteFn write, void* ctx, void* probe_ctx)
{
  auto& probe = *static_cast<ProbeCore<DapT>*>(probe_ctx);
  const auto memory = probe.vendor.MemoryStats();
  const auto commands = probe.exec.CommandStats();
  char line[144];
  auto emit = [&](int n)
  {
    if (n > 0)
    {
      write(ctx, line,
            static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n)
                                                  : sizeof(line) - 1U);
    }
  };
  emit(snprintf(line, sizeof(line),
                "probe owner=%u session=%lu sampling=%u usb_cmd=0x%02lx tcp_cmd=0x%02lx "
                "busy=%lu\r\n",
                static_cast<unsigned>(probe.session.Owner()),
                static_cast<unsigned long>(probe.session.Session()),
                static_cast<unsigned>(probe.vendor.Sampling()),
                static_cast<unsigned long>(commands.last_usb_owner_command),
                static_cast<unsigned long>(commands.last_tcp_owner_command),
                static_cast<unsigned long>(commands.busy_replies)));
  emit(snprintf(line, sizeof(line), "swd failures=%lu ack=%lu ec=%lu resyncs=%lu\r\n",
                static_cast<unsigned long>(memory.failed_transactions),
                static_cast<unsigned long>(memory.last_ack),
                static_cast<unsigned long>(memory.last_error),
                static_cast<unsigned long>(memory.resyncs)));
  emit(snprintf(line, sizeof(line),
                "net accepted=%lu closed=%lu errors=%lu errno=%lu\r\n",
                static_cast<unsigned long>(Net::connections.load()),
                static_cast<unsigned long>(Net::disconnections.load()),
                static_cast<unsigned long>(Net::socket_errors.load()),
                static_cast<unsigned long>(Net::last_socket_error.load())));
}

/// @brief 设置 DAP_Info 身份，挂上 0x86 Wi-Fi 命令，给串口设租期，启动采样任务 / Set the
///        DAP_Info identity, attach the 0x86 Wi-Fi command, give the serial link its
///        lease and start the sampling task.
/// @param wifi 0x86 读写的 Wi-Fi 设置，生命周期需长于 probe / Wi-Fi settings behind 0x86;
///        must outlive probe.
/// @param product DAP_Info 产品名，例如 "XR-DAP-S3" / DAP_Info product, e.g. "XR-DAP-S3".
/// @param core SWD 所在的核 / Core that owns the SWD pins.
/// @param sampling_priority 采样任务优先级 / Sampling task priority.
/// @note 在 app_main 里、DAP 命令开始处理之前调用一次；采样任务建不起来时采样不可用 /
///       Call once from app_main before DAP commands are processed; if the sampling task
///       cannot be created, sampling is unavailable.
template <typename DapT>
void InstallProbe(ProbeCore<DapT>& probe, DapT& dap, WifiSettings& wifi,
                  const char* product, int core, unsigned sampling_priority)
{
  SetProbeInfo(dap, ProbeIdentity{product, product, ProbeSerial(), kFirmwareVersion});
  probe.vendor.SetExtraCommand(WifiCommand::kCmd, WifiCommand::kFeature,
                               &WifiCommand::Handle, &wifi);
  probe.session.SetLease(Transport::SERIAL, kSerialLeaseMs);
  (void)StartSamplerTask(probe.vendor, core, sampling_priority);
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
  /// 有网络客户端时置 true，可为空（给状态灯）/ Set to true while a network client is
  /// connected; may be null (for the status light).
  std::atomic<bool>* client;
};

/// @brief 连 Wi-Fi，启动 TCP（OpenOCD）和 USB/IP 服务及 mDNS / Join Wi-Fi and start the
///        TCP (OpenOCD) and USB/IP services plus mDNS.
/// @return Wi-Fi 没有启动（没有热点参数）或服务任务建不起来时返回 false / False when
///         Wi-Fi is not started (no credentials) or the server task cannot be created.
/// @note app_main 里调用一次。最多阻塞 Config::kWifiWaitMs；没连上时 Wi-Fi 在后台重连，
///       服务照常启动 / Call once from app_main. Blocks for up to Config::kWifiWaitMs; if
///       not yet joined, Wi-Fi keeps reconnecting in the background and the services
///       start anyway.
template <typename DapT>
bool StartNetwork(ProbeCore<DapT>& probe, const NetworkOptions& opt)
{
  const esp_err_t wifi = WifiStaStart(Config::kWifiWaitMs);
  if (wifi != ESP_OK && wifi != ESP_ERR_TIMEOUT)
  {
    return false;
  }
  static DapTcpServer<Executor<DapT>> server(probe.exec, Config::kTcpPort, opt.client);
  server.EnableUsbip(Config::kUsbipPort, opt.usb_product, ProbeSerial());
  if (!server.Start(opt.core))
  {
    return false;
  }
  (void)MdnsStart(opt.chip, Config::kTcpPort);
  return true;
}

}  // namespace XrDap
