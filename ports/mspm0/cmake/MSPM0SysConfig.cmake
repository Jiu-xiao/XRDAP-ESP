# Runs TI SysConfig on the project's .syscfg at configure time, the way the MSPM0 SDK gcc
# makefiles do:
#
#   SYSCFG_FILES := $(shell $(SYSCFG_CMD_STUB) --listGeneratedFiles --listReferencedFiles ...)
#   syscfg: $(SYSCFG_CMD_STUB) --output <dir> <file>.syscfg
#
# --listGeneratedFiles and --listReferencedFiles name every file the device and the
# configuration need: ti_msp_dl_config.c/.h, device.opt, the linker script, device.lds.genlibs
# and the startup file. Nothing in the project depends on the device otherwise.
#
# Editing the .syscfg re-runs CMake on the next build, which regenerates the files.
#
# Inputs (cache or environment, the same names as the SDK imports.mak):
#   MSPM0_SDK_INSTALL_DIR  MSPM0 SDK root (has .metadata/product.json)
#   SYSCONFIG_TOOL         sysconfig_cli.sh / sysconfig_cli.bat
# Argument of mspm0_sysconfig():
#   the project's .syscfg file
# Outputs:
#   MSPM0_SYSCFG_OUTPUT_DIR   generated files (in the build tree), the include directory of
#                             ti_msp_dl_config.h
#   MSPM0_SYSCFG_SOURCES      generated C sources (ti_msp_dl_config.c)
#   MSPM0_SYSCFG_OPTIONS      device.opt (compiler options file, one @file argument)
#   MSPM0_SYSCFG_LINKER       device_linker.lds
#   MSPM0_SYSCFG_GENLIBS      device.lds.genlibs (linker script that names driverlib)
#   MSPM0_SYSCFG_LIBRARIES    the libraries named by device.lds.genlibs, as absolute paths
#   MSPM0_STARTUP_FILE        the device startup file referenced by SysConfig

foreach(_var MSPM0_SDK_INSTALL_DIR SYSCONFIG_TOOL)
  if(NOT ${_var} AND DEFINED ENV{${_var}})
    set(${_var} "$ENV{${_var}}")
  endif()
  set(${_var} "${${_var}}" CACHE FILEPATH "" FORCE)
endforeach()

if(NOT EXISTS "${MSPM0_SDK_INSTALL_DIR}/.metadata/product.json")
  message(FATAL_ERROR
    "MSPM0_SDK_INSTALL_DIR does not point to an MSPM0 SDK: '${MSPM0_SDK_INSTALL_DIR}'. "
    "Set it in the environment or with -DMSPM0_SDK_INSTALL_DIR=<sdk root>.")
endif()
if(NOT EXISTS "${SYSCONFIG_TOOL}")
  message(FATAL_ERROR
    "SYSCONFIG_TOOL does not point to sysconfig_cli: '${SYSCONFIG_TOOL}'. "
    "Set it in the environment or with -DSYSCONFIG_TOOL=<path of sysconfig_cli.sh/.bat>.")
endif()

function(mspm0_sysconfig syscfg_file)
  if(NOT EXISTS "${syscfg_file}")
    message(FATAL_ERROR "SysConfig file not found: '${syscfg_file}'")
  endif()

  set(_out "${CMAKE_BINARY_DIR}/syscfg")
  file(MAKE_DIRECTORY "${_out}")
  set(_syscfg_args
    --compiler gcc
    --product "${MSPM0_SDK_INSTALL_DIR}/.metadata/product.json"
    --output "${_out}")

  # The list of the files, without generating them.
  execute_process(
    COMMAND "${SYSCONFIG_TOOL}" ${_syscfg_args}
            --listGeneratedFiles --listReferencedFiles "${syscfg_file}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _list
    ERROR_VARIABLE _list_error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "SysConfig failed on ${syscfg_file}:\n${_list}${_list_error}")
  endif()
  string(REGEX REPLACE "[\r\n]+" ";" _list "${_list}")
  set(_files "")
  foreach(_line IN LISTS _list)
    string(STRIP "${_line}" _line)
    if(_line MATCHES "^([A-Za-z]:)?/")
      file(TO_CMAKE_PATH "${_line}" _line)
      list(APPEND _files "${_line}")
    endif()
  endforeach()

  # The generation. Warnings and errors of the configuration stop the configure.
  execute_process(
    COMMAND "${SYSCONFIG_TOOL}" ${_syscfg_args} "${syscfg_file}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "SysConfig failed on ${syscfg_file}:\n${_output}${_error}")
  endif()
  if(_output MATCHES "[1-9][0-9]* (error|warning)\\(s\\)")
    message(WARNING "SysConfig reported problems in ${syscfg_file}:\n${_output}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${syscfg_file}")

  file(TO_CMAKE_PATH "${_out}" _out)
  set(_sources "")
  set(_options "")
  set(_linker "")
  set(_genlibs "")
  set(_startup "")
  foreach(_file IN LISTS _files)
    string(FIND "${_file}" "${_out}/" _in_output)
    if(_in_output EQUAL 0)
      if(_file MATCHES "\\.c$")
        list(APPEND _sources "${_file}")
      elseif(_file MATCHES "\\.opt$")
        list(APPEND _options "${_file}")
      elseif(_file MATCHES "\\.genlibs$")
        list(APPEND _genlibs "${_file}")
      elseif(_file MATCHES "\\.lds$")
        list(APPEND _linker "${_file}")
      endif()
    elseif(_file MATCHES "\\.c$")
      list(APPEND _startup "${_file}")
    endif()
  endforeach()
  foreach(_name _sources _options _linker _genlibs _startup)
    list(LENGTH ${_name} _count)
    if(_count EQUAL 0)
      message(FATAL_ERROR "SysConfig did not list a ${_name} file for ${syscfg_file}:\n${_files}")
    endif()
  endforeach()
  foreach(_name _linker _genlibs _startup)
    list(LENGTH ${_name} _count)
    if(NOT _count EQUAL 1)
      message(FATAL_ERROR "SysConfig listed more than one ${_name} file: ${${_name}}")
    endif()
  endforeach()
  foreach(_file IN LISTS _sources _options _linker _genlibs _startup)
    if(NOT EXISTS "${_file}")
      message(FATAL_ERROR "SysConfig did not provide ${_file}")
    endif()
  endforeach()

  # device.lds.genlibs: INPUT("<archive, relative to <sdk>/source>") per library.
  file(STRINGS "${_genlibs}" _inputs REGEX "^[ \t]*INPUT\\(")
  set(_libraries "")
  foreach(_input IN LISTS _inputs)
    if(_input MATCHES "INPUT\\(\"?([^\")]+)\"?\\)")
      set(_library "${MSPM0_SDK_INSTALL_DIR}/source/${CMAKE_MATCH_1}")
      if(NOT EXISTS "${_library}")
        message(FATAL_ERROR "${_genlibs} names ${_library}, which does not exist")
      endif()
      list(APPEND _libraries "${_library}")
    endif()
  endforeach()

  set(MSPM0_SYSCFG_OUTPUT_DIR "${_out}" PARENT_SCOPE)
  set(MSPM0_SYSCFG_SOURCES "${_sources}" PARENT_SCOPE)
  set(MSPM0_SYSCFG_OPTIONS "${_options}" PARENT_SCOPE)
  set(MSPM0_SYSCFG_LINKER "${_linker}" PARENT_SCOPE)
  set(MSPM0_SYSCFG_GENLIBS "${_genlibs}" PARENT_SCOPE)
  set(MSPM0_SYSCFG_LIBRARIES "${_libraries}" PARENT_SCOPE)
  set(MSPM0_STARTUP_FILE "${_startup}" PARENT_SCOPE)
endfunction()
