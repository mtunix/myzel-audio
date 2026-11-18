# Developer tools. Each tool is only built when its sources exist.

# Measurement: the plugin's processor, its controls set through the parameters, against the gates of
# PLAN.md (Measurement and quality gates) and the NOTES' rows. The measurements (tests/support) are
# shared with tests/GateTests.cpp.
file(GLOB BASALT_MEASURE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/measure/*.cpp)
if(BASALT_MEASURE_SOURCES)
    file(GLOB BASALT_GATE_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/support/*.cpp)
    add_executable(BasaltMeasure ${BASALT_MEASURE_SOURCES} ${BASALT_GATE_SOURCES})
    target_include_directories(BasaltMeasure PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_link_libraries(BasaltMeasure PRIVATE BasaltShared)
    target_compile_definitions(BasaltMeasure PRIVATE
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0
        BASALT_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests")
endif()
