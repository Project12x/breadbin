cmake_minimum_required(VERSION 3.22)
project(Breadbin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CPM for dependency management
set(CPM_DOWNLOAD_VERSION 0.38.1)
if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
    file(DOWNLOAD
        "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
        "${CMAKE_BINARY_DIR}/cmake/CPM.cmake"
    )
endif()
include(${CMAKE_BINARY_DIR}/cmake/CPM.cmake)

# JUCE 8
CPMAddPackage(
    NAME JUCE
    GIT_TAG 8.0.4
    GITHUB_REPOSITORY juce-framework/JUCE
    OPTIONS
        "JUCE_BUILD_EXTRAS OFF"
        "JUCE_BUILD_EXAMPLES OFF"
)

# libsidplayfp (for reSIDfp)
CPMAddPackage(
    NAME libsidplayfp
    GIT_TAG v2.16.0
    GITHUB_REPOSITORY libsidplayfp/libsidplayfp
    DOWNLOAD_ONLY YES
)

# Collect reSIDfp source files
set(RESIDFP_DIR "${libsidplayfp_SOURCE_DIR}/src/builders/residfp-builder/residfp")
file(GLOB RESIDFP_SOURCES
    "${RESIDFP_DIR}/*.cpp"
    "${RESIDFP_DIR}/*.h"
    "${RESIDFP_DIR}/resample/*.cpp"
    "${RESIDFP_DIR}/resample/*.h"
)
# Exclude test file (uses deprecated std::auto_ptr)
list(FILTER RESIDFP_SOURCES EXCLUDE REGEX "test\\.cpp$")

# Plugin sources
set(PLUGIN_SOURCES
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/SIDEngine.cpp
    src/SIDEngine.h
    src/residfp/version.cc
)

# Add binary assets (background image)
juce_add_binary_data(BreadbinAssets
    SOURCES
        assets/background.jpg
)

juce_add_plugin(Breadbin
    COMPANY_NAME "GPL Audio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Gplx
    PLUGIN_CODE Bred
    FORMATS VST3 Standalone
    PRODUCT_NAME "Breadbin"
    DESCRIPTION "C64 Dual SID Synthesizer"
)

target_sources(Breadbin PRIVATE
    ${PLUGIN_SOURCES}
    ${RESIDFP_SOURCES}
)

target_include_directories(Breadbin PRIVATE
    # Local config headers (must come FIRST to override missing .in templates)
    src/residfp
    ${libsidplayfp_SOURCE_DIR}/src
    ${libsidplayfp_SOURCE_DIR}/src/builders/residfp-builder
    ${RESIDFP_DIR}
    ${RESIDFP_DIR}/resample
    src
)

target_compile_definitions(Breadbin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Disable plugin host formats that cause compile issues on Windows
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_AU=0
    # HarfBuzz fix for SDK 26100
    HB_MUTEX_IMPL_STD_MUTEX=1
    # Windows socket fix
    WIN32
)

# MSVC parallel build fix
if(MSVC)
    target_compile_options(Breadbin PRIVATE /FS)
endif()

target_link_libraries(Breadbin
    PRIVATE
        BreadbinAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
