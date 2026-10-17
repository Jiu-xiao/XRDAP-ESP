#include "wifi_sta.hpp"

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
EventGroupHandle_t g_events;

struct Credentials
{
  uint8_t version;
  char ssid[33];
  char password[64];
};

void OnEvent(void*, esp_event_base_t base, int32_t id, void* data)
{
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
  {
    xEventGroupClearBits(g_events, kGotIp);
    esp_wifi_connect();
  }
  else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
  {
    auto* ev = static_cast<ip_event_got_ip_t*>(data);
    ESP_LOGI(TAG, "已获得 IP " IPSTR, IP2STR(&ev->ip_info.ip));
    xEventGroupSetBits(g_events, kGotIp);
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
  nvs_handle_t nvs;
  err = nvs_open("rmlink", NVS_READONLY, &nvs);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "没有保存的热点参数（nvs_open: %s）", esp_err_to_name(err));
    return err;
  }
  size_t size = sizeof(cred);
  err = nvs_get_blob(nvs, "wifi", &cred, &size);
  nvs_close(nvs);
  if (err != ESP_OK || size != sizeof(cred) || cred.version != 1 || cred.ssid[0] == 0)
  {
    ESP_LOGE(TAG, "保存的热点参数缺失或无效");
    return ESP_ERR_NOT_FOUND;
  }
  cred.ssid[sizeof(cred.ssid) - 1] = 0;
  cred.password[sizeof(cred.password) - 1] = 0;
  ESP_LOGI(TAG, "使用已保存的热点，SSID 长度 %u", (unsigned)strlen(cred.ssid));

  g_events = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, OnEvent, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, OnEvent, nullptr));
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

  const EventBits_t bits =
      xEventGroupWaitBits(g_events, kGotIp, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
  return (bits & kGotIp) ? ESP_OK : ESP_ERR_TIMEOUT;
}
}  // namespace XrDap
