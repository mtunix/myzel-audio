# Code shared by every plugin. All targets are source-only (INTERFACE): each plugin compiles
# them with its own JUCE configuration, the same way it compiles its own sources, so JUCE is
# never linked twice. Includes read "common/<area>/<File>.h".

add_library(MyzelCommon INTERFACE)
target_include_directories(MyzelCommon INTERFACE ${PROJECT_SOURCE_DIR})
target_link_libraries(MyzelCommon INTERFACE MyzelJuceConfig)

# DSP building blocks (header-only, no JUCE).
add_library(MyzelDsp INTERFACE)
target_link_libraries(MyzelDsp INTERFACE MyzelCommon)

# Plugin infrastructure: undo, presets, A/B.
file(GLOB MYZEL_PLUGIN_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.cpp)
add_library(MyzelPlugin INTERFACE)
target_sources(MyzelPlugin INTERFACE ${MYZEL_PLUGIN_SOURCES})
target_link_libraries(MyzelPlugin INTERFACE MyzelCommon juce::juce_audio_processors)

# The bundled Inter typeface (SIL OFL 1.1, see ui/assets/fonts/OFL.txt). Everything else in
# the UI is drawn procedurally.
juce_add_binary_data(MyzelUiAssets
    HEADER_NAME MyzelAssets.h
    NAMESPACE MyzelAssets
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/fonts/Inter-Medium.otf
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/fonts/Inter-SemiBold.otf
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/fonts/InterDisplay-Black.otf)
set_target_properties(MyzelUiAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
