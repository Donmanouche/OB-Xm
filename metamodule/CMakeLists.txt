cmake_minimum_required(VERSION 3.22)

# MetaModule plugin SDK (https://github.com/4ms/metamodule-plugin-sdk): set
# METAMODULE_SDK_DIR (CMake or environment variable), or clone it next to this
# repository as ../metamodule-plugin-sdk.
if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
    message("METAMODULE_SDK_DIR set by CMake variable ${METAMODULE_SDK_DIR}")
elseif(DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
    message("METAMODULE_SDK_DIR set by environment variable ${METAMODULE_SDK_DIR}")
else()
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../metamodule-plugin-sdk")
    message("METAMODULE_SDK_DIR set to default: ${METAMODULE_SDK_DIR}")
endif()

include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(ObXmMetaModulePlugin
    DESCRIPTION "OB-Xm (OBXm Oscillator, Filter, Amplifier, Voice Variation) for MetaModule"
    LANGUAGES   C CXX ASM
)

add_library(ObXm STATIC)

set(PLUGIN_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# The VCV Rack sources are compiled unchanged through the SDK's Rack adaptor;
# MetaModule differences are under #ifdef METAMODULE in the sources.
target_sources(ObXm
    PRIVATE
    ${PLUGIN_ROOT}/src/plugin.cpp
    ${PLUGIN_ROOT}/src/ObxfOscillator.cpp
    ${PLUGIN_ROOT}/src/ObxfFilter.cpp
    ${PLUGIN_ROOT}/src/ObxfVoices.cpp
    ${PLUGIN_ROOT}/src/ObxfAmp.cpp
)

target_include_directories(ObXm PRIVATE
    ${PLUGIN_ROOT}/src
    ${PLUGIN_ROOT}/thirdparty/obxf/engine
    ${PLUGIN_ROOT}/thirdparty/obxf/shim
)

target_compile_options(ObXm PRIVATE -fno-math-errno)

# MetaModule simulator only (it builds this directory with its own fake SDK and
# prelinks the plugin with `ld -r` + `objcopy` to make its symbols private). Inline
# functions shared with the host (std::to_string, ...) live in COMDAT groups the
# final link then discards while our localized code still references them; with
# -fno-weak they are emitted as plain definitions owned by the plugin instead.
if(EXISTS "${METAMODULE_SDK_DIR}/localize-ext-plugin.cmake")
    target_compile_options(ObXm PRIVATE -fno-weak)
endif()

# OB-Xf's Noise.h uses std::countr_zero
set_property(TARGET ObXm PROPERTY CXX_STANDARD 20)

if("${INSTALL_DIR}" STREQUAL "")
    set(INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins)
endif()

create_plugin(
    SOURCE_LIB      ObXm
    PLUGIN_NAME     OB-Xm
    PLUGIN_JSON     ${PLUGIN_ROOT}/plugin.json
    SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION     ${INSTALL_DIR}
)
