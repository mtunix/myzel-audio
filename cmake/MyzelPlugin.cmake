# myzel_add_plugin(<Name>
#     CODE <four characters>              # PLUGIN_CODE: never change it, hosts identify the plugin by it
#     VST3_CATEGORIES <categories...>
#     CLAP_FEATURES <features...>
#     [DSP_SOURCES <files...>]            # default: every src/dsp/**/*.cpp
#     [DSP_LIBRARIES <targets...>]
#     [DSP_DEFINITIONS <definitions...>]
#     [GENERIC_EDITOR])                   # JUCE's generic editor: no kit UI (it needs the plugin's ui/Theme.cpp)
#
# Called from a plugin's CMakeLists.txt after its project(), which sets the name and version.
# Creates, for a plugin in plugins/<name>/:
#   <Name>JuceConfig  compile settings, with <NAME>_VERSION_STRING
#   <Name>Dsp         src/dsp (no GUI)
#   <Name>Shared      processor + DSP + UI (src/plugin, src/ui, the common/ kit), for the plugin,
#                     its tests and its tools
#   <Name>            the plugin: VST3, LV2, CLAP and Standalone, vendor IDs from the root CMakeLists.txt
# and adds tools/ and tests/ when MYZEL_BUILD_TOOLS / MYZEL_BUILD_TESTS ask for them.
function(myzel_add_plugin name)
    cmake_parse_arguments (ARG "GENERIC_EDITOR" "CODE" "VST3_CATEGORIES;CLAP_FEATURES;DSP_SOURCES;DSP_LIBRARIES;DSP_DEFINITIONS" ${ARGN})
    string(TOUPPER "${name}" upper)
    string(TOLOWER "${name}" lower)
    set(dir ${CMAKE_CURRENT_SOURCE_DIR})

    add_library(${name}JuceConfig INTERFACE)
    target_compile_definitions(${name}JuceConfig INTERFACE ${upper}_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(${name}JuceConfig INTERFACE MyzelJuceConfig)

    # DSP (no GUI)
    if(NOT ARG_DSP_SOURCES)
        file(GLOB_RECURSE ARG_DSP_SOURCES CONFIGURE_DEPENDS ${dir}/src/dsp/*.cpp)
    endif()

    add_library(${name}Dsp INTERFACE)
    target_sources(${name}Dsp INTERFACE ${ARG_DSP_SOURCES})
    target_include_directories(${name}Dsp INTERFACE ${dir}/src)
    target_link_libraries(${name}Dsp INTERFACE ${name}JuceConfig MyzelDsp juce::juce_dsp ${ARG_DSP_LIBRARIES})
    target_compile_definitions(${name}Dsp INTERFACE ${ARG_DSP_DEFINITIONS})

    # Processor + DSP + UI
    file(GLOB_RECURSE ui_sources CONFIGURE_DEPENDS ${dir}/src/ui/*.cpp)
    file(GLOB plugin_sources CONFIGURE_DEPENDS ${dir}/src/plugin/*.cpp)
    list(REMOVE_ITEM plugin_sources ${dir}/src/plugin/PluginEntry.cpp)   # plugin targets only

    add_library(${name}Shared INTERFACE)
    target_sources(${name}Shared INTERFACE ${plugin_sources} ${ui_sources})
    if(ARG_GENERIC_EDITOR)
        target_link_libraries(${name}Shared INTERFACE ${name}Dsp MyzelPlugin juce::juce_audio_utils)
    else()
        target_link_libraries(${name}Shared INTERFACE ${name}Dsp MyzelPlugin MyzelUi juce::juce_audio_utils melatonin_blur)
    endif()

    # The plugin
    juce_add_plugin(${name}
        PRODUCT_NAME "${name}"
        COMPANY_NAME "${MYZEL_COMPANY_NAME}"
        BUNDLE_ID "${MYZEL_ID_PREFIX}.${lower}"
        PLUGIN_MANUFACTURER_CODE ${MYZEL_MANUFACTURER_CODE}
        PLUGIN_CODE ${ARG_CODE}
        FORMATS VST3 LV2 Standalone
        LV2URI "${MYZEL_LV2_PREFIX}:${lower}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES ${ARG_VST3_CATEGORIES}
        COPY_PLUGIN_AFTER_BUILD ${MYZEL_COPY_PLUGINS})

    target_sources(${name} PRIVATE ${dir}/src/plugin/PluginEntry.cpp)
    target_link_libraries(${name} PRIVATE ${name}Shared)

    clap_juce_extensions_plugin(TARGET ${name}
        CLAP_ID "${MYZEL_ID_PREFIX}.${lower}"
        CLAP_FEATURES ${ARG_CLAP_FEATURES}
        CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 64)

    if(MYZEL_COPY_PLUGINS)
        add_custom_command(TARGET ${name}_CLAP POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.clap"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${name}_CLAP>" "$ENV{HOME}/.clap/"
            VERBATIM)
    endif()

    # Tools and tests may compile the DSP sources on their own (e.g. a measurement tool).
    set(${upper}_DSP_SOURCES ${ARG_DSP_SOURCES})

    if(MYZEL_BUILD_TOOLS AND EXISTS ${dir}/tools/CMakeLists.txt)
        add_subdirectory(${dir}/tools ${CMAKE_CURRENT_BINARY_DIR}/tools)
    endif()

    if(MYZEL_BUILD_TESTS AND EXISTS ${dir}/tests/CMakeLists.txt)
        add_subdirectory(${dir}/tests ${CMAKE_CURRENT_BINARY_DIR}/tests)
    endif()
endfunction()
