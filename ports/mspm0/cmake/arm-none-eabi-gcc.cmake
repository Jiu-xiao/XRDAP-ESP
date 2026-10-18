# Toolchain file for the GNU Arm Embedded toolchain (arm-none-eabi-gcc).
# The CPU options (Cortex-M0+) and the build options are set by CMakeLists.txt.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# arm-none-eabi- must be part of PATH, or TOOLCHAIN_PREFIX is a full path prefix such as
# /opt/arm-gnu-toolchain/bin/arm-none-eabi-
set(TOOLCHAIN_PREFIX "arm-none-eabi-" CACHE STRING "Cross-compiler prefix")

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

# The compiler checks link a static library, since no linker script is available yet.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# The TI SDK gcc makefile sets the optimization and debug options itself, the same for every
# build, and CMakeLists.txt does the same. Start the per-configuration flags empty, so that
# CMake's defaults (-O3 -DNDEBUG and so on) do not add anything to them. A -D on the command
# line still replaces them.
foreach(_lang C CXX ASM)
  foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_${_lang}_FLAGS_${_config} "" CACHE STRING "Flags for the ${_config} configuration")
  endforeach()
endforeach()
