// SPDX-License-Identifier: Apache-2.0
#pragma once

// 用 mDNS 在局域网广播探针：服务 _xrdap._tcp（端口即 TCP DAP 端口），主机名
// xrdap-xxxxxx.local，TXT 记录含序列号、芯片和固件版本。上位机靠它找到 Wi-Fi 上的探针，
// OpenOCD 也可以直接写 cmsis-dap tcp host xrdap-xxxxxx.local。
//
// Advertises the probe on the LAN via mDNS: service _xrdap._tcp (on the TCP DAP port),
// host name xrdap-xxxxxx.local, and TXT records with serial, chip and firmware version.
// Host tools use it to find Wi-Fi probes; OpenOCD can also use
// "cmsis-dap tcp host xrdap-xxxxxx.local" directly.

#include <cstdint>

#include "esp_log.h"
#include "mdns.h"
#include "probe_info.hpp"

namespace XrDap
{

/// @brief 启动 mDNS 广播，失败只记日志 / Start mDNS advertising; failures are only
///        logged.
/// @param chip 写入 TXT 的芯片名，例如 "esp32c3"，需长期有效 / Chip name for the TXT
///        record, e.g. "esp32c3"; must stay valid.
/// @param port TCP DAP 端口 / TCP DAP port.
/// @note 在 Wi-Fi 协议栈启动后调用一次，拿到 IP 后才开始应答 / Call once after the Wi-Fi
///       stack is up; answers begin once an IP is obtained.
inline void MdnsStart(const char* chip, uint16_t port)
{
  static constexpr const char* TAG = "xrdap_mdns";
  if (mdns_init() != ESP_OK)
  {
    ESP_LOGW(TAG, "mDNS 启动失败");
    return;
  }
  (void)mdns_hostname_set(ProbeHostname());
  (void)mdns_instance_name_set(ProbeSerial());
  mdns_txt_item_t txt[] = {
      {"serial", ProbeSerial()},
      {"chip", chip},
      {"fw", kFirmwareVersion},
  };
  if (mdns_service_add(ProbeSerial(), "_xrdap", "_tcp", port, txt,
                       sizeof(txt) / sizeof(txt[0])) != ESP_OK)
  {
    ESP_LOGW(TAG, "mDNS 服务登记失败");
    return;
  }
  ESP_LOGI(TAG, "mDNS：%s.local，服务 _xrdap._tcp 端口 %u", ProbeHostname(),
           (unsigned)port);
}

}  // namespace XrDap
