include("${CMAKE_CURRENT_LIST_DIR}/../../common/common.cmake")
idf_component_register(SRCS "main.cpp" ${XRDAP_COMMON_SRCS}
                       INCLUDE_DIRS "." ${XRDAP_COMMON_INCLUDES}
                       REQUIRES ${XRDAP_COMMON_REQUIRES}
                       LDFRAGMENTS "../../common/linker.lf")
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${XRDAP_COMMON_DEFINES})
