// SPDX-License-Identifier: Apache-2.0
#pragma once

// 探针身份：DAP_Info 里的厂商、产品、序列号、版本，以及 mDNS 主机名。序列号 = 前缀 +
// 出厂 MAC（大写十六进制），与 S3 版 USB 序列号相同，上位机靠它在 USB、串口、TCP 上认出
// 同一块探针。
//
// Probe identity: vendor, product, serial and version for DAP_Info, plus the mDNS host
// name. Serial = prefix + factory MAC (upper-case hex), identical to the S3 USB serial,
// so host tools can recognise one probe across USB, serial and TCP.

#include <cstdint>
#include <cstdio>

#include "esp_mac.h"

namespace XrDap
{

/// 探针固件版本 / Probe firmware version.
inline constexpr char kFirmwareVersion[] = "0.2.0";
/// 序列号前缀 / Serial number prefix.
inline constexpr char kSerialPrefix[] = "XRobot-XRDAP-";

/// 出厂 MAC（eFuse 中的 6 字节），首次调用时读取并缓存；返回的指针永久有效 / Factory MAC
/// (6 bytes from eFuse), read and cached on first call; the pointer stays valid forever.
inline const uint8_t* ProbeMac()
{
  static uint8_t mac[6] = {};
  static bool read = false;
  if (!read)
  {
    (void)esp_efuse_mac_get_default(mac);
    read = true;
  }
  return mac;
}

/// 序列号，例如 XRobot-XRDAP-E8069093EF1C；静态缓冲，首次调用不可并发 / Serial, e.g.
/// XRobot-XRDAP-E8069093EF1C; static buffer, so the first call must not race.
inline const char* ProbeSerial()
{
  static char text[sizeof(kSerialPrefix) + 12] = {};
  if (text[0] == 0)
  {
    const uint8_t* m = ProbeMac();
    snprintf(text, sizeof(text), "%s%02X%02X%02X%02X%02X%02X", kSerialPrefix, m[0], m[1],
             m[2], m[3], m[4], m[5]);
  }
  return text;
}

/// mDNS 主机名，例如 xrdap-93ef1c（MAC 后 3 字节，小写）；线程约束同 ProbeSerial() / mDNS
/// host name, e.g. xrdap-93ef1c (last 3 MAC bytes, lower case); same threading rule as
/// ProbeSerial().
inline const char* ProbeHostname()
{
  static char text[16] = {};
  if (text[0] == 0)
  {
    const uint8_t* m = ProbeMac();
    snprintf(text, sizeof(text), "xrdap-%02x%02x%02x", m[3], m[4], m[5]);
  }
  return text;
}

/// @brief 把探针身份写入 DAP 实例的 DAP_Info 字符串 / Write the probe identity into the
///        DAP instance's DAP_Info strings.
/// @param dap DAP 实例 / DAP instance.
/// @param product 产品名，例如 "XR-DAP-S3"，需长期有效 / Product name, e.g. "XR-DAP-S3";
///        must stay valid.
template <typename DapT>
void SetProbeInfo(DapT& dap, const char* product)
{
  dap.SetInfoStrings({
      "XRobot",          // 厂商
      product,           // 产品
      ProbeSerial(),     // 序列号
      "2.0.0",           // CMSIS-DAP 协议版本
      nullptr,           // 目标芯片厂商（不固定）
      nullptr,           // 目标芯片名
      "XRobot",          // 板子厂商
      product,           // 板子名
      kFirmwareVersion,  // 探针固件版本
  });
}

}  // namespace XrDap
