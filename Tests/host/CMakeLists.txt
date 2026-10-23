cmake_minimum_required(VERSION 3.22)
project(SkitterHostTest VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE JUCE)

juce_add_console_app(HostTest PRODUCT_NAME "HostTest")
target_sources(HostTest PRIVATE HostTest.cpp ../../Source/PluginProcessor.cpp ../../Source/PluginEditor.cpp)
target_compile_definitions(HostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JucePlugin_Name="Skitter")
target_link_libraries(HostTest PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags)
