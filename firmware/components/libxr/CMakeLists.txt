idf_component_register(
  SRCS "libxr_stub.c"
  INCLUDE_DIRS "."
  REQUIRES freertos driver esp_driver_gpio hal soc usb esp_hw_support esp_timer esp_event
           esp_netif esp_wifi esp_adc nvs_flash esp_rom esp_system esp_common)
# libxr-c checkout: -DLIBXR_ROOT=<path> or the environment variable XRDAP_LIBXR_ROOT
# (tested with branch feat/usb-slot-model-dap, commit 22ba8642 or later)
if(NOT DEFINED LIBXR_ROOT AND DEFINED ENV{XRDAP_LIBXR_ROOT})
  set(LIBXR_ROOT "$ENV{XRDAP_LIBXR_ROOT}")
endif()
if(NOT DEFINED LIBXR_ROOT OR NOT EXISTS "${LIBXR_ROOT}/cmake/esp32.cmake")
  message(FATAL_ERROR "Set XRDAP_LIBXR_ROOT (or -DLIBXR_ROOT) to a libxr-c checkout")
endif()
include(${LIBXR_ROOT}/cmake/esp32.cmake)
