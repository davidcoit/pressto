cmake_minimum_required(VERSION 3.22)

project(PresstoCompressor VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PRESSTO_COPY_PLUGIN_AFTER_BUILD "Copy plugins to the user's plugin folders after building" OFF)
option(PRESSTO_BUILD_STANDALONE "Also build a standalone app (handy for quick testing)" ON)
option(PRESSTO_BUILD_TESTS "Build the offline DSP tests" ON)
set(PRESSTO_JUCE_TAG "8.0.15" CACHE STRING "JUCE git tag to fetch when JUCE_SOURCE_DIR is not set")

#
# JUCE: use a local checkout if JUCE_SOURCE_DIR is given, otherwise fetch a
# pinned release. JUCE ships the VST3 SDK, so nothing else needs downloading.
#
if (DEFINED JUCE_SOURCE_DIR)
    message(STATUS "Using JUCE from ${JUCE_SOURCE_DIR}")
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce")
else()
    include(FetchContent)
    message(STATUS "Fetching JUCE ${PRESSTO_JUCE_TAG}")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${PRESSTO_JUCE_TAG}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Optional: build against an external VST3 SDK instead of the one bundled with JUCE.
if (VST3_SDK_DIR)
    juce_set_vst3_sdk_path("${VST3_SDK_DIR}")
endif()

set(PRESSTO_FORMATS VST3)
if (APPLE)
    list(APPEND PRESSTO_FORMATS AU)
endif()
if (PRESSTO_BUILD_STANDALONE)
    list(APPEND PRESSTO_FORMATS Standalone)
endif()

juce_add_plugin(PresstoCompressor
    PRODUCT_NAME "Pressto Compressor"
    COMPANY_NAME "CodexAudio"
    BUNDLE_ID "com.codexaudio.pressto"
    DESCRIPTION "Program-dependent compressor"
    PLUGIN_MANUFACTURER_CODE PreS
    PLUGIN_CODE PrsC
    FORMATS ${PRESSTO_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${PRESSTO_COPY_PLUGIN_AFTER_BUILD})

target_sources(PresstoCompressor PRIVATE
    Source/CompressorEngine.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PresstoLookAndFeel.cpp
    Source/PresstoLookAndFeel.h
    Source/UIComponents.cpp
    Source/UIComponents.h)

target_compile_definitions(PresstoCompressor
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PresstoCompressor
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (PRESSTO_BUILD_TESTS)
    enable_testing()
    add_executable(PresstoEngineTests tests/EngineTests.cpp)
    target_link_libraries(PresstoEngineTests PRIVATE juce::juce_recommended_warning_flags)
    add_test(NAME PresstoEngineTests COMMAND PresstoEngineTests)

    # Builds the real processor + editor into a host-less test app that checks
    # control wiring, audio behaviour and state. Needs a display on Linux
    # (use xvfb-run in CI).
    juce_add_console_app(PresstoPluginTests PRODUCT_NAME "PresstoPluginTests")
    target_sources(PresstoPluginTests PRIVATE
        tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PresstoLookAndFeel.cpp
        Source/UIComponents.cpp)
    target_compile_definitions(PresstoPluginTests PRIVATE
        "JucePlugin_Name=\"Pressto Compressor\""
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(PresstoPluginTests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME PresstoPluginTests COMMAND PresstoPluginTests)
endif()
