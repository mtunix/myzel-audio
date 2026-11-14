# Catch2 and include(Catch) come from the root CMakeLists.txt.

# Non-plugin JUCE executables: no JucePlugin_* defines exist here, which keeps
# the processor/DSP code honest about not depending on them.
set(MULBERRY_TEST_DEFINITIONS JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0)

# Whole-plugin tests: tests/*.cpp -------------------------------------------
file(GLOB MULBERRY_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(MulberryTests ${MULBERRY_TEST_SOURCES})
target_include_directories(MulberryTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MulberryTests PRIVATE MulberryShared Catch2::Catch2WithMain)
target_compile_definitions(MulberryTests PRIVATE
    ${MULBERRY_TEST_DEFINITIONS}
    MULBERRY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(MulberryTests)

# Per-machine tests: tests/machines/<m>/*.cpp ----------------------------------
# Each machine's tests build with Mulberry's DSP only (no JUCE at all), so they build and run
# fast and can be run on their own while a machine is voiced.
file(GLOB_RECURSE MULBERRY_DSP_FRAME_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/dsp/common/*.cpp
    ${PROJECT_SOURCE_DIR}/src/dsp/chain/*.cpp
    ${PROJECT_SOURCE_DIR}/src/dsp/*.cpp)
file(GLOB_RECURSE MULBERRY_MACHINE_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/machines/*.cpp)

foreach(machine clean rack bucket oilcan loop reel drum)
    file(GLOB_RECURSE machine_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/machines/${machine}/*.cpp)
    if(NOT machine_tests)
        continue()
    endif()

    set(target MulberryTests_${machine})
    add_executable(${target} ${machine_tests} ${MULBERRY_DSP_FRAME_SOURCES} ${MULBERRY_MACHINE_SOURCES})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Catch2::Catch2WithMain)
    target_compile_definitions(${target} PRIVATE MULBERRY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    catch_discover_tests(${target} TEST_PREFIX "${machine}: ")
endforeach()
