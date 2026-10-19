// SPDX-License-Identifier: Apache-2.0
#include "probe_info.hpp"

#include <cstdio>

#include "esp_mac.h"

namespace XrDap
{
namespace
{
struct Identity
{
  uint8_t mac[6] = {};
  char serial[sizeof(kSerialPrefix) + 12] = {};
  char hostname[16] = {};
};

// 局部静态量的初始化是线程安全的，任何任务第一次调用都可以。
const Identity& GetIdentity()
{
  static const Identity id = []
  {
    Identity v;
    (void)esp_efuse_mac_get_default(v.mac);
    const uint8_t* m = v.mac;
    snprintf(v.serial, sizeof(v.serial), "%s%02X%02X%02X%02X%02X%02X", kSerialPrefix,
             m[0], m[1], m[2], m[3], m[4], m[5]);
    snprintf(v.hostname, sizeof(v.hostname), "xrdap-%02x%02x%02x", m[3], m[4], m[5]);
    return v;
  }();
  return id;
}
}  // namespace

const uint8_t* ProbeMac() { return GetIdentity().mac; }
const char* ProbeSerial() { return GetIdentity().serial; }
const char* ProbeHostname() { return GetIdentity().hostname; }

}  // namespace XrDap
