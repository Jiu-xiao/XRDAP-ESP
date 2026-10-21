# 电脑上运行的单元测试：只编译不依赖 ESP-IDF 的纯逻辑（命令行、0x86、TCP 帧、USB/IP、状态灯输入）。
#   cmake -S firmware/tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
#
# Unit tests that run on a PC: only the pure logic without ESP-IDF is compiled (console,
# 0x86, TCP framing, USB/IP, status light inputs).
cmake_minimum_required(VERSION 3.16)
project(xrdap_esp_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../common)
set(CORE ${CMAKE_CURRENT_LIST_DIR}/../../third_party/xrdap-core)

add_executable(xrdap_esp_tests
    main.cpp
    test_console.cpp
    test_wifi_command.cpp
    test_dap_tcp.cpp
    test_usbip.cpp
    test_link_state.cpp
    ${COMMON}/console/console_core.cpp
    ${COMMON}/net/wifi_command.cpp)
target_include_directories(xrdap_esp_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${COMMON}/console
    ${COMMON}/net
    ${COMMON}/probe
    ${CORE}/light
    ${CORE}/vendor
    ${CORE}/probe)
if(MSVC)
  target_compile_options(xrdap_esp_tests PRIVATE /utf-8 /W4 /WX)
else()
  target_compile_options(xrdap_esp_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME xrdap_esp_tests COMMAND xrdap_esp_tests)
