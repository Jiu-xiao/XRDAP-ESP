// SPDX-License-Identifier: Apache-2.0
#pragma once

// 探针身份：DAP_Info 里的厂商、产品、序列号、版本，以及 mDNS 主机名。序列号 = 前缀 +
// 出厂 MAC（大写十六进制），与 S3 的 USB 序列号相同，上位机靠它在 USB、串口、TCP 上认出
// 同一块探针。
//
// Probe identity: vendor, product, serial and version for DAP_Info, plus the mDNS host
// name. Serial = prefix + factory MAC (upper-case hex), identical to the S3 USB serial,
// so host tools can recognise one probe across USB, serial and TCP.

#include <cstdint>

namespace XrDap
{

/// 探针固件版本 / Probe firmware version.
inline constexpr char kFirmwareVersion[] = "0.4.0";
/// 序列号前缀 / Serial number prefix.
inline constexpr char kSerialPrefix[] = "XRobot-XRDAP-";
/// 厂商名 / Vendor name.
inline constexpr char kVendorName[] = "XRobot";

/// 出厂 MAC（eFuse 中的 6 字节）/ Factory MAC (6 bytes from eFuse).
const uint8_t* ProbeMac();
/// 序列号，例如 XRobot-XRDAP-E8069093EF1C / Serial, e.g. XRobot-XRDAP-E8069093EF1C.
const char* ProbeSerial();
/// mDNS 主机名，例如 xrdap-93ef1c（MAC 后 3 字节，小写）/ mDNS host name, e.g.
/// xrdap-93ef1c (last 3 MAC bytes, lower case).
const char* ProbeHostname();

/// @brief 把探针身份写入 DAP 实例的 DAP_Info 字符串 / Write the probe identity into the
///        DAP instance's DAP_Info strings.
/// @param product 产品名，例如 "XR-DAP-S3"，需长期有效 / Product name, e.g. "XR-DAP-S3";
///        must stay valid.
template <typename DapT>
void SetProbeInfo(DapT& dap, const char* product)
{
  dap.SetInfoStrings({
      kVendorName,       // 厂商
      product,           // 产品
      ProbeSerial(),     // 序列号
      "2.0.0",           // CMSIS-DAP 协议版本
      nullptr,           // 目标芯片厂商（不固定）
      nullptr,           // 目标芯片名
      kVendorName,       // 板子厂商
      product,           // 板子名
      kFirmwareVersion,  // 探针固件版本
  });
}

}  // namespace XrDap
