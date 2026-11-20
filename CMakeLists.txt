cmake_minimum_required(VERSION 3.22)
project(AmariKeys VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local checkout if JUCE_DIR is given, otherwise fetch 8.0.12
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(AmariKeys
    COMPANY_NAME "Amari Labs"
    PLUGIN_MANUFACTURER_CODE AMAr
    PLUGIN_CODE AKey
    FORMATS VST3 Standalone
    PRODUCT_NAME "Amari Keys"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

target_sources(AmariKeys PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(AmariKeys PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(AmariKeys PRIVATE /utf-8)
endif()

target_link_libraries(AmariKeys
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional offline test harness (renders demo WAVs + a UI snapshot). Not built in CI.
option(AMARI_BUILD_TESTS "Build offline render test" OFF)
if(AMARI_BUILD_TESTS)
    juce_add_console_app(AmariKeysTest PRODUCT_NAME "AmariKeysTest")
    target_sources(AmariKeysTest PRIVATE tests/RenderTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(AmariKeysTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="AmariKeys")
    target_link_libraries(AmariKeysTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Amari Sampler: standalone app that records a VST3 instrument into a multisample folder + SFZ
juce_add_gui_app(AmariSampler
    PRODUCT_NAME "Amari Sampler"
    COMPANY_NAME "Amari Labs"
    VERSION 0.2.0)
target_sources(AmariSampler PRIVATE AmariSampler/Main.cpp)
target_compile_definitions(AmariSampler PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="Amari Sampler"
    JUCE_APPLICATION_VERSION_STRING="0.2.0")
target_link_libraries(AmariSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Offline round-trip test for the sampler engine (renders a test synth, then loads the result). Not built in CI.
if(AMARI_BUILD_TESTS)
    juce_add_console_app(SamplerRoundTrip PRODUCT_NAME "SamplerRoundTrip")
    target_sources(SamplerRoundTrip PRIVATE tests/SamplerRoundTrip.cpp)
    target_compile_definitions(SamplerRoundTrip PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SamplerRoundTrip PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_events)
    juce_add_console_app(Vst2RoundTrip PRODUCT_NAME "Vst2RoundTrip")
    target_sources(Vst2RoundTrip PRIVATE tests/Vst2RoundTrip.cpp)
    target_compile_definitions(Vst2RoundTrip PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(Vst2RoundTrip PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_events)
endif()
