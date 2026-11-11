# Catch2 and include(Catch) come from the root CMakeLists.txt.

# Non-plugin JUCE executables: no JucePlugin_* defines exist here, which keeps
# the processor/DSP code honest about not depending on them.
set(CERULEAN_TEST_DEFINITIONS JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0)

# Whole-plugin tests: tests/*.cpp -------------------------------------------
file(GLOB CERULEAN_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(CeruleanTests ${CERULEAN_TEST_SOURCES})
target_include_directories(CeruleanTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(CeruleanTests PRIVATE CeruleanShared Catch2::Catch2WithMain)
target_compile_definitions(CeruleanTests PRIVATE
    ${CERULEAN_TEST_DEFINITIONS}
    CERULEAN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(CeruleanTests)

# Per-algorithm tests: tests/algorithms/<a>/*.cpp -------------------------------
# Each algorithm builds in isolation (its own sources + src/dsp/common + Hall, which
# others may reuse as a tank), with no JUCE at all: fast to build, and an algorithm
# under construction can never break another algorithm's build.
file(GLOB_RECURSE CERULEAN_DSP_COMMON_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/common/*.cpp)
file(GLOB_RECURSE CERULEAN_HALL_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/algorithms/hall/*.cpp)

foreach(algorithm room hall plate spring nimbus halo rise billow choir echo)
    file(GLOB_RECURSE algorithm_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/algorithms/${algorithm}/*.cpp)
    file(GLOB_RECURSE algorithm_sources CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/algorithms/${algorithm}/*.cpp)

    if(NOT algorithm_tests)
        continue()
    endif()

    if(NOT algorithm STREQUAL "hall")
        list(APPEND algorithm_sources ${CERULEAN_HALL_SOURCES})
    endif()

    set(target CeruleanTests_${algorithm})
    add_executable(${target} ${algorithm_tests} ${algorithm_sources} ${CERULEAN_DSP_COMMON_SOURCES})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Catch2::Catch2WithMain)
    catch_discover_tests(${target} TEST_PREFIX "${algorithm}: ")
endforeach()
