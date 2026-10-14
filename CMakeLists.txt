cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
    endif()
endif()

project(TETHER VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TTH_BUILD_TOOLS "Build the editor snapshot tool" OFF)
option(TTH_LTO "Link-time optimisation" ON)
option(TTH_BUILD_PLUGIN "Build the plug-in (off: engine tests only, no JUCE needed)" ON)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (empty = ./JUCE, ../JUCE or download JUCE 8.0.10)")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# ---- the link to norns: plain C++20, no JUCE ----------------------------------------------------
set(ENGINE_SOURCES Source/engine/NornsLink.cpp)

find_package(Threads REQUIRED)
enable_testing()
set(TTH_SYS_LIBS Threads::Threads $<$<PLATFORM_ID:Windows>:ws2_32>)

add_executable(TetherEngineTests tests/EngineTests.cpp ${ENGINE_SOURCES})
target_include_directories(TetherEngineTests PRIVATE Source tests)
target_link_libraries(TetherEngineTests PRIVATE ${TTH_SYS_LIBS})
add_test(NAME engine COMMAND TetherEngineTests)
set_tests_properties(engine PROPERTIES ENVIRONMENT "TETHER_TESTS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests")

# tether-audio, the norns side of the audio stream (the norns builds come from scripts/build-norns-audio.sh),
# and its end-to-end test against a real JACK server (dummy driver), Linux only
if(UNIX AND NOT APPLE)
    add_executable(tether-audio norns/tether-audio/tether_audio.cpp)
    target_include_directories(tether-audio PRIVATE Source)
    target_link_libraries(tether-audio PRIVATE ${TTH_SYS_LIBS} ${CMAKE_DL_LIBS})
    add_executable(TetherAudioE2E tests/AudioE2E.cpp)
    target_include_directories(TetherAudioE2E PRIVATE Source)
    target_link_libraries(TetherAudioE2E PRIVATE ${TTH_SYS_LIBS} ${CMAKE_DL_LIBS})
    add_test(NAME audio COMMAND TetherAudioE2E $<TARGET_FILE:tether-audio>)
endif()

# the norns mod itself, against a stub norns
find_program(TTH_LUA NAMES lua5.4 lua5.3 lua)
if(TTH_LUA)
    add_test(NAME mod COMMAND ${TTH_LUA} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mod_test.lua)
endif()

if(NOT TTH_BUILD_PLUGIN)
    return()
endif()

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt)
    add_subdirectory(JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- family-sdk: FamilyBus, SETBOOK, BLACKBOX, CONDUIT (docs: family-sdk/docs/LINK.md) ----------
# a local checkout instead: -DFETCHCONTENT_SOURCE_DIR_FAMILY_SDK=/path/to/family-sdk
include(FetchContent)
# offline: a family-sdk next to this folder (../family-sdk) is used as it is, nothing is downloaded
if(NOT FETCHCONTENT_SOURCE_DIR_FAMILY_SDK AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../family-sdk/cmake/FamilySDK.cmake)
    set(FETCHCONTENT_SOURCE_DIR_FAMILY_SDK ${CMAKE_CURRENT_SOURCE_DIR}/../family-sdk)
endif()
FetchContent_Declare(family_sdk
    GIT_REPOSITORY https://github.com/semierendonmez-commits/family-sdk
    GIT_TAG 605d1968628edba96a8c97fc130041cecab7a3fd)   # family-sdk 1.0.3 (Apple Clang 14-15)
FetchContent_MakeAvailable(family_sdk)

set(TTH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TTH_FORMATS AU)
endif()

juce_add_binary_data(TetherData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/fonts/Michroma-Regular.ttf
        Resources/fonts/VT323-Regular.ttf)
set_target_properties(TetherData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(PLUGIN_SOURCES
    ${ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/NornsPanel.cpp
    Source/ui/ParamList.cpp)
set(TTH_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

# TETHER: an instrument; norns' audio comes out of it
juce_add_plugin(Tether
    PRODUCT_NAME "TETHER"
    COMPANY_NAME "Semi"
    BUNDLE_ID "com.semi.tether"
    PLUGIN_MANUFACTURER_CODE Semi
    PLUGIN_CODE Tthr
    FORMATS ${TTH_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Network
    DESCRIPTION "norns from the DAW: screen, keys, encoders, parameters, automation, audio")

# TETHER FX: the same, as an effect: its input goes to norns, norns comes back on top of it
juce_add_plugin(TetherFx
    PRODUCT_NAME "TETHER FX"
    COMPANY_NAME "Semi"
    BUNDLE_ID "com.semi.tetherfx"
    PLUGIN_MANUFACTURER_CODE Semi
    PLUGIN_CODE Tthf
    FORMATS ${TTH_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Network
    DESCRIPTION "TETHER as an effect: a track's audio to norns and back")

foreach(T Tether TetherFx)
    target_sources(${T} PRIVATE ${PLUGIN_SOURCES})
    target_include_directories(${T} PRIVATE Source)
    target_compile_definitions(${T} PUBLIC ${TTH_DEFS})
    target_link_libraries(${T}
        PRIVATE TetherData juce::juce_audio_utils ${TTH_SYS_LIBS}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(TTH_LTO)
        target_link_libraries(${T} PUBLIC juce::juce_recommended_lto_flags)
    endif()
    family_sdk_add(${T} MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/family.json)
endforeach()

set(TTH_APP_DEFS ${TTH_DEFS} JucePlugin_Name="TETHER" JucePlugin_Manufacturer="Semi" JucePlugin_VersionString="0.2.0"
    JucePlugin_IsSynth=1 JucePlugin_IsMidiEffect=0 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0)
set(TTH_FX_DEFS ${TTH_DEFS} JucePlugin_Name="TETHER FX" JucePlugin_Manufacturer="Semi" JucePlugin_VersionString="0.2.0"
    JucePlugin_IsSynth=0 JucePlugin_IsMidiEffect=0 JucePlugin_WantsMidiInput=0 JucePlugin_ProducesMidiOutput=0)

# ---- processor tests (JUCE) ----------------------------------------------------------------------
juce_add_console_app(TetherProcessorTests PRODUCT_NAME "TetherProcessorTests")
target_sources(TetherProcessorTests PRIVATE tests/ProcessorTests.cpp ${PLUGIN_SOURCES})
target_include_directories(TetherProcessorTests PRIVATE Source tests)
target_compile_definitions(TetherProcessorTests PRIVATE ${TTH_APP_DEFS})
target_link_libraries(TetherProcessorTests PRIVATE TetherData juce::juce_audio_utils juce::juce_recommended_config_flags ${TTH_SYS_LIBS})
family_sdk_add(TetherProcessorTests MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/family.json)
add_test(NAME processor COMMAND TetherProcessorTests)
set_tests_properties(processor PROPERTIES ENVIRONMENT "TETHER_TESTS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests")

# the audio stream through the processor, both builds, against a real JACK server (Linux)
if(UNIX AND NOT APPLE)
    foreach(KIND Synth Fx)
        juce_add_console_app(TetherAudio${KIND}Tests PRODUCT_NAME "TetherAudio${KIND}Tests")
        target_sources(TetherAudio${KIND}Tests PRIVATE tests/AudioProcessorTests.cpp ${PLUGIN_SOURCES})
        target_include_directories(TetherAudio${KIND}Tests PRIVATE Source tests)
        if(KIND STREQUAL "Fx")
            target_compile_definitions(TetherAudio${KIND}Tests PRIVATE ${TTH_FX_DEFS})
        else()
            target_compile_definitions(TetherAudio${KIND}Tests PRIVATE ${TTH_APP_DEFS})
        endif()
        target_link_libraries(TetherAudio${KIND}Tests PRIVATE TetherData juce::juce_audio_utils juce::juce_recommended_config_flags ${TTH_SYS_LIBS} ${CMAKE_DL_LIBS})
        family_sdk_add(TetherAudio${KIND}Tests MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/family.json)
        add_test(NAME audio_${KIND} COMMAND TetherAudio${KIND}Tests $<TARGET_FILE:tether-audio>)
    endforeach()
endif()

# ---- snapshot tool ---------------------------------------------------------------------------------
if(TTH_BUILD_TOOLS)
    juce_add_console_app(TetherSnapshot PRODUCT_NAME "TetherSnapshot")
    target_sources(TetherSnapshot PRIVATE tools/Snapshot.cpp ${PLUGIN_SOURCES})
    target_include_directories(TetherSnapshot PRIVATE Source tests)
    target_compile_definitions(TetherSnapshot PRIVATE ${TTH_APP_DEFS})
    target_link_libraries(TetherSnapshot PRIVATE TetherData juce::juce_audio_utils juce::juce_recommended_config_flags ${TTH_SYS_LIBS})
    family_sdk_add(TetherSnapshot MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/family.json)
endif()
