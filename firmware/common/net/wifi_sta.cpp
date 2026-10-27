// SPDX-License-Identifier: Apache-2.0
// Wi-Fi STA 实现：热点参数存在 NVS，掉线后在事件回调里无限重连。
// Wi-Fi STA implementation: credentials live in NVS; the event handler reconnects forever
// after a drop.
#include "wifi_sta.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "restart.hpp"

namespace XrDap
{
namespace
{
constexpr EventBits_t kGotIp = BIT0;
EventGroupHandle_t s_events = nullptr;
std::atomic<bool> s_started{false};
// WifiStaInitStorage 在别的任务启动前写一次 / Written once by WifiStaInitStorage before
// other tasks start.
bool s_storage_repaired = false;

struct Credentials
{
  uint8_t version;
  char ssid[kSsidMax + 1U];
  char password[kPasswordMax + 1U];
};

// 命名空间沿用早期固件的名字，已经配过网的板子升级后不用重配 / The namespace keeps the
// early firmware's name so configured boards keep their credentials after an upgrade.
constexpr const char* kNvsNamespace = "rmlink";
constexpr const char* kNvsKey = "wifi";
constexpr const char* kNvsEraseKey = "wifi_erase";

esp_err_t LoadCredentials(Credentials& cred)
{
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &nvs);
  if (err != ESP_OK)
  {
    return err;
  }
  size_t size = sizeof(cred);
  err = nvs_get_blob(nvs, kNvsKey, &cred, &size);
  nvs_close(nvs);
  if (err != ESP_OK || size != sizeof(cred) || cred.version != 1 || cred.ssid[0] == 0)
  {
    return ESP_ERR_NOT_FOUND;
  }
  cred.ssid[sizeof(cred.ssid) - 1] = 0;
  cred.password[sizeof(cred.password) - 1] = 0;
  return ESP_OK;
}

esp_err_t SaveCredentials(const char* ssid, const char* password)
{
  Credentials cred = {};
  cred.version = 1;
  memcpy(cred.ssid, ssid, strlen(ssid));
  memcpy(cred.password, password, strlen(password));
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
  if (err == ESP_OK)
  {
    err = nvs_set_blob(nvs, kNvsKey, &cred, sizeof(cred));
    if (err == ESP_OK)
    {
      err = nvs_commit(nvs);
    }
    nvs_close(nvs);
  }
  memset(&cred, 0, sizeof(cred));
  return err;
}

// 删除热点参数并在同一次提交里记下来源 / Erase the credentials and record the source in
// the same commit.
esp_err_t ClearCredentials(EraseSource source)
{
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
  if (err != ESP_OK)
  {
    return err;
  }
  err = nvs_erase_key(nvs, kNvsKey);
  if (err == ESP_ERR_NVS_NOT_FOUND)
  {
    err = ESP_OK;
  }
  if (err == ESP_OK)
  {
    err = nvs_set_u8(nvs, kNvsEraseKey, static_cast<uint8_t>(source));
  }
  if (err == ESP_OK)
  {
    err = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return err;
}

EraseSource LoadEraseSource()
{
  nvs_handle_t nvs;
  uint8_t value = 0U;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &nvs) == ESP_OK)
  {
    (void)nvs_get_u8(nvs, kNvsEraseKey, &value);
    nvs_close(nvs);
  }
  return static_cast<EraseSource>(value);
}

WifiStatus ReadStatus()
{
  WifiStatus st = {};
  Credentials cred = {};
  if (LoadCredentials(cred) == ESP_OK)
  {
    memcpy(st.ssid, cred.ssid, sizeof(st.ssid));
  }
  memset(&cred, 0, sizeof(cred));
  st.storage_repaired = s_storage_repaired;
  st.last_erase = LoadEraseSource();
  st.reset_reason = LastResetReason();
  LastWatchdogTasks(st.watchdog_task);
  st.started = s_started;
  if (st.started)
  {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    if (netif != nullptr && esp_netif_get_ip_info(netif, &ip) == ESP_OK &&
        ip.ip.addr != 0)
    {
      st.connected = true;
      memcpy(st.ip, &ip.ip.addr, 4);
    }
    wifi_ap_record_t ap = {};
    if (st.connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
      st.rssi = ap.rssi;
    }
  }
  return st;
}

/// NVS 里的热点参数 / Credentials kept in NVS.
class NvsWifiSettings final : public WifiSettings
{
 public:
  WifiStatus Status() override { return ReadStatus(); }

  SaveResult Save(const char* ssid, const char* password) override
  {
    if (!CredentialsValid(ssid, password))
    {
      return SaveResult::INVALID;
    }
    return SaveCredentials(ssid, password) == ESP_OK ? SaveResult::OK
                                                     : SaveResult::STORAGE;
  }

  bool Clear(EraseSource source) override { return ClearCredentials(source) == ESP_OK; }

  void RestartLater() override { XrDap::RestartLater(); }
};

void OnEvent(void*, esp_event_base_t base, int32_t id, void*)
{
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
  {
    xEventGroupClearBits(s_events, kGotIp);
    esp_wifi_connect();
  }
  else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
  {
    xEventGroupSetBits(s_events, kGotIp);
  }
}
}  // namespace

esp_err_t WifiStaInitStorage()
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    err = nvs_flash_erase();
    if (err == ESP_OK)
    {
      err = nvs_flash_init();
    }
    if (err == ESP_OK)
    {
      s_storage_repaired = true;
      err = ClearCredentials(EraseSource::STORAGE_REPAIR);
    }
  }
  return err;
}

esp_err_t WifiStaStart(uint32_t timeout_ms)
{
  Credentials cred = {};
  esp_err_t err = LoadCredentials(cred);
  if (err != ESP_OK)
  {
    return err;
  }

  s_events = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(
      esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, OnEvent, nullptr));
  ESP_ERROR_CHECK(
      esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, OnEvent, nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  wifi_config_t wifi = {};
  memcpy(wifi.sta.ssid, cred.ssid, sizeof(wifi.sta.ssid));
  memcpy(wifi.sta.password, cred.password, sizeof(wifi.sta.password));
  wifi.sta.threshold.authmode = cred.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  memset(&cred, 0, sizeof(cred));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
  memset(&wifi, 0, sizeof(wifi));
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_wifi_set_ps(WIFI_PS_NONE);
  s_started = true;

  const EventBits_t bits =
      xEventGroupWaitBits(s_events, kGotIp, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
  return (bits & kGotIp) ? ESP_OK : ESP_ERR_TIMEOUT;
}

WifiSettings& WifiStaSettings()
{
  static NvsWifiSettings settings;
  return settings;
}

WifiLink WifiStaGetLink()
{
  if (!s_started)
  {
    return WifiLink::OFF;
  }
  return (xEventGroupGetBits(s_events) & kGotIp) != 0 ? WifiLink::UP : WifiLink::JOINING;
}

}  // namespace XrDap
