cmake_minimum_required(VERSION 3.16)
# libxr 组件与 C3 版共用（firmware/components/libxr）。
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 把所有寄存器分配放在同一个区域里，SWD 热路径的代码更紧凑
idf_build_set_property(COMPILE_OPTIONS "-fira-region=one" APPEND)
project(xrdap_esp32s3)
