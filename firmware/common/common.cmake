# 各芯片共用的源文件和依赖。各芯片 main/CMakeLists.txt 先 include 本文件，再把
# XRDAP_COMMON_SRCS、XRDAP_COMMON_INCLUDES、XRDAP_COMMON_REQUIRES 交给
# idf_component_register。芯片目录在包含路径里排在前面，config.hpp 用芯片自己的。
#
# Sources and dependencies shared by every chip. Each chip's main/CMakeLists.txt
# includes this file, passes XRDAP_COMMON_SRCS, XRDAP_COMMON_INCLUDES and
# XRDAP_COMMON_REQUIRES to idf_component_register. The chip directory comes first on the
# include path, so config.hpp is the chip's own.
set(XRDAP_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}")
# 会话主人、命令执行器、串口链路、厂商扩展、状态灯规则和探针身份来自 XRDAP-Core
# （third_party/xrdap-core）。
# The session owner, the command executor, the serial link, the vendor extension, the
# status light rule and the probe identity come from XRDAP-Core
# (third_party/xrdap-core).
include("${XRDAP_COMMON_DIR}/../../third_party/xrdap-core/xrdap_core.cmake")
set(XRDAP_COMMON_SRCS
    "${XRDAP_COMMON_DIR}/console/console_core.cpp"
    "${XRDAP_COMMON_DIR}/net/mdns_service.cpp"
    "${XRDAP_COMMON_DIR}/net/wifi_command.cpp"
    "${XRDAP_COMMON_DIR}/net/wifi_sta.cpp"
    "${XRDAP_COMMON_DIR}/probe/heap_report.cpp"
    "${XRDAP_COMMON_DIR}/probe/probe_info.cpp"
    "${XRDAP_COMMON_DIR}/probe/restart.cpp"
    "${XRDAP_COMMON_DIR}/probe/sampler_task.cpp"
    ${XRDAP_CORE_SOURCES})
set(XRDAP_COMMON_INCLUDES
    "${XRDAP_COMMON_DIR}/console"
    "${XRDAP_COMMON_DIR}/net"
    "${XRDAP_COMMON_DIR}/probe"
    "${XRDAP_COMMON_DIR}/serial"
    "${XRDAP_COMMON_DIR}/swd"
    ${XRDAP_CORE_INCLUDE_DIRS})
set(XRDAP_COMMON_REQUIRES
    libxr esp_driver_gpio esp_driver_gptimer esp_driver_uart esp_timer esp_wifi esp_netif
    esp_event nvs_flash lwip)
