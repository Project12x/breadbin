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

# libsidplayfp (for reSIDfp + full player engine)
# Use release tarball (not git) because it includes pre-built .bin files
# (psiddrv.bin, sidplayer1.bin, sidplayer2.bin) generated from 6502 assembly
CPMAddPackage(
    NAME libsidplayfp
    VERSION 2.16.0
    URL https://github.com/libsidplayfp/libsidplayfp/releases/download/v2.16.0/libsidplayfp-2.16.0.tar.gz
    DOWNLOAD_ONLY YES
)

set(GHOSTMOONGPL_DIR "C:/Users/estee/Desktop/My Stuff/Code/Antigravity/ghostmoongpl"
    CACHE PATH "Path to the GhostmoonGPL repo")
add_subdirectory("${GHOSTMOONGPL_DIR}" "${CMAKE_BINARY_DIR}/ghostmoongpl-build")

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

# libsidplayfp player engine sources (C64 machine emulator, SID tune loader, player)
set(SIDPLAYFP_SRC "${libsidplayfp_SOURCE_DIR}/src")
set(SIDPLAYFP_SOURCES
    # Core engine
    ${SIDPLAYFP_SRC}/EventScheduler.cpp
    ${SIDPLAYFP_SRC}/player.cpp
    ${SIDPLAYFP_SRC}/mixer.cpp
    ${SIDPLAYFP_SRC}/simpleMixer.cpp
    ${SIDPLAYFP_SRC}/psiddrv.cpp
    ${SIDPLAYFP_SRC}/reloc65.cpp
    ${SIDPLAYFP_SRC}/sidemu.cpp
    # Public API
    ${SIDPLAYFP_SRC}/sidplayfp/sidplayfp.cpp
    ${SIDPLAYFP_SRC}/sidplayfp/SidConfig.cpp
    ${SIDPLAYFP_SRC}/sidplayfp/SidInfo.cpp
    ${SIDPLAYFP_SRC}/sidplayfp/SidTune.cpp
    ${SIDPLAYFP_SRC}/sidplayfp/SidTuneInfo.cpp
    ${SIDPLAYFP_SRC}/sidplayfp/sidbuilder.cpp
    # Builder bridge (reSIDfp)
    ${SIDPLAYFP_SRC}/builders/residfp-builder/residfp-builder.cpp
    ${SIDPLAYFP_SRC}/builders/residfp-builder/residfp-emu.cpp
    # C64 machine
    ${SIDPLAYFP_SRC}/c64/c64.cpp
    ${SIDPLAYFP_SRC}/c64/mmu.cpp
    # CPU (6502)
    ${SIDPLAYFP_SRC}/c64/CPU/mos6510.cpp
    ${SIDPLAYFP_SRC}/c64/CPU/mos6510debug.cpp
    # CIA
    ${SIDPLAYFP_SRC}/c64/CIA/SerialPort.cpp
    ${SIDPLAYFP_SRC}/c64/CIA/interrupt.cpp
    ${SIDPLAYFP_SRC}/c64/CIA/mos652x.cpp
    ${SIDPLAYFP_SRC}/c64/CIA/timer.cpp
    ${SIDPLAYFP_SRC}/c64/CIA/tod.cpp
    # VIC-II
    ${SIDPLAYFP_SRC}/c64/VIC_II/mos656x.cpp
    # Tune loaders
    ${SIDPLAYFP_SRC}/sidtune/MUS.cpp
    ${SIDPLAYFP_SRC}/sidtune/PSID.cpp
    ${SIDPLAYFP_SRC}/sidtune/SidTuneBase.cpp
    ${SIDPLAYFP_SRC}/sidtune/SidTuneTools.cpp
    ${SIDPLAYFP_SRC}/sidtune/p00.cpp
    ${SIDPLAYFP_SRC}/sidtune/prg.cpp
    # Utils
    ${SIDPLAYFP_SRC}/utils/MD5/MD5.cpp
    ${SIDPLAYFP_SRC}/utils/md5Factory.cpp
)

# Plugin sources
set(PLUGIN_SOURCES
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/SIDEngine.cpp
    src/SIDEngine.h
    src/SidFilePlayer.cpp
    src/SidFilePlayer.h
    src/residfp/version.cc
)

# Add binary assets (background image, fonts)
juce_add_binary_data(BreadbinAssets
    SOURCES
        assets/background.jpg
        assets/PressStart2P-Regular.ttf
        assets/Lato-Regular.ttf
        assets/Lato-Bold.ttf
        assets/JetBrainsMono-Regular.ttf
)

juce_add_plugin(Breadbin
    COMPANY_NAME "Eric Steenwerth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Estw
    PLUGIN_CODE Bred
    FORMATS VST3 Standalone
    PRODUCT_NAME "Breadbin"
    DESCRIPTION "C64 Dual SID Synthesizer"
)

target_sources(Breadbin PRIVATE
    ${PLUGIN_SOURCES}
    ${RESIDFP_SOURCES}
    ${SIDPLAYFP_SOURCES}
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

# ASIO Support (Windows standalone)
if(WIN32)
    set(ASIO_SDK_PATH "C:/SDKs/asiosdk/ASIOSDK" CACHE PATH "Path to Steinberg ASIO SDK")
    if(EXISTS "${ASIO_SDK_PATH}/common/asio.h")
        message(STATUS "ASIO SDK found at ${ASIO_SDK_PATH}")
        target_compile_definitions(Breadbin PUBLIC JUCE_ASIO=1)
        target_include_directories(Breadbin PRIVATE "${ASIO_SDK_PATH}/common")
    else()
        message(WARNING "ASIO SDK not found at ${ASIO_SDK_PATH}/common/asio.h - building without ASIO support")
    endif()
endif()

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

# HAVE_CONFIG_H only for libsidplayfp sources (not JUCE, which has its own pnglib config.h)
set_source_files_properties(${SIDPLAYFP_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS HAVE_CONFIG_H
)

# MSVC parallel build fix
if(MSVC)
    target_compile_options(Breadbin PRIVATE /FS)
endif()

target_link_libraries(Breadbin
    PRIVATE
        BreadbinAssets
        ghostmoongpl::ui
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_gui_basics
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Test suites ---

# Standalone LFO math tests (no JUCE dependency, fast)
add_executable(BreadbinLFOTests
    tests/LFOTests.cpp
)
target_compile_features(BreadbinLFOTests PRIVATE cxx_std_20)

# Standalone mutation tests (no JUCE dependency, fast)
add_executable(BreadbinMutationTests
    tests/MutationTests.cpp
)
target_compile_features(BreadbinMutationTests PRIVATE cxx_std_20)

# Integration tests (JUCE-linked, full signal path)
add_executable(BreadbinIntegrationTests
    tests/IntegrationTests.cpp
    ${PLUGIN_SOURCES}
    ${RESIDFP_SOURCES}
    ${SIDPLAYFP_SOURCES}
)
target_include_directories(BreadbinIntegrationTests PRIVATE
    src/residfp
    ${libsidplayfp_SOURCE_DIR}/src
    ${libsidplayfp_SOURCE_DIR}/src/builders/residfp-builder
    ${RESIDFP_DIR}
    ${RESIDFP_DIR}/resample
    src
)
target_link_libraries(BreadbinIntegrationTests PRIVATE
    BreadbinAssets
    ghostmoongpl::ui
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_gui_basics
    juce::juce_dsp
)
target_compile_definitions(BreadbinIntegrationTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_AU=0
    HB_MUTEX_IMPL_STD_MUTEX=1
    WIN32
)
target_compile_features(BreadbinIntegrationTests PRIVATE cxx_std_20)
# HAVE_CONFIG_H scoped to sidplayfp sources only (already set via set_source_files_properties above)
if(MSVC)
    target_compile_options(BreadbinIntegrationTests PRIVATE /FS)
endif()

enable_testing()
add_test(NAME LFOTests COMMAND BreadbinLFOTests)
add_test(NAME MutationTests COMMAND BreadbinMutationTests)
add_test(NAME IntegrationTests COMMAND BreadbinIntegrationTests)

