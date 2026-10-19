// SPDX-License-Identifier: Apache-2.0
#pragma once

// 串口 DAP 链路与本探针 DAP 实例之间的适配层。libxr 的 DapSerialTransport 负责 Topic
// 帧、CRC、重新同步和工作线程，只要求一个带 ExecuteExternal
// 的对象。本适配器在每个请求前后
// 告诉厂商扩展当前传输是串口，并刷新串口采样的租期。厂商命令仍由 DAP 实例自己的命令钩子
// （USB、TCP 共用）处理，不再挂第二个钩子。
//
// Adapter between the serial DAP link and this probe's DAP instance. libxr's
// DapSerialTransport handles Topic frames, CRC, resync and the worker thread, and only
// needs an object with ExecuteExternal. This adapter tells the vendor extension that the
// serial link is active around each request and refreshes the serial sampling lease.
// Vendor commands still go through the DAP instance's own command hook (shared with USB
// and TCP); no second hook is installed.

#include <cstdint>

#include "dap_vendor.hpp"
#include "libxr.hpp"

namespace XrDap
{

/// 串口链路用的 DAP 适配器 / DAP adapter for the serial link.
/// @tparam DapType 需提供 ExecuteExternal() / Must provide ExecuteExternal().
template <typename DapType>
class SerialDap
{
 public:
  /// @param dap 执行请求的 DAP 实例 / DAP instance that runs requests.
  /// @param vendor 厂商扩展 / Vendor extension.
  /// @note 两者生命周期都需长于本对象 / Both must outlive this object.
  SerialDap(DapType& dap, DapVendor& vendor) : dap_(dap), vendor_(vendor) {}

  /// @brief 执行一个 DAP 请求，由串口链路工作线程调用 / Execute one DAP request; called
  ///        from the serial link worker thread.
  /// @param req 请求数据 / Request bytes.
  /// @param req_len 请求长度 / Request length.
  /// @param resp 响应缓冲 / Response buffer.
  /// @param resp_cap 响应缓冲容量 / Response buffer capacity.
  /// @param out_len 实际响应长度 / Actual response length.
  /// @return DAP 实例返回的错误码 / Error code from the DAP instance.
  /// @note 返回前把传输标记恢复为 USB / The transport marker is reset to USB on return.
  LibXR::ErrorCode ExecuteExternal(const uint8_t* req, uint16_t req_len, uint8_t* resp,
                                   uint16_t resp_cap, uint16_t& out_len)
  {
    vendor_.NotifySerialActivity();
    vendor_.SetTransport(DapVendor::Transport::SERIAL);
    const LibXR::ErrorCode ec =
        dap_.ExecuteExternal(req, req_len, resp, resp_cap, out_len);
    vendor_.SetTransport(DapVendor::Transport::USB);
    return ec;
  }

 private:
  DapType& dap_;
  DapVendor& vendor_;
};

}  // namespace XrDap
