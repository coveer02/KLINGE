cmake_minimum_required(VERSION 3.22)

project(KLINGE VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

# ---------------------------------------------------------------------------
# Dependencies (pass -DJUCE_DIR=..., -DNAM_DIR=..., -DEIGEN_DIR=..., -DCLAP_JUCE_DIR=...
# or place them next to this file as JUCE/, deps/nam, deps/eigen, deps/clap-juce-extensions)
# ---------------------------------------------------------------------------
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
if(NOT DEFINED NAM_DIR)
    set(NAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/nam")
endif()
if(NOT DEFINED EIGEN_DIR)
    set(EIGEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/eigen")
endif()
if(NOT DEFINED CLAP_JUCE_DIR)
    set(CLAP_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/clap-juce-extensions")
endif()

foreach(dep_check "JUCE_DIR;CMakeLists.txt" "NAM_DIR;NAM/dsp.h" "EIGEN_DIR;Eigen/Core")
    list(GET dep_check 0 dep_var)
    list(GET dep_check 1 dep_file)
    if(NOT EXISTS "${${dep_var}}/${dep_file}")
        message(FATAL_ERROR "${dep_var} (${${dep_var}}) does not contain ${dep_file}. "
                            "Run tools/fetch-deps.sh or pass -D${dep_var}=<path>.")
    endif()
endforeach()

add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

option(KLINGE_BUILD_TESTS "Build the offline test harness and profiler" OFF)
option(KLINGE_BUILD_CLAP "Also build a CLAP plugin (needs clap-juce-extensions)" ON)

# ---------------------------------------------------------------------------
# Neural Amp Modeler core (MIT), compiled as its own library without JUCE's warning flags
# ---------------------------------------------------------------------------
file(GLOB NAM_SOURCES "${NAM_DIR}/NAM/*.cpp" "${NAM_DIR}/NAM/wavenet/*.cpp")
# OBJECT library: NAM registers its architectures in static initialisers, which a static
# library would let the linker drop
add_library(nam_core OBJECT ${NAM_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC "${NAM_DIR}" "${NAM_DIR}/Dependencies/nlohmann" "${EIGEN_DIR}")
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT EIGEN_MPL2_ONLY)
if(MSVC)
    target_compile_options(nam_core PRIVATE /W0 /bigobj)
else()
    target_compile_options(nam_core PRIVATE -w -O3)
endif()

# ---------------------------------------------------------------------------
set(KLINGE_FORMATS VST3)
if(APPLE)
    list(APPEND KLINGE_FORMATS AU)
endif()

juce_add_plugin(KLINGE
    COMPANY_NAME "KLINGE Audio"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.klingeaudio.klinge"
    PLUGIN_MANUFACTURER_CODE Klng
    PLUGIN_CODE Kl01
    FORMATS ${KLINGE_FORMATS}
    PRODUCT_NAME "KLINGE"
    DESCRIPTION "Distortion multitool"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(KlingeBinaryData SOURCES
    Resources/SpaceGrotesk-Regular.ttf
    Resources/SpaceGrotesk-Medium.ttf
    Resources/SpaceGrotesk-Bold.ttf
    Resources/JetBrainsMono-Medium.ttf)

set(KLINGE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp
    Source/Session.cpp
    Source/dsp/Params.cpp
    Source/dsp/Circuits.cpp
    Source/dsp/Band.cpp
    Source/dsp/Engine.cpp
    Source/dsp/Modulation.cpp
    Source/dsp/Capture.cpp
    Source/ui/LookAndFeel.cpp
    Source/ui/Widgets.cpp
    Source/ui/Panels.cpp
    Source/ui/ModUI.cpp
    Source/ui/Analyzer.cpp
    Source/ui/PresetBrowser.cpp)

target_sources(KLINGE PRIVATE ${KLINGE_SOURCES})
target_include_directories(KLINGE PRIVATE Source)

set(KLINGE_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
target_compile_definitions(KLINGE PUBLIC ${KLINGE_DEFINES})

target_link_libraries(KLINGE
    PRIVATE
        KlingeBinaryData
        nam_core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# CLAP
# ---------------------------------------------------------------------------
if(KLINGE_BUILD_CLAP AND EXISTS "${CLAP_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${CLAP_JUCE_DIR} ${CMAKE_BINARY_DIR}/clap_juce_extensions EXCLUDE_FROM_ALL)
    clap_juce_extensions_plugin(TARGET KLINGE
        CLAP_ID "com.klingeaudio.klinge"
        CLAP_FEATURES audio-effect distortion stereo mono)
    target_compile_definitions(KLINGE PRIVATE KLINGE_WITH_CLAP=1)
endif()

# ---------------------------------------------------------------------------
# Tests and profiler (console apps compiling the plugin sources directly)
# ---------------------------------------------------------------------------
if(KLINGE_BUILD_TESTS)
    foreach(tool KlingeTests KlingeProfiler)
        juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
        target_include_directories(${tool} PRIVATE Source)
        target_compile_definitions(${tool} PRIVATE ${KLINGE_DEFINES}
            JucePlugin_VersionString="${PROJECT_VERSION}"
            JucePlugin_Name="KLINGE"
            JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(${tool} PRIVATE KlingeBinaryData nam_core
            juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endforeach()
    target_sources(KlingeTests PRIVATE tests/TestHarness.cpp ${KLINGE_SOURCES})
    set_source_files_properties(tests/TestHarness.cpp PROPERTIES
        COMPILE_DEFINITIONS "KLINGE_NAM_EXAMPLES=\"${NAM_DIR}/example_models\"")
    target_sources(KlingeProfiler PRIVATE tools/Profiler.cpp)
endif()
