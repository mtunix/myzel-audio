# Catch2 and include(Catch) come from the root CMakeLists.txt.

# Non-plugin JUCE executables: no JucePlugin_* defines exist here, which keeps
# the processor/DSP code honest about not depending on them.
set(VIRIDIAN_TEST_DEFINITIONS JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0)

# Whole-plugin tests: tests/*.cpp -------------------------------------------
file(GLOB VIRIDIAN_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(ViridianTests ${VIRIDIAN_TEST_SOURCES})
target_include_directories(ViridianTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ViridianTests PRIVATE ViridianShared Catch2::Catch2WithMain)
target_compile_definitions(ViridianTests PRIVATE
    ${VIRIDIAN_TEST_DEFINITIONS}
    VIRIDIAN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(ViridianTests)

# Per-model tests: tests/models/<m>/*.cpp --------------------------------------
# Each model builds in isolation (its own sources + src/dsp/common only), so a
# model under construction can never break another model's build.
file(GLOB_RECURSE VIRIDIAN_DSP_COMMON_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/common/*.cpp)

foreach(model passive inductor proportional console mastering)
    file(GLOB_RECURSE model_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/models/${model}/*.cpp)
    file(GLOB_RECURSE model_sources CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/models/${model}/*.cpp)

    if(NOT model_tests)
        continue()
    endif()

    set(target ViridianTests_${model})
    add_executable(${target} ${model_tests} ${model_sources} ${VIRIDIAN_DSP_COMMON_SOURCES})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE
        ViridianJuceConfig
        juce::juce_dsp
        Catch2::Catch2WithMain)
    target_compile_definitions(${target} PRIVATE ${VIRIDIAN_TEST_DEFINITIONS})
    catch_discover_tests(${target} TEST_PREFIX "${model}: ")
endforeach()
