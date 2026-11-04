cmake_minimum_required(VERSION 3.22)
project(FlatQ VERSION 0.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(FlatQ
    PRODUCT_NAME "flatQ"
    COMPANY_NAME "BandPass Records"           # the vendor shown by hosts
    BUNDLE_ID "com.bandpassrecords.flatq"
    COMPANY_WEBSITE "https://flatq.bandpassrecords.com"
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icon.png"
    PLUGIN_MANUFACTURER_CODE FlTq
    PLUGIN_CODE FlQp
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)   # no install step; unsigned/ad-hoc builds are fine for personal use

# Built-in profiles: everything in profiles/ is embedded in the plugin (BinaryData.h)
file(GLOB FLATQ_BUILTIN CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/profiles/*.flatq")   # profiles/local/ (personal, git-ignored) is NOT embedded; the embedded ones are only offered in the search
juce_add_binary_data(FlatQProfiles SOURCES ${FLATQ_BUILTIN})
# AutoEq targets (MIT, see targets/LICENSE-AutoEq.txt) for the measurement import
file(GLOB FLATQ_TARGETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/targets/*.csv")
juce_add_binary_data(FlatQTargets HEADER_NAME "TargetData.h" NAMESPACE FlatQTargetData SOURCES ${FLATQ_TARGETS})
set_target_properties(FlatQTargets PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(FlatQProfiles PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Measurement pack (data/measurements/, built by tools/build_measurement_db.py; git-ignored, personal use): optional
set(FLATQ_MEAS "${CMAKE_SOURCE_DIR}/data/measurements")
if(EXISTS "${FLATQ_MEAS}/measurements.tsv" AND EXISTS "${FLATQ_MEAS}/measurements.bin.gz")
    juce_add_binary_data(FlatQMeasurements HEADER_NAME "MeasurementData.h" NAMESPACE FlatQMeasurementData
        SOURCES "${FLATQ_MEAS}/measurements.tsv" "${FLATQ_MEAS}/measurements.bin.gz")
    set_target_properties(FlatQMeasurements PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(FLATQ_MEAS_TARGET FlatQMeasurements)
    set(FLATQ_MEAS_DEF FLATQ_HAS_MEASUREMENTS=1)
else()
    message(STATUS "No measurement pack (run tools/build_measurement_db.py): building without the measurement search")
    set(FLATQ_MEAS_DEF FLATQ_HAS_MEASUREMENTS=0)
endif()

target_sources(FlatQ PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(FlatQ PRIVATE src)
target_compile_definitions(FlatQ PUBLIC ${FLATQ_MEAS_DEF} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(FlatQ PRIVATE FlatQProfiles FlatQTargets ${FLATQ_MEAS_TARGET} juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Tools (no JUCE) and tests
add_executable(lvls_dump tools/lvls_dump.cpp)
add_executable(design_test tools/design_test.cpp)
add_executable(flatq_convert tools/flatq_convert.cpp)
add_executable(autoeq_import tools/autoeq_import.cpp)

enable_testing()
file(GLOB_RECURSE FLATQ_PROFILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/profiles/*.flatq")
foreach(p ${FLATQ_PROFILES})
    get_filename_component(n "${p}" NAME_WE)
    add_test(NAME "design_${n}" COMMAND design_test "${p}")
    add_test(NAME "roundtrip_${n}" COMMAND flatq_convert "${p}" "${CMAKE_BINARY_DIR}/roundtrip_${n}.flatq")
endforeach()
