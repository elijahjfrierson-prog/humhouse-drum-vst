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
#
# v1.6.1-rc.18 — AAX (Pro Tools) target is now wired in, but only enabled
# when the user supplies the AAX SDK path locally. Avid's AAX SDK is
# NDA-gated and not redistributable, so CI does NOT build AAX (the path
# isn't set on the runner). To compile AAX locally:
#
#   1. Register at the Avid Developer Program and download the AAX SDK.
#   2. Configure CMake with -DAIDRUM_AAX_SDK_PATH=/path/to/AAX_SDK_2_x_x
#   3. Build the AAX target:
#        cmake --build build --target AIDrumVST_AAX -j$(nproc)
#
# Distributing the resulting .aaxplugin to other machines requires a PACE
# iLok signing certificate (~$295/yr) — without that the binary is
# locally-loadable in your own Pro Tools install but won't load on
# anyone else's. We do NOT ship AAX in the GitHub release artifacts.
set(AIDRUM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AIDRUM_FORMATS AU)
endif()

set(AIDRUM_AAX_SDK_PATH "" CACHE PATH
    "Path to a local AAX SDK checkout (enables the AAX build target). Leave empty on CI.")
if(AIDRUM_AAX_SDK_PATH)
    if(EXISTS "${AIDRUM_AAX_SDK_PATH}")
        message(STATUS "HumHouse Drums: enabling AAX target (SDK at ${AIDRUM_AAX_SDK_PATH})")
        juce_set_aax_sdk_path("${AIDRUM_AAX_SDK_PATH}")
        list(APPEND AIDRUM_FORMATS AAX)
    else()
        message(WARNING "AIDRUM_AAX_SDK_PATH (${AIDRUM_AAX_SDK_PATH}) does not exist — AAX target disabled.")
    endif()
endif()

# v1.6.1-rc.29 — SECOND identity bump. rc.24 already moved us to
# Mk2 (Hhm2 / Hh24) and rc.28 inherited those codes. User reported
# rc.28 crashes FL Studio Mac on plugin load — same class of issue
# as the rc.23 → rc.24 transition where FL Studio's plugin DB held
# the cached validation result of the old binary, then served a
# half-stale/half-new bundle when the new install replaced the
# bundle in place under the same component identity. User's words:
# "make a new file every time and for Mac too". Going forward we
# bump these three on EVERY rc — the cost is one re-scan in the
# host on first launch, the benefit is no cached-validation
# crashes ever again.
#
# rc.30 codes: Mk4 / Hhm4 / Hh30 (29 → 30 mirrors the rc number).
# PRODUCT_NAME / COMPANY_NAME stay stable so the packaging pipeline
# (macOS .pkg/.dmg, Windows .exe, CI artifact names) doesn't have to
# change in lockstep.
juce_add_plugin(AIDrumVST
    COMPANY_NAME              "HumHouse"
    BUNDLE_ID                 "com.humhouse.humhousedrumsx"
    PLUGIN_MANUFACTURER_CODE  Hhm5
    PLUGIN_CODE               HhX1
    FORMATS                   ${AIDRUM_FORMATS}
    PRODUCT_NAME              "HumHouse Drums X"
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         TRUE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           "Instrument" "Drum"
    AAX_CATEGORY              "AAX_EPlugInCategory_SWGenerators"
    # v1.6.1-rc.10 — HumHouse crest as the plugin / .vst3 / .component
    # bundle icon. JUCE rewrites this to .icns on macOS and embeds it as
    # the resource icon group on Windows VST3 builds.
    ICON_BIG                  "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icons/HumHouseAppIcon-1024.png"
    ICON_SMALL                "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icons/HumHouseAppIcon-1024.png"
)

juce_generate_juce_header(AIDrumVST)

# HumHouse Drums X ships exactly one kit: the SoCal rock kit sliced from
# HumHouse's own recordings by tools/kitx/slice_kit.py.
file(GLOB AIDRUM_DEFAULT_KIT_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/KitX/*.wav")
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

# The compiled human-performance corpus (Magenta Groove MIDI Dataset, CC-BY 4.0,
# rebuilt by tools/corpusx/build_corpus.py). This is what makes X play real
# takes instead of computing notes from probability tables.
juce_add_binary_data(AIDrumCorpus
    HEADER_NAME CorpusData.h
    NAMESPACE   CorpusData
    SOURCES     "${CMAKE_CURRENT_SOURCE_DIR}/content/rock_corpus.hhc")
target_link_libraries(AIDrumVST PRIVATE AIDrumCorpus)

target_sources(AIDrumVST PRIVATE
    SourceX/DrumsXProcessor.cpp
    SourceX/DrumsXProcessor.h
    SourceX/DrumsXEditor.cpp
    SourceX/DrumsXEditor.h
    SourceX/DrumsXLookAndFeel.h
    SourceX/GrooveCorpus.cpp
    SourceX/GrooveCorpus.h
    SourceX/PerformanceEngine.cpp
    SourceX/PerformanceEngine.h
    SourceX/KitEngine.cpp
    SourceX/KitEngine.h
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

# --- Engine tests ------------------------------------------------------------
# The corpus and performance engine are deliberately JUCE-free so the
# determinism / fill-variation guarantees can be tested as a plain executable.
enable_testing()
add_executable(HHDXEngineTests
    tests/EngineTests.cpp
    SourceX/GrooveCorpus.cpp
    SourceX/PerformanceEngine.cpp)
target_compile_features(HHDXEngineTests PRIVATE cxx_std_20)
add_test(NAME HHDXEngineTests
         COMMAND HHDXEngineTests "${CMAKE_CURRENT_SOURCE_DIR}/content/rock_corpus.hhc"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_renders.txt")

# --- Processor tests ---------------------------------------------------------
# Edit preservation, the CPU budget and project-state round-trip need the real
# processor, so this one links JUCE and the bundled content.
juce_add_console_app(HHDXProcessorTests PRODUCT_NAME "HHDXProcessorTests")
juce_generate_juce_header(HHDXProcessorTests)
target_sources(HHDXProcessorTests PRIVATE
    tests/ProcessorTests.cpp
    SourceX/DrumsXProcessor.cpp
    SourceX/DrumsXEditor.cpp
    SourceX/GrooveCorpus.cpp
    SourceX/PerformanceEngine.cpp
    SourceX/KitEngine.cpp)
target_compile_definitions(HHDXProcessorTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="HumHouse Drums X"
    AIDRUM_HAS_BUNDLED_KIT=$<BOOL:${AIDRUM_DEFAULT_KIT_FILES}>
    AIDRUM_HAS_BRANDING=0)
target_link_libraries(HHDXProcessorTests PRIVATE
    AIDrumCorpus
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(AIDRUM_DEFAULT_KIT_FILES)
    target_link_libraries(HHDXProcessorTests PRIVATE AIDrumBundledKit)
endif()
add_test(NAME HHDXProcessorTests COMMAND HHDXProcessorTests)
