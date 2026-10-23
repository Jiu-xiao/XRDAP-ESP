cmake_minimum_required(VERSION 3.16)
# libxr 组件各芯片共用（firmware/components/libxr）。
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# S2、S3 共用本工程，组件依赖按目标各锁一份，换目标编译不会改动另一份 / S2 and S3 share
# this project, so each target keeps its own component lock file and building one does
# not rewrite the other's.
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET}")
# 把所有寄存器分配放在同一个区域里，SWD 热路径的代码更紧凑
idf_build_set_property(COMPILE_OPTIONS "-fira-region=one" APPEND)
project(xrdap_esp32sx)
