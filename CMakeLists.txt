cmake_minimum_required(VERSION 3.22)

project(SPOOL VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Be explicit about runtime so JUCE static libs link cleanly on MSVC.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ---- Fetch JUCE ------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---- SPOOL plugin / standalone --------------------------------------------
# FORMATS: Standalone for the .exe; VST3 for DAW use on Windows + Linux;
# AudioUnit when building on macOS.
set(SPOOL_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND SPOOL_FORMATS AU)
endif()

juce_add_plugin(SPOOL
    COMPANY_NAME                  "itselliott"
    COMPANY_WEBSITE               "https://github.com/itselliott/spool"
    BUNDLE_ID                     com.itselliott.spool
    PLUGIN_MANUFACTURER_CODE      ItsE
    PLUGIN_CODE                   Spol
    FORMATS                       ${SPOOL_FORMATS}
    PRODUCT_NAME                  "SPOOL"
    DESCRIPTION                   "TP-7-inspired sampler / looper / field recorder"
    VST3_CATEGORIES               Fx Sampler
    AU_MAIN_TYPE                  kAudioUnitType_Effect
    IS_SYNTH                      FALSE   # stays an Fx plugin so insert-FX passthrough still works
    NEEDS_MIDI_INPUT              TRUE    # play the loaded sample chromatically via MIDI notes (C4 = root)
    NEEDS_MIDI_OUTPUT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   TRUE       # spacebar + 1-8 slot keys
    COPY_PLUGIN_AFTER_BUILD       FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "SPOOL needs microphone access to record audio."
)

target_sources(SPOOL PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/SpoolStandaloneApp.cpp
)

# ---- Embed the bg.png as a binary resource --------------------------------
# Without this the chassis background would silently miss in installed
# builds (the release zip wouldn't carry assets/ alongside the binary).
# Embedding bakes the image into the executable / plugin module so it works
# everywhere by default; users can still override by dropping their own
# bg.png into <cwd>/assets/ or %APPDATA%/SPOOL/assets/.
juce_add_binary_data(SPOOL_BinaryData
    SOURCES
        assets/bg.png
)
target_link_libraries(SPOOL PRIVATE SPOOL_BinaryData)

target_compile_definitions(SPOOL PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1  # we provide our own entry point + extended Options menu
)

target_link_libraries(SPOOL PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
