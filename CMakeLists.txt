cmake_minimum_required(VERSION 3.22)

project(AIDrumVST VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Statically link the MSVC runtime on Windows so the Standalone .exe and
# the VST3 run on fresh machines without needing Microsoft's VC++ 2015-2022
# redistributable installed.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "" FORCE)
endif()

# Warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

set(JUCE_VERSION 8.0.4 CACHE STRING "JUCE version to fetch")

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${JUCE_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
# Build AU on macOS (alongside VST3); other platforms just build VST3 + Standalone.
# AAX is intentionally omitted: Avid's AAX SDK is NDA-gated and distribution
# requires PACE iLok signing. Wire it in once you have access.
set(AIDRUM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AIDRUM_FORMATS AU)
endif()

juce_add_plugin(AIDrumVST
    COMPANY_NAME              "HumHouse"
    BUNDLE_ID                 "com.humhouse.humhousedrums"
    PLUGIN_MANUFACTURER_CODE  Humh
    PLUGIN_CODE               Hhdr
    FORMATS                   ${AIDRUM_FORMATS}
    PRODUCT_NAME              "HumHouse Drums"
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         TRUE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           "Instrument" "Drum"
)

juce_generate_juce_header(AIDrumVST)

# v1.4.0 — bundle the CC0 default drum kit so the plugin makes
# real-sample sound out of the box, no user setup required.
file(GLOB AIDRUM_DEFAULT_KIT_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/DefaultKit/*.wav")
if(AIDRUM_DEFAULT_KIT_FILES)
    juce_add_binary_data(AIDrumBundledKit
        HEADER_NAME BundledKitData.h
        NAMESPACE   BundledKitData
        SOURCES     ${AIDRUM_DEFAULT_KIT_FILES})
    target_link_libraries(AIDrumVST PRIVATE AIDrumBundledKit)
    target_compile_definitions(AIDrumVST PRIVATE AIDRUM_HAS_BUNDLED_KIT=1)
else()
    target_compile_definitions(AIDrumVST PRIVATE AIDRUM_HAS_BUNDLED_KIT=0)
endif()

# v1.6.1-rc.7 — bundle the HumHouse Drums logo + branding artwork so the
# editor renders the masthead even when the user has no asset folder
# alongside the plugin.
file(GLOB AIDRUM_BRANDING_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Branding/*.png")
if(AIDRUM_BRANDING_FILES)
    juce_add_binary_data(AIDrumBranding
        HEADER_NAME BrandingData.h
        NAMESPACE   BrandingData
        SOURCES     ${AIDRUM_BRANDING_FILES})
    target_link_libraries(AIDrumVST PRIVATE AIDrumBranding)
    target_compile_definitions(AIDrumVST PRIVATE AIDRUM_HAS_BRANDING=1)
else()
    target_compile_definitions(AIDrumVST PRIVATE AIDRUM_HAS_BRANDING=0)
endif()

target_sources(AIDrumVST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/AIBackend.cpp
    Source/AIBackend.h
    Source/DrumKit.cpp
    Source/DrumKit.h
    Source/DrumSynth.h
    Source/DrumBusMixer.h
    Source/SampleKit.h
    Source/SampleKit.cpp
    Source/MixerPanel.h
    Source/MidiPattern.h
    Source/GothicLookAndFeel.h
    Source/PatternStrip.h
    Source/ArrangementStrip.h
    Source/ManualGrid.h
)

target_compile_definitions(AIDrumVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(AIDrumVST PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
