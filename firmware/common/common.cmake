# 各芯片共用的源文件和依赖。各芯片 main/CMakeLists.txt 先 include 本文件，再把
# XRDAP_COMMON_SRCS、XRDAP_COMMON_INCLUDES、XRDAP_COMMON_REQUIRES 交给
# idf_component_register，把 XRDAP_COMMON_DEFINES 加到组件上。芯片目录在包含路径里排在
# 前面，config.hpp 用芯片自己的。
#
# Sources and dependencies shared by every chip. Each chip's main/CMakeLists.txt
# includes this file, passes XRDAP_COMMON_SRCS, XRDAP_COMMON_INCLUDES and
# XRDAP_COMMON_REQUIRES to idf_component_register and adds XRDAP_COMMON_DEFINES to the
# component. The chip directory comes first on the include path, so config.hpp is the
# chip's own.
set(XRDAP_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}")
# 厂商扩展、状态灯规则和探针身份来自 XRDAP-Core（third_party/xrdap-core）。
# The vendor extension, the status light rule and the probe identity come from
# XRDAP-Core (third_party/xrdap-core).
include("${XRDAP_COMMON_DIR}/../../third_party/xrdap-core/xrdap_core.cmake")
set(XRDAP_COMMON_SRCS
    "${XRDAP_COMMON_DIR}/console/console_core.cpp"
    "${XRDAP_COMMON_DIR}/net/mdns_service.cpp"
    "${XRDAP_COMMON_DIR}/net/wifi_command.cpp"
    "${XRDAP_COMMON_DIR}/net/wifi_sta.cpp"
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
# ESP 的 RAM 够大，采样环形缓冲用 16 KB（Core 默认 4 KB）。
# ESP has enough RAM for a 16 KB sampling ring (Core default 4 KB).
set(XRDAP_COMMON_DEFINES XRDAP_RING_BYTES=16384)
set(XRDAP_COMMON_REQUIRES
    libxr esp_driver_gpio esp_driver_gptimer esp_driver_uart esp_timer esp_wifi esp_netif
    esp_event nvs_flash lwip)
