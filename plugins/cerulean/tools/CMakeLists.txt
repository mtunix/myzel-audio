# Developer tools. Each tool is only built when its sources exist.

# Measurement: renders every algorithm and prints RT60 per band, echo density, level, CPU.
# Pure DSP (no JUCE modules).
file(GLOB CERULEAN_MEASURE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/measure/*.cpp)
if(CERULEAN_MEASURE_SOURCES)
    add_executable(CeruleanMeasure ${CERULEAN_MEASURE_SOURCES} ${CERULEAN_DSP_SOURCES})
    if(CERULEAN_ONLY_HALL)
        target_compile_definitions(CeruleanMeasure PRIVATE CERULEAN_ONLY_HALL=1)
    endif()
    target_include_directories(CeruleanMeasure PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(CeruleanMeasure PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

# UI snapshot: renders the editor to PNG files (headless-friendly, use with xvfb-run).
file(GLOB CERULEAN_UI_SNAPSHOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui_snapshot/*.cpp)
if(CERULEAN_UI_SNAPSHOT_SOURCES)
    juce_add_console_app(CeruleanUiSnapshot PRODUCT_NAME "CeruleanUiSnapshot")
    target_sources(CeruleanUiSnapshot PRIVATE ${CERULEAN_UI_SNAPSHOT_SOURCES})
    target_link_libraries(CeruleanUiSnapshot PRIVATE CeruleanShared)
    # Modal loops: the self-test pumps the message loop to check asynchronous view switching.
    target_compile_definitions(CeruleanUiSnapshot PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
endif()
