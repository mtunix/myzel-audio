# Umber: built from the repository root (see ../../CMakeLists.txt), which sets up
# the dependencies, the shared compile settings (MyzelJuceConfig) and the MYZEL_* options.
project(Umber VERSION 0.1.0 LANGUAGES C CXX)

# Common compile settings -----------------------------------------------------
add_library(UmberJuceConfig INTERFACE)
target_compile_definitions(UmberJuceConfig INTERFACE UMBER_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(UmberJuceConfig INTERFACE MyzelJuceConfig)

# DSP (no GUI) ----------------------------------------------------------------
# src/dsp/common      shared DSP building blocks (header-only where possible)
# src/dsp/models/<m>  one directory per circuit model
# src/dsp/chain       the full signal chain
file(GLOB_RECURSE UMBER_DSP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp)

add_library(UmberDsp INTERFACE)
target_sources(UmberDsp INTERFACE ${UMBER_DSP_SOURCES})
target_include_directories(UmberDsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(UmberDsp INTERFACE
    UmberJuceConfig
    juce::juce_dsp
    chowdsp::chowdsp_wdf)

# Shared plugin code (processor + DSP + UI) ------------------------------------
# UI-owned hook: src/ui/ui.cmake may define a UmberUiAssets target
# (e.g. juce_add_binary_data for fonts), which is linked automatically.
include(${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ui.cmake OPTIONAL)

file(GLOB_RECURSE UMBER_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)

add_library(UmberShared INTERFACE)
target_sources(UmberShared INTERFACE
    src/plugin/Parameters.cpp
    src/plugin/ParameterUndo.cpp
    src/plugin/PluginProcessor.cpp
    ${UMBER_UI_SOURCES})
target_link_libraries(UmberShared INTERFACE
    UmberDsp
    juce::juce_audio_utils
    melatonin_blur)

if(TARGET UmberUiAssets)
    target_link_libraries(UmberShared INTERFACE UmberUiAssets)
endif()

# Plugin ----------------------------------------------------------------------
juce_add_plugin(Umber
    PRODUCT_NAME "Umber"
    COMPANY_NAME "${MYZEL_COMPANY_NAME}"
    BUNDLE_ID "${MYZEL_ID_PREFIX}.umber"
    PLUGIN_MANUFACTURER_CODE ${MYZEL_MANUFACTURER_CODE}
    PLUGIN_CODE Umbr
    FORMATS VST3 LV2 Standalone
    LV2URI "${MYZEL_LV2_PREFIX}:umber"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD ${MYZEL_COPY_PLUGINS})

target_sources(Umber PRIVATE src/plugin/PluginEntry.cpp)
target_link_libraries(Umber PRIVATE UmberShared)

clap_juce_extensions_plugin(TARGET Umber
    CLAP_ID "${MYZEL_ID_PREFIX}.umber"
    CLAP_FEATURES audio-effect distortion stereo mono
    CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 64)

if(MYZEL_COPY_PLUGINS)
    add_custom_command(TARGET Umber_CLAP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.clap"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Umber_CLAP>" "$ENV{HOME}/.clap/"
        VERBATIM)
endif()

# Developer tools -------------------------------------------------------------
if(MYZEL_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Tests -----------------------------------------------------------------------
if(MYZEL_BUILD_TESTS)
    add_subdirectory(tests)
endif()
