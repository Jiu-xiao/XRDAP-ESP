# XR-DAP for the TI LP-MSPM0G3507 LaunchPad: CMSIS-DAP over the XDS110 backchannel UART.
#
# The build follows the MSPM0G3507_LibXR_Template BSP (xrobot-org), which does what the gcc
# makefile of the SDK examples does:
#   - SysConfig runs on xrdap_lp_mspm0g3507.syscfg at configure time and generates
#     ti_msp_dl_config.c/.h, device.opt, the linker script and device.lds.genlibs
#     (cmake/MSPM0SysConfig.cmake)
#   - the generated sources and the SDK startup file are compiled with the makefile options
#   - the link uses the generated linker script and driverlib
#
# Inputs, in the cache or in the environment (the names of the SDK imports.mak):
#   MSPM0_SDK_INSTALL_DIR  MSPM0 SDK root, e.g. C:/ti/mspm0_sdk_2_11_00_07
#   SYSCONFIG_TOOL         sysconfig_cli.bat / sysconfig_cli.sh
# Options:
#   XRDAP_SERIAL_BAUD      UART baud rate to the host (default 1000000)
cmake_minimum_required(VERSION 3.20)

project(xrdap_mspm0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type: Debug or Release" FORCE)
endif()

set(XRDAP_SERIAL_BAUD 1000000 CACHE STRING "UART baud rate to the host (xrdap-forward --baud)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ---- SysConfig ----------------------------------------------------------------------------
include(cmake/MSPM0SysConfig.cmake)
set(MSPM0_SYSCFG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/xrdap_lp_mspm0g3507.syscfg"
    CACHE FILEPATH "The SysConfig file of the board")
mspm0_sysconfig("${MSPM0_SYSCFG_FILE}")

# ---- Options of the SDK example makefile ------------------------------------------------
add_library(mspm0_sdk INTERFACE)
target_include_directories(mspm0_sdk INTERFACE "${MSPM0_SYSCFG_OUTPUT_DIR}")
# The SDK headers are C code that the C++20 warnings of -Wall flag, so they are system
# directories (-isystem).
target_include_directories(mspm0_sdk SYSTEM INTERFACE
  "${MSPM0_SDK_INSTALL_DIR}/source/third_party/CMSIS/Core/Include"
  "${MSPM0_SDK_INSTALL_DIR}/source")
target_compile_options(mspm0_sdk INTERFACE
  "@${MSPM0_SYSCFG_OPTIONS}"
  $<IF:$<CONFIG:Debug>,-Og,-O2>
  -mcpu=cortex-m0plus
  -march=armv6-m
  -mthumb
  -mfloat-abi=soft
  -ffunction-sections
  -fdata-sections
  -g
  $<$<CONFIG:Debug>:-gstrict-dwarf>
  -Wall
  $<$<COMPILE_LANGUAGE:C>:-std=c99>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)
target_link_libraries(mspm0_sdk INTERFACE ${MSPM0_SYSCFG_LIBRARIES})

add_library(mspm0_device OBJECT "${MSPM0_STARTUP_FILE}" ${MSPM0_SYSCFG_SOURCES})
target_link_libraries(mspm0_device PUBLIC mspm0_sdk)

# ---- Application --------------------------------------------------------------------------
add_executable(${PROJECT_NAME} src/main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_compile_definitions(${PROJECT_NAME} PRIVATE XRDAP_SERIAL_BAUD=${XRDAP_SERIAL_BAUD})
target_link_libraries(${PROJECT_NAME} PRIVATE mspm0_device)
target_compile_options(${PROJECT_NAME} PRIVATE -Werror)

# The SysConfig linker script puts heap and stack in the RAM left after .bss (heap upward
# from the end of .bss, stack downward from the top of SRAM) and checks neither.
# ram_check.ld fails the link when less than XRDAP_MIN_FREE_RAM bytes are left for them.
set(XRDAP_MIN_FREE_RAM 8192 CACHE STRING "Bytes of SRAM that must stay free for heap + stack")
target_link_options(${PROJECT_NAME} PRIVATE
  -mcpu=cortex-m0plus
  -march=armv6-m
  -mthumb
  -mfloat-abi=soft
  -nostartfiles
  "-T${MSPM0_SYSCFG_GENLIBS}"
  "-Wl,-T,${MSPM0_SYSCFG_LINKER}"
  "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/cmake/ram_check.ld"
  "-Wl,--defsym,XRDAP_MIN_FREE_RAM=${XRDAP_MIN_FREE_RAM}"
  "-Wl,-Map,${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map"
  "-L${MSPM0_SDK_INSTALL_DIR}/source"
  -static
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segment
  -Wl,--print-memory-usage
  --specs=nano.specs
  --specs=nosys.specs)
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
  "${MSPM0_SYSCFG_LINKER}" "${MSPM0_SYSCFG_GENLIBS}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ram_check.ld")

# ---- LibXR --------------------------------------------------------------------------------
# Platform None (bare metal, SysTick timebase) with the MSPM0 drivers.
set(LIBXR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libxr" CACHE PATH
    "Path to the LibXR source tree")
if(NOT EXISTS "${LIBXR_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "LibXR not found at ${LIBXR_DIR}; run git submodule update --init")
endif()
set(LIBXR_SYSTEM None)
set(LIBXR_DRIVER mspm0)
set(LIBXR_NO_EIGEN True)
add_subdirectory("${LIBXR_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/libxr")
target_link_libraries(xr PUBLIC mspm0_sdk)
target_link_libraries(${PROJECT_NAME} PRIVATE xr)

# ---- Firmware images ----------------------------------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  WORKING_DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>
  COMMAND ${CMAKE_OBJCOPY} --output-target ihex
    $<TARGET_FILE_NAME:${PROJECT_NAME}> ${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} --output-target binary
    $<TARGET_FILE_NAME:${PROJECT_NAME}> ${PROJECT_NAME}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE_NAME:${PROJECT_NAME}>
  VERBATIM)
