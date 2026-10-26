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
    test_timer_deadline.cpp
    ${COMMON}/console/console_core.cpp
    ${COMMON}/net/wifi_command.cpp)
target_include_directories(xrdap_esp_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${COMMON}/console
    ${COMMON}/net
    ${COMMON}/probe
    ${CORE})
# USB/IP 描述符对照 XRDAP-Core 协议文档第 6 节 / USB/IP descriptors are checked against
# section 6 of the XRDAP-Core protocol document.
target_compile_definitions(xrdap_esp_tests PRIVATE
    XRDAP_PROTOCOL_DOC="${CORE}/docs/protocol.en.md")
if(MSVC)
  target_compile_options(xrdap_esp_tests PRIVATE /utf-8 /W4 /WX)
else()
  target_compile_options(xrdap_esp_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME xrdap_esp_tests COMMAND xrdap_esp_tests)

if(UNIX)
  add_executable(xrdap_socket_tests test_socket_io.cpp)
  target_include_directories(xrdap_socket_tests PRIVATE
      "${CMAKE_CURRENT_LIST_DIR}/posix" "${COMMON}/net")
  target_compile_options(xrdap_socket_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME xrdap_socket_tests COMMAND xrdap_socket_tests)
  set_tests_properties(xrdap_socket_tests PROPERTIES TIMEOUT 15)
endif()
