cmake_minimum_required(VERSION 3.16)
# libxr 组件各芯片共用（firmware/components/libxr）/ The libxr component is shared by
# every chip (firmware/components/libxr).
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 把所有寄存器分配放在同一个区域里，SWD 热路径的代码更紧凑 / Allocate registers in one
# region, which makes the SWD hot path more compact.
idf_build_set_property(COMPILE_OPTIONS "-fira-region=one" APPEND)
project(xrdap_esp32)
