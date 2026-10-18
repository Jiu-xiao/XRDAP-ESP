cmake_minimum_required(VERSION 3.16)
# ESP32-C3 和 ESP32-H2 共用这个工程，芯片在编译时用 -DIDF_TARGET 选，
# 各芯片自己的配置在 sdkconfig.defaults.esp32c3 / sdkconfig.defaults.esp32h2 里。
# libxr 组件与 S3 版共用（firmware/components/libxr）。
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../firmware/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 把所有寄存器分配放在同一个区域里，SWD 热路径的代码更紧凑（与 S3 版相同）
idf_build_set_property(COMPILE_OPTIONS "-fira-region=one" APPEND)
project(xrdap_${IDF_TARGET})
