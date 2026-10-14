cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
    endif()
endif()

project(TETHER VERSION 0.1.0 LANGUAGES C CXX)

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

# TETHER: an instrument with MIDI in and out, no sound of its own (norns makes the sound)
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
    DESCRIPTION "norns from the DAW: screen, keys, encoders, parameters, automation")

target_sources(Tether PRIVATE ${PLUGIN_SOURCES})
target_include_directories(Tether PRIVATE Source)
target_compile_definitions(Tether PUBLIC ${TTH_DEFS})
target_link_libraries(Tether
    PRIVATE TetherData juce::juce_audio_utils ${TTH_SYS_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(TTH_LTO)
    target_link_libraries(Tether PUBLIC juce::juce_recommended_lto_flags)
endif()
family_sdk_add(Tether MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/family.json)

set(TTH_APP_DEFS ${TTH_DEFS} JucePlugin_Name="TETHER" JucePlugin_Manufacturer="Semi" JucePlugin_VersionString="0.1.0"
    JucePlugin_IsSynth=1 JucePlugin_IsMidiEffect=0 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0)

# ---- processor tests (JUCE) ----------------------------------------------------------------------
juce_add_console_app(TetherProcessorTests PRODUCT_NAME "TetherProcessorTests")
target_sources(TetherProcessorTests PRIVATE tests/ProcessorTests.cpp ${PLUGIN_SOURCES})
target_include_directories(TetherProcessorTests PRIVATE Source tests)
target_compile_definitions(TetherProcessorTests PRIVATE ${TTH_APP_DEFS})
target_link_libraries(TetherProcessorTests PRIVATE TetherData juce::juce_audio_utils juce::juce_recommended_config_flags ${TTH_SYS_LIBS})
family_sdk_add(TetherProcessorTests MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/family.json)
add_test(NAME processor COMMAND TetherProcessorTests)
set_tests_properties(processor PROPERTIES ENVIRONMENT "TETHER_TESTS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests")

# ---- snapshot tool ---------------------------------------------------------------------------------
if(TTH_BUILD_TOOLS)
    juce_add_console_app(TetherSnapshot PRODUCT_NAME "TetherSnapshot")
    target_sources(TetherSnapshot PRIVATE tools/Snapshot.cpp ${PLUGIN_SOURCES})
    target_include_directories(TetherSnapshot PRIVATE Source tests)
    target_compile_definitions(TetherSnapshot PRIVATE ${TTH_APP_DEFS})
    target_link_libraries(TetherSnapshot PRIVATE TetherData juce::juce_audio_utils juce::juce_recommended_config_flags ${TTH_SYS_LIBS})
    family_sdk_add(TetherSnapshot MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/family.json)
endif()
