# Developer tools. Each tool is only built when its sources exist.

# Measurement: renders every machine through the real chain and checks the quality gates.
# Pure DSP (no JUCE modules).
file(GLOB MULBERRY_MEASURE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/measure/*.cpp)
if(MULBERRY_MEASURE_SOURCES)
    add_executable(MulberryMeasure ${MULBERRY_MEASURE_SOURCES} ${MULBERRY_DSP_SOURCES})
    target_include_directories(MulberryMeasure PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR})
    target_link_libraries(MulberryMeasure PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

# UI snapshot: renders the editor to PNG files (headless-friendly, use with xvfb-run).
file(GLOB MULBERRY_UI_SNAPSHOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui_snapshot/*.cpp)
if(MULBERRY_UI_SNAPSHOT_SOURCES)
    juce_add_console_app(MulberryUiSnapshot PRODUCT_NAME "MulberryUiSnapshot")
    target_sources(MulberryUiSnapshot PRIVATE ${MULBERRY_UI_SNAPSHOT_SOURCES})
    target_link_libraries(MulberryUiSnapshot PRIVATE MulberryShared)
    # Modal loops: the self-test pumps the message loop to check asynchronous view switching.
    target_compile_definitions(MulberryUiSnapshot PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
endif()
