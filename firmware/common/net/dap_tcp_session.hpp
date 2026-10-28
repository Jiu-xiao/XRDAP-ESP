// SPDX-License-Identifier: Apache-2.0
#pragma once

// CMSIS-DAP over TCP 的一个连接：读请求帧、执行、回响应帧。帧为 8 字节头加 DAP 数据：
// 'D' 'A' 'P' 0，2 字节小端长度，类型（1 请求、2 响应），1 字节保留，与 OpenOCD 的
// cmsis_dap_tcp.c 一致。只依赖一个字节流，不依赖 lwIP，电脑上可测。
//
// One CMSIS-DAP over TCP connection: read a request frame, execute it, send the response
// frame. A frame is an 8-byte header plus DAP data: 'D' 'A' 'P' 0, a 2-byte
// little-endian length, a type (1 request, 2 response) and 1 reserved byte, as in
// OpenOCD's cmsis_dap_tcp.c. It only needs a byte stream, not lwIP, so it is PC-testable.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace XrDap
{

/// CMSIS-DAP over TCP 的一个连接 / One CMSIS-DAP over TCP connection.
/// @tparam DapT 需提供 ExecuteExternal 和 ExecuteControl(req, len, resp, cap, out_len) /
///         Must provide ExecuteExternal and ExecuteControl(req, len, resp, cap, out_len).
/// @tparam kMaxPayload 一个 DAP 包的上限 / Limit of one DAP packet.
template <typename DapT, uint16_t kMaxPayload>
class DapTcpSession
{
 public:
  /// 帧头长度 / Header length.
  static constexpr size_t kHeaderLen = 8U;
  /// 请求帧类型 / Request frame type.
  static constexpr uint8_t kTypeRequest = 1U;
  /// 响应帧类型 / Response frame type.
  static constexpr uint8_t kTypeResponse = 2U;
  /// 控制请求帧类型：探针自己的设置，不是 DAP 命令（OpenOCD 不会发）/ Control request
  /// frame type: the probe's own settings, not a DAP command (OpenOCD never sends it).
  static constexpr uint8_t kTypeControlRequest = 3U;
  /// 控制应答帧类型 / Control response frame type.
  static constexpr uint8_t kTypeControlResponse = 4U;

  /// @param dap 执行请求的 DAP 实例，生命周期需长于本对象 / DAP instance that runs the
  ///        requests; must outlive this object.
  explicit DapTcpSession(DapT& dap) : dap_(dap) {}

  /// @brief 按顺序执行请求并应答，直到对方关闭、出错或收到无效帧头 / Execute and answer
  ///        requests in order until the peer closes, an error occurs or a header is
  ///        invalid.
  /// @tparam StreamT 需提供 bool ReadAll(uint8_t*, size_t) 和
  ///         bool WriteAll(const uint8_t*, size_t)，读满或写完才返回 true / Must provide
  ///         bool ReadAll(uint8_t*, size_t) and bool WriteAll(const uint8_t*, size_t),
  ///         returning true only when the whole buffer was transferred.
  /// @note 无效帧头：签名不是 "DAP\0"、类型不是请求或控制请求、长度为 0 或大于
  ///       kMaxPayload。没有应答的命令（DAP_TransferAbort）不回帧 / Invalid header:
  ///       signature not "DAP\0", type neither request nor control request, length 0 or
  ///       above kMaxPayload. A command without a response (DAP_TransferAbort) gets no
  ///       frame.
  template <typename StreamT>
  void Serve(StreamT& io)
  {
    for (;;)
    {
      uint8_t hdr[kHeaderLen];
      if (!io.ReadAll(hdr, sizeof(hdr)))
      {
        return;
      }
      const uint16_t len = static_cast<uint16_t>(hdr[4] | (hdr[5] << 8));
      const bool control = hdr[6] == kTypeControlRequest;
      if (memcmp(hdr, "DAP\0", 4) != 0 || (hdr[6] != kTypeRequest && !control) ||
          len == 0U || len > kMaxPayload)
      {
        return;
      }
      if (!io.ReadAll(req_, len))
      {
        return;
      }

      uint16_t out_len = 0U;
      if (control)
      {
        (void)dap_.ExecuteControl(req_, len, out_ + kHeaderLen, kMaxPayload, out_len);
      }
      else
      {
        (void)dap_.ExecuteExternal(req_, len, out_ + kHeaderLen, kMaxPayload, out_len);
      }
      if (out_len == 0U)
      {
        continue;
      }
      memcpy(out_, "DAP\0", 4);
      out_[4] = static_cast<uint8_t>(out_len & 0xFFU);
      out_[5] = static_cast<uint8_t>(out_len >> 8);
      out_[6] = control ? kTypeControlResponse : kTypeResponse;
      out_[7] = 0U;
      if (!io.WriteAll(out_, kHeaderLen + out_len))
      {
        return;
      }
    }
  }

 private:
  DapT& dap_;
  uint8_t req_[kMaxPayload] = {};
  // 帧头和数据放在同一块缓冲里，一个响应只写一次 / Header and data share one buffer, so
  // one response is one write.
  uint8_t out_[kHeaderLen + kMaxPayload] = {};
};

}  // namespace XrDap
