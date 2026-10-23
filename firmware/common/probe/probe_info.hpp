// SPDX-License-Identifier: Apache-2.0
#pragma once

// ESP 探针的身份：固件版本、出厂 MAC 和由它得出的序列号与 mDNS 主机名。序列号规则和
// DAP_Info 字符串来自 XRDAP-Core 的 probe_identity.hpp；这里的标识是出厂 MAC，与 S3 的
// USB 序列号相同。
//
// Identity of an ESP probe: firmware version, factory MAC, and the serial and mDNS host
// name derived from it. The serial rule and the DAP_Info strings come from XRDAP-Core's
// probe_identity.hpp; the id here is the factory MAC, identical to the S3 USB serial.

#include <cstdint>

#include "sdkconfig.h"
#include "xrdap/probe/probe_identity.hpp"

namespace XrDap
{

/// 版本后缀，Kconfig 的 XRDAP_VERSION_SUFFIX；还没在实物上测过的板子是 "-preview" /
/// Version suffix from Kconfig XRDAP_VERSION_SUFFIX; "-preview" for a board not yet
/// tested on hardware.
#ifdef CONFIG_XRDAP_VERSION_SUFFIX
#define XRDAP_VERSION_SUFFIX CONFIG_XRDAP_VERSION_SUFFIX
#else
#define XRDAP_VERSION_SUFFIX ""
#endif

/// 探针固件版本，DAP_Info 和 mDNS 里报告 / Probe firmware version, reported in DAP_Info
/// and mDNS.
inline constexpr char kFirmwareVersion[] = "0.5.5" XRDAP_VERSION_SUFFIX;

/// 出厂 MAC（eFuse 中的 6 字节）/ Factory MAC (6 bytes from eFuse).
const uint8_t* ProbeMac();
/// 序列号，例如 XRobot-XRDAP-0123456789AB / Serial, e.g. XRobot-XRDAP-0123456789AB.
const char* ProbeSerial();
/// mDNS 主机名，例如 xrdap-6789ab（MAC 后 3 字节，小写）/ mDNS host name, e.g.
/// xrdap-6789ab (last 3 MAC bytes, lower case).
const char* ProbeHostname();

}  // namespace XrDap
