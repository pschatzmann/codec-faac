cmake_minimum_required(VERSION 3.16)

# Sources in src/ are generated from original/faac by create_arduino_lib.py
file(GLOB FAAC_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/libfaac/*.c)

# ESP-IDF component
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${FAAC_SOURCES}
        INCLUDE_DIRS src
        PRIV_INCLUDE_DIRS src/libfaac
    )
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -Wno-unused-function)
    return()
endif()

project(codec-faac VERSION 2.2.0 LANGUAGES C)

set(FAAC_MAX_CHANNELS "8" CACHE STRING "Maximum number of channels (1-8)")
set(FAAC_SBR_DECIMATION "1" CACHE STRING "SBR analysis density: analyse every Nth slot (1-8)")
option(FAAC_STATS "Print end-of-encode statistics to stderr" OFF)
option(FAAC_SSE2 "Use the SSE2 quantizer on x86 hosts" ON)
option(FAAC_BUILD_EXAMPLES "Build the adts_encode example" ${PROJECT_IS_TOP_LEVEL})

add_library(faac ${FAAC_SOURCES})
add_library(faac::faac ALIAS faac)

target_include_directories(faac
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/libfaac
)
target_compile_features(faac PRIVATE c_std_11)
target_compile_definitions(faac PRIVATE
    MAX_CHANNELS=${FAAC_MAX_CHANNELS}
    FAAC_SBR_DECIMATION=${FAAC_SBR_DECIMATION}
    $<$<BOOL:${FAAC_STATS}>:FAAC_STATS>
    $<$<NOT:$<BOOL:${FAAC_SSE2}>>:FAAC_NO_SSE2>
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)
set_target_properties(faac PROPERTIES C_VISIBILITY_PRESET hidden)

find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(faac PUBLIC ${MATH_LIBRARY})
endif()

if(FAAC_BUILD_EXAMPLES)
    add_executable(adts_encode examples/cmake/adts_encode.c)
    target_link_libraries(adts_encode PRIVATE faac)
endif()

include(GNUInstallDirs)
install(TARGETS faac
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/faac.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
