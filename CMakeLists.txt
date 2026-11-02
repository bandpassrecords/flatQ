cmake_minimum_required(VERSION 3.22)
project(FreeQ VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(FreeQ
    PRODUCT_NAME "FreeQ"
    COMPANY_NAME "FreeQ"
    PLUGIN_MANUFACTURER_CODE FrEQ
    PLUGIN_CODE FqPl
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)   # no install step; unsigned/ad-hoc builds are fine for personal use

# Built-in profiles: everything in profiles/ is embedded in the plugin (BinaryData.h)
file(GLOB FREEQ_BUILTIN CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/profiles/*.lvls" "${CMAKE_SOURCE_DIR}/profiles/*.tmreq")
juce_add_binary_data(FreeQProfiles SOURCES ${FREEQ_BUILTIN})
set_target_properties(FreeQProfiles PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(FreeQ PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(FreeQ PRIVATE src)
target_compile_definitions(FreeQ PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(FreeQ PRIVATE FreeQProfiles juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Parser/design unit tools (no JUCE)
add_executable(lvls_dump tools/lvls_dump.cpp)
add_executable(design_test tools/design_test.cpp)

enable_testing()
file(GLOB FREEQ_PROFILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/profiles/*.lvls" "${CMAKE_SOURCE_DIR}/profiles/*.tmreq")
foreach(p ${FREEQ_PROFILES})
    get_filename_component(n "${p}" NAME_WE)
    add_test(NAME "design_${n}" COMMAND design_test "${p}")
endforeach()
