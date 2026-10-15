cmake_minimum_required(VERSION 3.22)

# macOS: 10.15 or later. Pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build
# (Apple Silicon + Intel), as .github/workflows/platforms.yml does.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(Anneal VERSION 4.0.0 LANGUAGES C CXX)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/Reference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/ReferenceAnalyser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/HarmonicSynth.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/SpectralEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/LoudnessMatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/TruePeakLimiter.cpp
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
    JUCE_VST3_CAN_REPLACE_VST2=0
    # 4.0 reference files: JUCE's own MP3 decoder (portable; no OS codecs are used).
    JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(AnnealCore INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp)

# ---------------------------------------------------------------------------
# JUCE writes the Windows version resource (the DLL's File version in Explorer) only when the
# icon changes, so after a version bump it kept the old number: 3.0.0 shipped saying 0.2.1.
# A stale copy is removed here, at configure time (a version bump edits this file, so CMake
# re-configures), and the build then writes it again from the current version.
# build.ps1 checks the built DLL's file version.
set(anneal_rc "${CMAKE_CURRENT_BINARY_DIR}/Anneal_artefacts/JuceLibraryCode/Anneal_resources.rc")
if(EXISTS "${anneal_rc}")
    file(READ "${anneal_rc}" anneal_rc_text)
    string(REPLACE "." "," anneal_rc_version "${PROJECT_VERSION}")
    if(NOT anneal_rc_text MATCHES "FILEVERSION +${anneal_rc_version},0")
        message(STATUS "Anneal: version resource is stale, regenerating it for ${PROJECT_VERSION}")
        file(REMOVE "${anneal_rc}")
    endif()
endif()

# ---------------------------------------------------------------------------
# The plugin. Not copied into any system/user plugin folder by default.
option(ANNEAL_COPY_AFTER_BUILD "Copy the VST3 into ANNEAL_VST3_COPY_DIR after building" OFF)
set(ANNEAL_VST3_COPY_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3" CACHE PATH "Where to copy the VST3 when ANNEAL_COPY_AFTER_BUILD is ON")

# Formats per platform. Windows: VST3 + Standalone (the setup). macOS: also AU, for Logic Pro,
# GarageBand and MainStage. Linux: also LV2, for Ardour and other LV2 hosts. CLAP is optional on
# every platform (ANNEAL_CLAP, below). AAX (Pro Tools) needs Avid's SDK and PACE signing, so it
# isn't built here.
if(APPLE)
    set(anneal_formats VST3 AU Standalone)
elseif(UNIX)
    set(anneal_formats VST3 LV2 Standalone)
else()
    set(anneal_formats VST3 Standalone)
endif()

juce_add_plugin(Anneal
    COMPANY_NAME "Qulic"
    PLUGIN_MANUFACTURER_CODE Qulc
    PLUGIN_CODE Anl1
    FORMATS ${anneal_formats}
    PRODUCT_NAME "Anneal"
    LV2URI "urn:qulic:anneal"
    BUNDLE_ID "com.qulic.anneal"
    # App icon (standalone .exe) and the VST3 bundle's folder icon (Plugin.ico + desktop.ini).
    # Rendered from Source/ui/Icon.h: AnnealSnapshot --icons resources/icon
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon_512.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon_32.png"
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

# CLAP (Bitwig, Reaper, FL Studio 2024+, ...), through clap-juce-extensions, pinned to a commit.
# Off by default because it downloads that library; the platforms workflow turns it on.
option(ANNEAL_CLAP "Also build Anneal.clap (fetches clap-juce-extensions)" OFF)
if(ANNEAL_CLAP)
    if(CMAKE_VERSION VERSION_LESS 3.28)
        message(FATAL_ERROR "ANNEAL_CLAP needs CMake 3.28 or newer")
    endif()
    include(FetchContent)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e   # main, 2026-09-12
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(clap_juce_extensions)
    clap_juce_extensions_plugin(TARGET Anneal
        CLAP_ID "com.qulic.anneal"
        CLAP_FEATURES audio-effect equalizer distortion stereo)
endif()

# ---------------------------------------------------------------------------
# Headless DSP/processor acceptance tests + benchmark.
juce_add_console_app(AnnealTests PRODUCT_NAME "AnnealTests")
target_sources(AnnealTests PRIVATE tests/AnnealTests.cpp)
# The reference-file test runs the message loop while the "audio thread" keeps processing.
target_compile_definitions(AnnealTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
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
