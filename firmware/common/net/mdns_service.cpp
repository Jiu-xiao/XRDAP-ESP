// SPDX-License-Identifier: Apache-2.0
#include "mdns_service.hpp"

#include "esp_log.h"
#include "mdns.h"
#include "probe_info.hpp"

namespace XrDap
{
namespace
{
constexpr const char* TAG = "xrdap_mdns";
}  // namespace

void MdnsStart(const char* chip, uint16_t port)
{
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
