cmake_minimum_required(VERSION 3.22)

project(SPOOL VERSION 1.0.0 LANGUAGES CXX)

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
    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              FALSE
    NEEDS_MIDI_OUTPUT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   TRUE       # spacebar + 1-8 slot keys
    COPY_PLUGIN_AFTER_BUILD       FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "SPOOL needs microphone access to record audio."
)

target_sources(SPOOL PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
)

target_compile_definitions(SPOOL PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SPOOL PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
