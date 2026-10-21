// SPDX-License-Identifier: Apache-2.0
#pragma once

// XR 厂商扩展命令（0x80 到 0x85）：多地址读写、指针链解引用、探针端周期采样。还可挂一条
// 附加命令（SetExtraCommand，例如 0x86 Wi-Fi 配置）。通过 libxr DAP 类的命令钩子接入，
// USB、TCP、串口三种传输行为一致。协议见 docs/protocol.md 第 3 节。
//
// 采样归发起它的传输所有：USB 发起的在总线复位后停止，串口发起的在 kSerialLeaseMs 内没有
// 请求时停止，TCP 发起的在客户端断开时由 TCP 服务调用 OnTransportClosed() 停止。
//
// XR vendor extension commands (0x80 to 0x85): multi-address read/write, pointer-chain
// dereference and probe-side periodic sampling. One extra command can be attached
// (SetExtraCommand, e.g. 0x86 Wi-Fi configuration). They plug in through the libxr DAP
// class command hook and behave identically over USB, TCP and serial. See
// docs/protocol.md section 3.
//
// Sampling belongs to the transport that started it: USB-started sampling stops after a
// bus reset, serial-started sampling stops after kSerialLeaseMs without a request, and
// TCP-started sampling stops when the TCP server calls OnTransportClosed() on disconnect.

#include <atomic>
#include <cstdint>

#include "dap_activity.hpp"
#include "debug/swd.hpp"
#include "mem_ap.hpp"
#include "sampler.hpp"
#include "vendor_protocol.hpp"

namespace XrDap
{

/// XR 厂商扩展命令处理器 / XR vendor extension command handler.
/// @note 命令在 DAP 命令线程中执行，采样在 Begin() 创建的任务中执行；两者经 MemAp::Lock
///       串行访问 SWD / Commands run on the DAP command thread and sampling runs in the
///       task created by Begin(); MemAp::Lock serialises their SWD access.
class DapVendor
{
 public:
  /// 串口发起的采样在这么久收不到任何请求后自动停止 / Serial-started sampling stops
  /// after this long without any request.
  static constexpr uint32_t kSerialLeaseMs = 5000U;

  /// 当前驱动 DAP 的传输 / Transport currently driving the DAP.
  enum class Transport : uint8_t
  {
    USB = 0,     ///< USB 接口 / USB interface.
    TCP = 1,     ///< TCP 连接（含 USB/IP）/ TCP connection (including USB/IP).
    SERIAL = 2,  ///< 串口链路 / Serial link.
  };

  /// 附加命令处理函数，返回 true 表示已填好回复 / Extra command handler; returns true
  /// once the reply is filled in.
  using ExtraHandler = bool (*)(void* ctx, const uint8_t* req, uint16_t len,
                                uint8_t* resp, uint16_t cap, uint16_t& out);
  /// 读取 USB 总线复位计数的函数 / Function returning the USB bus-reset count.
  using ResetCountFn = uint32_t (*)(void*);

  /// @param swd SWD 实现，生命周期需长于本对象 / SWD implementation; must outlive this
  ///        object.
  /// @param max_packet DAP 包长 / DAP packet size.
  DapVendor(LibXR::Debug::Swd& swd, uint16_t max_packet)
      : max_packet_(max_packet), mem_(swd), sampler_(mem_)
  {
  }

  /// @brief 在指定的核上启动采样任务 / Start the sampling task on a core.
  /// @param core 必须是 SWD 所在的核 / Must be the core that owns the SWD pins.
  /// @param task_prio 采样任务优先级，应为该核上最高 / Sampling task priority; should be
  ///        the highest on that core.
  /// @note 只调用一次，且在命令钩子开始使用之前 / Call once, before the command hook is
  ///       used.
  void Begin(int core, uint32_t task_prio);

  /// @brief 挂一条附加命令，其 feature 并入 INFO 功能位 / Attach one extra command whose
  ///        feature bit is merged into INFO.
  /// @param cmd 命令字节，应在 0x85 之后 / Command byte, expected above 0x85.
  /// @note 在 Begin() 之前调用，只能挂一条 / Call before Begin(); only one is supported.
  void SetExtraCommand(uint8_t cmd, uint32_t feature, ExtraHandler handler, void* ctx);

  /// 由传输在执行 DAP 命令之前设置当前传输 / Set by a transport before it executes DAP
  /// commands.
  void SetTransport(Transport t) { transport_ = t; }

  /// 串口收到一个请求时调用，刷新串口采样的租期 / Called on every serial request to
  /// refresh the serial sampling lease.
  void NotifySerialActivity();

  /// 拥有探针的传输断开（TCP 客户端关闭、USB 复位等）时调用：停止采样并清除连接状态 /
  /// Call when the transport owning the probe goes away (TCP client closed, USB reset,
  /// ...): stops sampling and clears the connection state.
  void OnTransportClosed();

  /// @brief 网络客户端（TCP 或 USB/IP）接管探针：别的传输发起的采样作废 / A network
  ///        client (TCP or USB/IP) takes over the probe; sampling started by another
  ///        transport is dropped.
  void OnNetworkClientConnected();

  /// 设置 USB 复位计数来源 / Set the USB reset-count source.
  void SetUsbResetSource(ResetCountFn fn, void* ctx);

  /// DAP 活动和上位机状态，给状态灯读 / DAP activity and host state, read by status
  /// lights.
  const Vendor::DapActivity& Activity() const { return activity_; }

  /// libxr DAP 类的命令钩子入口，返回 true 表示命令已处理（回复在 resp/out）/ Entry for
  /// the libxr DAP class command hook; returns true when the command was handled (reply
  /// in resp/out).
  static bool Hook(void* ctx, const uint8_t* req, uint16_t len, uint8_t* resp,
                   uint16_t cap, uint16_t& out);

 private:
  static constexpr uint32_t kCommandLockMs = 2000U;

  static bool KeepSampling(void* ctx);

  bool OnCommand(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                 uint16_t& out);
  bool IsVendor(uint8_t cmd) const;
  bool HandleVendor(uint8_t cmd, const uint8_t* req, uint16_t len, uint8_t* resp,
                    uint16_t cap, uint16_t& out);
  bool CmdInfo(uint16_t len, uint8_t* resp, uint16_t cap, uint16_t& out);
  bool CmdRead(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
               uint16_t& out);
  bool CmdWrite(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                uint16_t& out);
  bool CmdSubSet(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                 uint16_t& out);
  bool CmdSubCtrl(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t& out);
  bool CmdSubRead(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                  uint16_t& out);
  bool StartSampling();

  uint16_t max_packet_;
  Vendor::MemAp mem_;
  Vendor::Sampler sampler_;
  Vendor::DapActivity activity_;

  std::atomic<Transport> transport_{Transport::USB};
  std::atomic<Transport> owner_{Transport::USB};
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> last_serial_ms_{0};

  uint8_t extra_cmd_ = 0U;
  uint32_t extra_feature_ = 0U;
  ExtraHandler extra_handler_ = nullptr;
  void* extra_ctx_ = nullptr;

  ResetCountFn usb_reset_fn_ = nullptr;
  void* usb_reset_ctx_ = nullptr;
  uint32_t usb_reset_base_ = 0U;
};

}  // namespace XrDap
