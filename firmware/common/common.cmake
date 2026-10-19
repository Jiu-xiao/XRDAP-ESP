# 两种芯片共用的源文件和依赖。各芯片 main/CMakeLists.txt 先 include 本文件，再把
# XRDAP_COMMON_SRCS、XRDAP_COMMON_INCLUDES、XRDAP_COMMON_REQUIRES 交给
# idf_component_register。芯片目录在包含路径里排在前面，config.hpp 用芯片自己的。
#
# Sources and dependencies shared by both chips. Each chip's main/CMakeLists.txt
# includes this file and passes XRDAP_COMMON_SRCS, XRDAP_COMMON_INCLUDES and
# XRDAP_COMMON_REQUIRES to idf_component_register. The chip directory comes first on the
# include path, so config.hpp is the chip's own.
set(XRDAP_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(XRDAP_COMMON_SRCS
    "${XRDAP_COMMON_DIR}/console/console_core.cpp"
    "${XRDAP_COMMON_DIR}/net/mdns_service.cpp"
    "${XRDAP_COMMON_DIR}/net/wifi_sta.cpp"
    "${XRDAP_COMMON_DIR}/probe/probe_info.cpp"
    "${XRDAP_COMMON_DIR}/probe/restart.cpp"
    "${XRDAP_COMMON_DIR}/vendor/dap_vendor.cpp"
    "${XRDAP_COMMON_DIR}/vendor/frame_ring.cpp"
    "${XRDAP_COMMON_DIR}/vendor/mem_ap.cpp"
    "${XRDAP_COMMON_DIR}/vendor/sampler.cpp"
    "${XRDAP_COMMON_DIR}/vendor/wifi_command.cpp")
set(XRDAP_COMMON_INCLUDES
    "${XRDAP_COMMON_DIR}/console"
    "${XRDAP_COMMON_DIR}/net"
    "${XRDAP_COMMON_DIR}/probe"
    "${XRDAP_COMMON_DIR}/serial"
    "${XRDAP_COMMON_DIR}/swd"
    "${XRDAP_COMMON_DIR}/vendor")
set(XRDAP_COMMON_REQUIRES
    libxr esp_driver_gpio esp_driver_gptimer esp_driver_uart esp_timer esp_wifi esp_netif
    esp_event nvs_flash lwip)
