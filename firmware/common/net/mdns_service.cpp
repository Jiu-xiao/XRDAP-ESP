// SPDX-License-Identifier: Apache-2.0
#include "mdns_service.hpp"

#include "mdns.h"
#include "probe_info.hpp"

namespace XrDap
{

bool MdnsStart(const char* chip, uint16_t port)
{
  if (mdns_init() != ESP_OK)
  {
    return false;
  }
  (void)mdns_hostname_set(ProbeHostname());
  (void)mdns_instance_name_set(ProbeSerial());
  mdns_txt_item_t txt[] = {
      {"serial", ProbeSerial()},
      {"chip", chip},
      {"fw", kFirmwareVersion},
  };
  return mdns_service_add(ProbeSerial(), "_xrdap", "_tcp", port, txt,
                          sizeof(txt) / sizeof(txt[0])) == ESP_OK;
}

}  // namespace XrDap
