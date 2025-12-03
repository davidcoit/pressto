cmake_minimum_required(VERSION 3.22)

project(PresstoCompressor VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PRESSTO_COPY_PLUGIN_AFTER_BUILD "Copy plugin to system VST folders after build" OFF)
option(PRESSTO_FETCH_VST3_SDK "Automatically fetch the VST3 SDK when not provided" ON)

include(FetchContent)

#
# Locate the VST3 SDK in a platform-agnostic way:
#   - Respect VST3_SDK_DIR / VST3_SDK_PATH env vars or cache hints.
#   - Fall back to common local folders.
#   - Optionally fetch the SDK if nothing is found (handy in CI or fresh checkouts).
#
set(VST3_SDK_DIR "$ENV{VST3_SDK_DIR}" CACHE PATH "Path to the VST3 SDK (set to disable auto-fetch)")
if (NOT VST3_SDK_DIR AND DEFINED ENV{VST3_SDK_PATH})
    set(VST3_SDK_DIR "$ENV{VST3_SDK_PATH}" CACHE PATH "Path to the VST3 SDK (set to disable auto-fetch)" FORCE)
endif()

if (NOT VST3_SDK_DIR)
    set(_vst3_hints
        "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk"
        "${CMAKE_CURRENT_SOURCE_DIR}/../vst3sdk"
    )

    foreach(_hint IN LISTS _vst3_hints)
        if (EXISTS "${_hint}/CMakeLists.txt")
            set(VST3_SDK_DIR "${_hint}" CACHE PATH "Path to the VST3 SDK (set to disable auto-fetch)" FORCE)
            break()
        endif()
    endforeach()
endif()

if (NOT VST3_SDK_DIR AND PRESSTO_FETCH_VST3_SDK)
    message(STATUS "Fetching VST3 SDK (override with VST3_SDK_DIR or set PRESSTO_FETCH_VST3_SDK=OFF)")
    FetchContent_Declare(
        VST3SDK
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.7.14_build_55
    )
    FetchContent_MakeAvailable(VST3SDK)
    set(VST3_SDK_DIR "${vst3sdk_SOURCE_DIR}" CACHE PATH "Path to the VST3 SDK (set to disable auto-fetch)" FORCE)
endif()

if (NOT VST3_SDK_DIR OR NOT EXISTS "${VST3_SDK_DIR}")
    message(FATAL_ERROR "VST3 SDK not found. Set VST3_SDK_DIR or enable PRESSTO_FETCH_VST3_SDK.")
endif()

set(JUCE_VST3_SDK_LOCATION "${VST3_SDK_DIR}")

if (NOT DEFINED JUCE_SOURCE_DIR)
    message(STATUS "Fetching JUCE 7.0.9")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.9
    )
    FetchContent_MakeAvailable(JUCE)
else()
    message(STATUS "Using JUCE from ${JUCE_SOURCE_DIR}")
    add_subdirectory(${JUCE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/juce)
endif()

juce_add_plugin(PresstoCompressor
    PLUGIN_MANUFACTURER_CODE PreS
    PLUGIN_CODE PrsC
    COMPANY_NAME "CodexAudio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD ${PRESSTO_COPY_PLUGIN_AFTER_BUILD}
    FORMATS VST3
    PRODUCT_NAME "Pressto Compressor"
)

target_sources(PresstoCompressor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/MeterComponent.cpp
    Source/MeterComponent.h
)

target_compile_definitions(PresstoCompressor
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(PresstoCompressor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
