# Catch2 and include(Catch) come from the root CMakeLists.txt.
#
# Every source list here is a glob: a new test or source file inside a module's own directories
# (src/dsp/modules/<m>/, tests/modules/<m>/) needs no edit to this file. A module that needs more
# (a definition, an extra library) puts it in tests/modules/<m>/module.cmake, included below with
# the target in `target`.

# Non-plugin JUCE executables: no JucePlugin_* defines exist here, which keeps
# the processor/DSP code honest about not depending on them.
set(INKCAP_TEST_DEFINITIONS JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0)

set(INKCAP_JUCE_FREE_LIBRARIES
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2WithMain)

# Whole-plugin tests: tests/*.cpp -------------------------------------------
file(GLOB INKCAP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(InkcapTests ${INKCAP_TEST_SOURCES})
target_include_directories(InkcapTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(InkcapTests PRIVATE InkcapShared Catch2::Catch2WithMain)
target_compile_definitions(InkcapTests PRIVATE
    ${INKCAP_TEST_DEFINITIONS}
    INKCAP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(InkcapTests)

# Seeds, the clock, the capture buffer: tests/common/*.cpp ----------------------
# src/dsp/common only, no JUCE: the frozen hash builds and runs in seconds.
file(GLOB_RECURSE INKCAP_DSP_COMMON_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/common/*.cpp)
file(GLOB INKCAP_COMMON_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp)

add_executable(InkcapTests_common ${INKCAP_COMMON_TESTS} ${INKCAP_DSP_COMMON_SOURCES})
target_include_directories(InkcapTests_common PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(InkcapTests_common PRIVATE ${INKCAP_JUCE_FREE_LIBRARIES})
catch_discover_tests(InkcapTests_common TEST_PREFIX "common: ")

# The chain and its gates: tests/chain/*.cpp ----------------------------------
# Every DSP source, no JUCE: the null and determinism gates through the real signal path.
file(GLOB INKCAP_CHAIN_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/chain/*.cpp)

add_executable(InkcapTests_chain ${INKCAP_CHAIN_TESTS} ${INKCAP_DSP_SOURCES})
target_include_directories(InkcapTests_chain PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(InkcapTests_chain PRIVATE ${INKCAP_JUCE_FREE_LIBRARIES})
target_compile_definitions(InkcapTests_chain PRIVATE INKCAP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(InkcapTests_chain TEST_PREFIX "chain: ")

# Per-module tests: tests/modules/<m>/*.cpp ------------------------------------
# Each module builds in isolation (its own sources and src/dsp/common, no JUCE, no other
# module), so a module under construction never breaks another module's build or tests.
foreach(module glitch warble crush bits hiss room)
    file(GLOB_RECURSE module_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/modules/${module}/*.cpp)
    file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/modules/${module}/*.cpp)

    if(NOT module_tests)
        continue()
    endif()

    set(target InkcapTests_${module})
    add_executable(${target} ${module_tests} ${module_sources} ${INKCAP_DSP_COMMON_SOURCES})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE ${INKCAP_JUCE_FREE_LIBRARIES})
    target_compile_definitions(${target} PRIVATE INKCAP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    include(${CMAKE_CURRENT_SOURCE_DIR}/modules/${module}/module.cmake OPTIONAL)
    catch_discover_tests(${target} TEST_PREFIX "${module}: ")
endforeach()
