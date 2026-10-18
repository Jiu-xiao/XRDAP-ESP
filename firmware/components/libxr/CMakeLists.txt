idf_component_register(
  SRCS "libxr_stub.c"
  INCLUDE_DIRS "."
  REQUIRES freertos driver esp_driver_gpio hal soc usb esp_hw_support esp_timer esp_event
           esp_netif esp_wifi esp_adc nvs_flash esp_rom esp_system esp_common)
# libxr 以子模块形式放在 third_party/libxr
set(LIBXR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/libxr")
if(NOT EXISTS "${LIBXR_ROOT}/cmake/esp32.cmake")
  message(FATAL_ERROR "没有找到 libxr 子模块，先执行 git submodule update --init")
endif()
include(${LIBXR_ROOT}/cmake/esp32.cmake)
# 没有 Wi-Fi 的芯片（ESP32-H2）上 libxr 的 Wi-Fi 驱动编译不过（esp_wifi.h 要的 CONFIG_ESP_WIFI_* 不存在），
# 从 xr 库里去掉。S3、C3 不受影响。
if(NOT CONFIG_SOC_WIFI_SUPPORTED)
  get_target_property(_xr_sources xr SOURCES)
  list(FILTER _xr_sources EXCLUDE REGEX "/driver/esp/esp_wifi_client\\.cpp$")
  set_property(TARGET xr PROPERTY SOURCES ${_xr_sources})
endif()
