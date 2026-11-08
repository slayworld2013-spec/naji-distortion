cmake_minimum_required(VERSION 3.22)

project(NAJI_DISTORTION VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the plugin works on any Windows PC without installing
# the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# JUCE
#   - Pass -DNAJI_JUCE_PATH=/path/to/JUCE to use a local copy, or
#   - leave it empty and CMake downloads JUCE 8.0.9 automatically.
# ---------------------------------------------------------------------------
set(NAJI_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(NAJI_BUILD_TESTS "Build the offline test host" OFF)

if(NAJI_JUCE_PATH)
    add_subdirectory(${NAJI_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(NajiDistortion
    COMPANY_NAME              "Naji Audio"
    COMPANY_WEBSITE           ""
    PLUGIN_MANUFACTURER_CODE  Naji
    PLUGIN_CODE               NjDs
    FORMATS                   VST3
    PRODUCT_NAME              "NAJI DISTORTION"
    BUNDLE_ID                 com.najiaudio.najidistortion
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx Distortion
    VST3_AUTO_MANIFEST        TRUE)

juce_add_binary_data(NajiDistortionAssets
    SOURCES
        Assets/Photos/photo1.jpg
        Assets/Photos/photo2.jpg
        Assets/Photos/photo3.jpg
        Assets/Photos/photo4.jpg
        Assets/Photos/photo5.jpg
        Assets/Fonts/Inter-Bold.otf
        Assets/Fonts/Inter-Medium.otf)

set(NAJI_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp
    Source/ui/Widgets.cpp
    Source/ui/PhotoStage.cpp)

target_sources(NajiDistortion PRIVATE ${NAJI_SOURCES})

target_include_directories(NajiDistortion PRIVATE Source)

target_compile_definitions(NajiDistortion PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(UNIX AND NOT APPLE)
    # Only needed so the plugin also builds on minimal Linux machines.
    target_compile_definitions(NajiDistortion PUBLIC
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0)
endif()

target_link_libraries(NajiDistortion
    PRIVATE
        NajiDistortionAssets
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test host (loads the built .vst3 and runs audio/automation tests)
# ---------------------------------------------------------------------------
if(NAJI_BUILD_TESTS)
    juce_add_console_app(NajiTestHost PRODUCT_NAME "NajiTestHost")
    # The plugin sources are compiled in too, so the editor can be rendered to a PNG.
    target_sources(NajiTestHost PRIVATE Tests/TestHost.cpp ${NAJI_SOURCES})
    target_include_directories(NajiTestHost PRIVATE Source)
    target_compile_definitions(NajiTestHost PRIVATE
        JucePlugin_Name="NAJI DISTORTION" JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0)
    target_link_libraries(NajiTestHost PRIVATE
        NajiDistortionAssets
        juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
