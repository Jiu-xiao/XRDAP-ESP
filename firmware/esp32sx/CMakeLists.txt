cmake_minimum_required(VERSION 3.16)
# libxr 自带的 Eigen 默认会连自己的 BLAS/LAPACK/测试一起编（本工程用不到），而较新的
# xtensa GCC（14.2.0_20251107）对这批 f2c 代码报 maybe-uninitialized，撞上 IDF 默认的
# -Werror 会直接编译失败，所以显式关掉。 / libxr's bundled Eigen builds its own
# BLAS/LAPACK/tests by default, which this project does not use, and the newer xtensa GCC
# (14.2.0_20251107) reports maybe-uninitialized in that f2c code, which fails the build
# under IDF's default -Werror, so turn those parts off explicitly.
set(EIGEN_BUILD_BLAS OFF)
set(EIGEN_BUILD_LAPACK OFF)
set(EIGEN_BUILD_TESTING OFF)
set(EIGEN_BUILD_DEMOS OFF)
set(EIGEN_BUILD_PKGCONFIG OFF)
set(EIGEN_BUILD_CMAKE_PACKAGE OFF)
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
