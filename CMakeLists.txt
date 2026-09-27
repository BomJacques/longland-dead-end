cmake_minimum_required(VERSION 3.22)

project(LonglandDeadEnd VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_PATH "C:/SDK/JUCE" CACHE PATH "Path to JUCE")
if (NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found. Set JUCE_PATH to the JUCE 9 checkout.")
endif()

add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

juce_add_plugin(LonglandDeadEnd
    COMPANY_NAME "OpenAI"
    BUNDLE_ID "com.openai.longlanddeadend"
    PLUGIN_MANUFACTURER_CODE OAI1
    PLUGIN_CODE LDED
    FORMATS VST3
    PRODUCT_NAME "LONGLAND DEAD END"
    DESCRIPTION "Dreamy rolling-memory afterimage processor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(LonglandDeadEnd)

target_sources(LonglandDeadEnd PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

target_include_directories(LonglandDeadEnd PRIVATE Source)

target_compile_definitions(LonglandDeadEnd
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LonglandDeadEnd
    PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

set_target_properties(LonglandDeadEnd PROPERTIES FOLDER "LONGLAND DEAD END")

option(LONGLAND_BUILD_PROBE "Build the offline DSP probe" ON)
if (LONGLAND_BUILD_PROBE)
    add_executable(LonglandDeadEndProbe
        Tests/ProcessorProbe.cpp
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp)
    target_include_directories(LonglandDeadEndProbe PRIVATE Source)
    target_compile_definitions(LonglandDeadEndProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(LonglandDeadEndProbe PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_include_directories(LonglandDeadEndProbe PRIVATE
        "${CMAKE_BINARY_DIR}/LonglandDeadEnd_artefacts/JuceLibraryCode")
endif()
