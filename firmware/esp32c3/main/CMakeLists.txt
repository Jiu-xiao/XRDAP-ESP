# 与 S3 版共用的源文件（厂商扩展、TCP 服务、串口适配、串口分流、专用 GPIO、Wi-Fi）在 firmware/common 下。
# 本目录在包含路径里排在前面，所以 config.hpp 用的是本目录的。
set(common "${CMAKE_CURRENT_LIST_DIR}/../../common")
idf_component_register(SRCS "main.cpp" "${common}/wifi_sta.cpp"
                       INCLUDE_DIRS "." "${common}"
                       REQUIRES libxr esp_driver_gpio esp_driver_gptimer esp_driver_uart esp_timer
                                esp_wifi esp_netif esp_event nvs_flash lwip
                       LDFRAGMENTS "linker.lf")
