idf_component_register(SRCS "main.cpp" "wifi_sta.cpp" INCLUDE_DIRS "."
                       REQUIRES libxr esp_driver_gpio esp_driver_uart esp_wifi esp_netif esp_event nvs_flash lwip
                       LDFRAGMENTS "linker.lf")
