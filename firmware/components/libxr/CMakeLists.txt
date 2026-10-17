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
