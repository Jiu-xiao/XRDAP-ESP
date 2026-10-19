// SPDX-License-Identifier: Apache-2.0
#pragma once

// 串口 DAP 链路和本探针 DAP 实例之间的适配层。
// libxr 的 DapSerialTransport 负责 Topic 帧、CRC、重新同步和工作线程，只要求一个带
// ExecuteExternal 的对象。这里在每个请求前后告诉厂商扩展"现在是串口在用"，并刷新串口采样的租期。
// 厂商命令仍由 DAP 实例自己的命令钩子处理（USB、TCP 共用的那一个），这里不再挂第二个钩子。

#include <cstdint>

#include "dap_vendor.hpp"
#include "libxr.hpp"

namespace XrDap
{

template <typename DapType>
class SerialDap
{
 public:
  SerialDap(DapType& dap, DapVendor& vendor) : dap_(dap), vendor_(vendor) {}

  LibXR::ErrorCode ExecuteExternal(const uint8_t* req, uint16_t req_len, uint8_t* resp,
                                   uint16_t resp_cap, uint16_t& out_len)
  {
    vendor_.NotifySerialActivity();
    vendor_.SetTransport(DapVendor::Transport::SERIAL);
    const LibXR::ErrorCode ec = dap_.ExecuteExternal(req, req_len, resp, resp_cap, out_len);
    vendor_.SetTransport(DapVendor::Transport::USB);
    return ec;
  }

 private:
  DapType& dap_;
  DapVendor& vendor_;
};

}  // namespace XrDap
