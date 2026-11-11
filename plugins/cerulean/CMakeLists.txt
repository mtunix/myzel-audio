# Cerulean: built from the repository root (see ../../CMakeLists.txt), which sets up
# the dependencies, the shared compile settings (MyzelJuceConfig) and the MYZEL_* options.
project(Cerulean VERSION 0.1.0 LANGUAGES C CXX)

option(CERULEAN_ONLY_HALL "Build every algorithm as Hall (UI work while algorithms are in flux)" OFF)

# Common compile settings -----------------------------------------------------
add_library(CeruleanJuceConfig INTERFACE)
target_compile_definitions(CeruleanJuceConfig INTERFACE CERULEAN_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(CeruleanJuceConfig INTERFACE MyzelJuceConfig)

# DSP (no GUI) ----------------------------------------------------------------
# src/dsp/common          shared reverb building blocks (header-only where possible)
# src/dsp/algorithms/<a>  one directory per algorithm (engine)
# src/dsp/chain           the full signal chain and the engine factory
file(GLOB_RECURSE CERULEAN_DSP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp)

if(CERULEAN_ONLY_HALL)
    list(FILTER CERULEAN_DSP_SOURCES EXCLUDE REGEX "/src/dsp/algorithms/(room|plate|spring|nimbus|halo|rise|billow|choir|echo)/")
endif()

add_library(CeruleanDsp INTERFACE)
target_sources(CeruleanDsp INTERFACE ${CERULEAN_DSP_SOURCES})
target_include_directories(CeruleanDsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(CeruleanDsp INTERFACE
    CeruleanJuceConfig
    juce::juce_dsp)

if(CERULEAN_ONLY_HALL)
    target_compile_definitions(CeruleanDsp INTERFACE CERULEAN_ONLY_HALL=1)
endif()

# Shared plugin code (processor + DSP + UI) ------------------------------------
# UI-owned hook: src/ui/ui.cmake may define a CeruleanUiAssets target
# (e.g. juce_add_binary_data for fonts), which is linked automatically.
include(${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ui.cmake OPTIONAL)

file(GLOB_RECURSE CERULEAN_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)

add_library(CeruleanShared INTERFACE)
target_sources(CeruleanShared INTERFACE
    src/plugin/Parameters.cpp
    src/plugin/ParameterUndo.cpp
    src/plugin/PluginProcessor.cpp
    ${CERULEAN_UI_SOURCES})
target_link_libraries(CeruleanShared INTERFACE
    CeruleanDsp
    juce::juce_audio_utils
    melatonin_blur)

if(TARGET CeruleanUiAssets)
    target_link_libraries(CeruleanShared INTERFACE CeruleanUiAssets)
endif()

# Plugin ----------------------------------------------------------------------
juce_add_plugin(Cerulean
    PRODUCT_NAME "Cerulean"
    COMPANY_NAME "${MYZEL_COMPANY_NAME}"
    BUNDLE_ID "${MYZEL_ID_PREFIX}.cerulean"
    PLUGIN_MANUFACTURER_CODE ${MYZEL_MANUFACTURER_CODE}
    PLUGIN_CODE Crln
    FORMATS VST3 LV2 Standalone
    LV2URI "${MYZEL_LV2_PREFIX}:cerulean"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Reverb
    COPY_PLUGIN_AFTER_BUILD ${MYZEL_COPY_PLUGINS})

target_sources(Cerulean PRIVATE src/plugin/PluginEntry.cpp)
target_link_libraries(Cerulean PRIVATE CeruleanShared)

clap_juce_extensions_plugin(TARGET Cerulean
    CLAP_ID "${MYZEL_ID_PREFIX}.cerulean"
    CLAP_FEATURES audio-effect reverb stereo mono
    CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 64)

if(MYZEL_COPY_PLUGINS)
    add_custom_command(TARGET Cerulean_CLAP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.clap"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Cerulean_CLAP>" "$ENV{HOME}/.clap/"
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
