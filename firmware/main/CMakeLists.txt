idf_component_register(SRCS "main.cpp" "wifi_sta.cpp" INCLUDE_DIRS "."
                       REQUIRES libxr esp_driver_gpio esp_wifi esp_netif esp_event nvs_flash lwip
                       LDFRAGMENTS "linker.lf")
# -O3 for the hot path (SWD engine, DAP command processing and TCP framing are instantiated in main.cpp)
if(DEFINED ENV{XRDAP_OPT3})
  target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
endif()
