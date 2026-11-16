# Developer tools. Each tool is only built when its sources exist.

# Measurement: the plugin's own detector (dsp::probe) over the labelled corpora, the detection gates
# and sweeps; with --reduction the chain in every shape and the reduction gates (PLAN.md, Measurement
# and quality gates). The DSP only (no plugin, no UI); the gate code is shared with the corpus tests
# (tests/support).
file(GLOB HALITE_MEASURE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/measure/*.cpp)
if(HALITE_MEASURE_SOURCES)
    add_executable(HaliteMeasure ${HALITE_MEASURE_SOURCES}
        ${PROJECT_SOURCE_DIR}/tests/support/Corpus.cpp
        ${PROJECT_SOURCE_DIR}/tests/support/Gates.cpp)
    target_include_directories(HaliteMeasure PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR})
    target_link_libraries(HaliteMeasure PRIVATE HaliteDsp)
    target_compile_definitions(HaliteMeasure PRIVATE
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0
        HALITE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests")
endif()

# UI snapshot: renders the editor to PNG files and runs the editor self-test (headless with xvfb-run).
file(GLOB HALITE_UI_SNAPSHOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui_snapshot/*.cpp)
if(HALITE_UI_SNAPSHOT_SOURCES)
    juce_add_console_app(HaliteUiSnapshot PRODUCT_NAME "HaliteUiSnapshot")
    target_sources(HaliteUiSnapshot PRIVATE ${HALITE_UI_SNAPSHOT_SOURCES})
    target_link_libraries(HaliteUiSnapshot PRIVATE HaliteShared)
    # Modal loops: the self-test pumps the message loop for asynchronous work (menus, dialogs).
    target_compile_definitions(HaliteUiSnapshot PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
endif()
