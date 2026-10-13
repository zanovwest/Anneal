cmake_minimum_required(VERSION 3.22)
project(Anneal VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8.0.4 (same pin as the author's other plugins). Point ANNEAL_JUCE_DIR at a clean
# checkout of that tag to build offline (build.ps1 finds and verifies one automatically);
# otherwise the same tag is fetched.
set(ANNEAL_JUCE_DIR "" CACHE PATH "Local JUCE 8.0.4 source tree (optional)")
if(ANNEAL_JUCE_DIR AND EXISTS "${ANNEAL_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Anneal: using local JUCE at ${ANNEAL_JUCE_DIR}")
    add_subdirectory("${ANNEAL_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Shared sources: DSP, processor, editor. Compiled into the plugin and the tools.
add_library(AnnealCore INTERFACE)
target_sources(AnnealCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/PerceptualModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/HarmonicSynth.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/SpectralEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/LoudnessMatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/AnnealLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Knob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/PerceptionScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/OutputMeter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/PresetBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Fader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/HarmonicGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/LatencyMenu.cpp)
target_include_directories(AnnealCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(AnnealCore INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(AnnealCore INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp)

# ---------------------------------------------------------------------------
# The plugin. Not copied into any system/user plugin folder by default.
option(ANNEAL_COPY_AFTER_BUILD "Copy the VST3 into ANNEAL_VST3_COPY_DIR after building" OFF)
set(ANNEAL_VST3_COPY_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3" CACHE PATH "Where to copy the VST3 when ANNEAL_COPY_AFTER_BUILD is ON")

juce_add_plugin(Anneal
    COMPANY_NAME "Qulic"
    PLUGIN_MANUFACTURER_CODE Qulc
    PLUGIN_CODE Anl1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Anneal"
    BUNDLE_ID "com.qulic.anneal"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx EQ Distortion
    COPY_PLUGIN_AFTER_BUILD ${ANNEAL_COPY_AFTER_BUILD}
    VST3_COPY_DIR "${ANNEAL_VST3_COPY_DIR}")

target_link_libraries(Anneal
    PRIVATE
        AnnealCore
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless DSP/processor acceptance tests + benchmark.
juce_add_console_app(AnnealTests PRODUCT_NAME "AnnealTests")
target_sources(AnnealTests PRIVATE tests/AnnealTests.cpp)
target_link_libraries(AnnealTests
    PRIVATE
        AnnealCore
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Renders the real editor (fed with real processed audio) to PNG files.
juce_add_console_app(AnnealSnapshot PRODUCT_NAME "AnnealSnapshot")
target_sources(AnnealSnapshot PRIVATE tools/Snapshot.cpp tools/TestSignals.h)
target_link_libraries(AnnealSnapshot
    PRIVATE
        AnnealCore
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
target_include_directories(AnnealTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# Loads the *built* Anneal.vst3 through JUCE's VST3 host (scan, process, state, editor, instances).
# Deliberately does not link AnnealCore: it tests the shipped binary.
juce_add_console_app(AnnealHostCheck PRODUCT_NAME "AnnealHostCheck")
target_sources(AnnealHostCheck PRIVATE tools/HostCheck.cpp)
target_compile_definitions(AnnealHostCheck PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(AnnealHostCheck
    PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
