# Catch2 and include(Catch) come from the root CMakeLists.txt.

# Non-plugin JUCE executables: no JucePlugin_* defines exist here, which keeps
# the processor/DSP code honest about not depending on them.
set(HALITE_TEST_DEFINITIONS JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0)

# Whole-plugin tests: tests/*.cpp
file(GLOB HALITE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(HaliteTests ${HALITE_TEST_SOURCES})
target_include_directories(HaliteTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(HaliteTests PRIVATE HaliteShared Catch2::Catch2WithMain)
target_compile_definitions(HaliteTests PRIVATE
    ${HALITE_TEST_DEFINITIONS}
    HALITE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(HaliteTests)

# Detector tests: tests/detector/*.cpp. The detector builds on its own (src/dsp/detector and
# src/dsp/common), so its loop never compiles the processor.
file(GLOB HALITE_DETECTOR_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/detector/*.cpp)
file(GLOB HALITE_DETECTOR_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/dsp/detector/*.cpp
    ${PROJECT_SOURCE_DIR}/src/dsp/common/*.cpp)

# The corpus gates (support/Corpus, support/Gates) are shared with tools/measure.
add_executable(HaliteTests_detector ${HALITE_DETECTOR_TESTS} ${HALITE_DETECTOR_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/support/Corpus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/support/Gates.cpp)
target_include_directories(HaliteTests_detector PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(HaliteTests_detector PRIVATE HaliteJuceConfig juce::juce_dsp Catch2::Catch2WithMain)
target_compile_definitions(HaliteTests_detector PRIVATE
    ${HALITE_TEST_DEFINITIONS}
    HALITE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(HaliteTests_detector TEST_PREFIX "detector: ")

# Reduction tests: tests/reduction/*.cpp. The signal path without the plugin (src/dsp: detector,
# shapes, chain), so their loop never compiles the processor. The corpus reader feeds the golden
# renders (the committed excerpts, tests/golden) and Spectral's hidden corpus report.
file(GLOB HALITE_REDUCTION_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/reduction/*.cpp)
add_executable(HaliteTests_reduction ${HALITE_REDUCTION_TESTS} ${CMAKE_CURRENT_SOURCE_DIR}/support/Corpus.cpp)
target_include_directories(HaliteTests_reduction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(HaliteTests_reduction PRIVATE HaliteDsp Catch2::Catch2WithMain)
target_compile_definitions(HaliteTests_reduction PRIVATE
    ${HALITE_TEST_DEFINITIONS}
    HALITE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(HaliteTests_reduction TEST_PREFIX "reduction: ")
