// SPDX-License-Identifier: Apache-2.0
// Wi-Fi STA 实现：热点参数存在 NVS，掉线后在事件回调里无限重连。
// Wi-Fi STA implementation: credentials live in NVS; the event handler reconnects forever
// after a drop.
#include "wifi_sta.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

namespace
{
constexpr const char* TAG = "xrdap_wifi";
constexpr EventBits_t kGotIp = BIT0;
EventGroupHandle_t s_events = nullptr;
std::atomic<bool> s_started{false};

struct Credentials
{
  uint8_t version;
  char ssid[33];
  char password[64];
};

// 命名空间沿用早期固件的名字，已经配过网的板子升级后不用重配
constexpr const char* kNvsNamespace = "rmlink";
constexpr const char* kNvsKey = "wifi";

esp_err_t LoadCredentials(Credentials& cred)
{
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &nvs);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "没有保存的热点参数（nvs_open: %s）", esp_err_to_name(err));
    return err;
  }
  size_t size = sizeof(cred);
  err = nvs_get_blob(nvs, kNvsKey, &cred, &size);
  nvs_close(nvs);
  if (err != ESP_OK || size != sizeof(cred) || cred.version != 1 || cred.ssid[0] == 0)
  {
    ESP_LOGW(TAG, "保存的热点参数缺失或无效");
    return ESP_ERR_NOT_FOUND;
  }
  cred.ssid[sizeof(cred.ssid) - 1] = 0;
  cred.password[sizeof(cred.password) - 1] = 0;
  return ESP_OK;
}

void OnEvent(void*, esp_event_base_t base, int32_t id, void* data)
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
    auto* ev = static_cast<ip_event_got_ip_t*>(data);
    ESP_LOGI(TAG, "已获得 IP " IPSTR, IP2STR(&ev->ip_info.ip));
    xEventGroupSetBits(s_events, kGotIp);
  }
}
}  // namespace

namespace XrDap
{
esp_err_t WifiStaStart(uint32_t timeout_ms)
{
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK)
  {
    return err;  // 不要擦除 NVS，里面存着热点参数
  }

  Credentials cred = {};
  err = LoadCredentials(cred);
  if (err != ESP_OK)
  {
    return err;
  }
  ESP_LOGI(TAG, "使用已保存的热点，SSID 长度 %u", (unsigned)strlen(cred.ssid));

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

esp_err_t WifiStaSave(const char* ssid, const char* password)
{
  const size_t ssid_len = strlen(ssid);
  const size_t pwd_len = strlen(password);
  if (ssid_len < 1 || ssid_len > 32 || (pwd_len != 0 && (pwd_len < 8 || pwd_len > 63)))
  {
    return ESP_ERR_INVALID_ARG;
  }
  Credentials cred = {};
  cred.version = 1;
  memcpy(cred.ssid, ssid, ssid_len);
  memcpy(cred.password, password, pwd_len);
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

esp_err_t WifiStaClear()
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
    err = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return err;
}

WifiStatus WifiStaGetStatus()
{
  WifiStatus st = {};
  Credentials cred = {};
  if (LoadCredentials(cred) == ESP_OK)
  {
    memcpy(st.ssid, cred.ssid, sizeof(st.ssid));
  }
  memset(&cred, 0, sizeof(cred));
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

WifiLink WifiStaGetLink()
{
  if (!s_started)
  {
    return WifiLink::OFF;
  }
  return (xEventGroupGetBits(s_events) & kGotIp) != 0 ? WifiLink::UP : WifiLink::JOINING;
}

}  // namespace XrDap
