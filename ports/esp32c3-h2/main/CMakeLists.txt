# 与 S3 版共用的源文件（厂商扩展、TCP 服务、串口适配、专用 GPIO、Wi-Fi）在 firmware/main 下。
# 本目录在包含路径里排在前面，所以 config.hpp 用的是本目录的。
set(shared "${CMAKE_CURRENT_LIST_DIR}/../../../firmware/main")
set(srcs "main.cpp")
set(reqs libxr esp_driver_gpio esp_driver_gptimer esp_driver_uart esp_timer)
if(IDF_TARGET STREQUAL "esp32c3")
  list(APPEND srcs "${shared}/wifi_sta.cpp")
  list(APPEND reqs esp_wifi esp_netif esp_event nvs_flash lwip)
elseif(IDF_TARGET STREQUAL "esp32h2")
  list(APPEND srcs "esp_cdc_jtag_h2.cpp")
endif()
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "." "${shared}"
                       REQUIRES ${reqs}
                       LDFRAGMENTS "linker.lf")
