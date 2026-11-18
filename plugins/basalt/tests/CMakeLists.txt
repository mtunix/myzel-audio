# Catch2 and include(Catch) come from the root CMakeLists.txt.

# Non-plugin JUCE executables: no JucePlugin_* defines exist here, which keeps
# the processor/DSP code honest about not depending on them.
set(BASALT_TEST_DEFINITIONS JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0)

# Whole-plugin tests: tests/*.cpp. The gate measurements (support/*.cpp) are shared with tools/measure.
file(GLOB BASALT_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB BASALT_SUPPORT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp)

add_executable(BasaltTests ${BASALT_TEST_SOURCES} ${BASALT_SUPPORT_SOURCES})
target_include_directories(BasaltTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(BasaltTests PRIVATE BasaltShared Catch2::Catch2WithMain)
target_compile_definitions(BasaltTests PRIVATE
    ${BASALT_TEST_DEFINITIONS}
    BASALT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(BasaltTests)

# Dynamics tests: tests/dynamics/*.cpp. The core and the models build on their own (src/dsp/dynamics,
# src/dsp/models), without JUCE's processor, so their loop never compiles the plugin.
file(GLOB BASALT_DYNAMICS_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dynamics/*.cpp)
file(GLOB_RECURSE BASALT_DYNAMICS_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/dsp/dynamics/*.cpp
    ${PROJECT_SOURCE_DIR}/src/dsp/models/*.cpp)

add_executable(BasaltTests_dynamics ${BASALT_DYNAMICS_TESTS} ${BASALT_DYNAMICS_SOURCES})
target_include_directories(BasaltTests_dynamics PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(BasaltTests_dynamics PRIVATE BasaltJuceConfig Catch2::Catch2WithMain)
catch_discover_tests(BasaltTests_dynamics TEST_PREFIX "dynamics: ")

# Per-model tests: tests/models/<m>/*.cpp against that model's sources and the core only, so each
# model builds and tests on its own. A model's target appears once its test folder has sources
# (reconfigure after adding the first file).
file(GLOB BASALT_CORE_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/dynamics/*.cpp)
foreach (model vca fet opto varimu upward)
    file(GLOB model_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/models/${model}/*.cpp)
    if (model_tests)
        file(GLOB model_sources CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/dsp/models/${model}/*.cpp)
        add_executable(BasaltTests_${model} ${model_tests} ${model_sources} ${BASALT_CORE_SOURCES})
        target_include_directories(BasaltTests_${model} PRIVATE
            ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
        target_link_libraries(BasaltTests_${model} PRIVATE BasaltJuceConfig Catch2::Catch2WithMain)
        catch_discover_tests(BasaltTests_${model} TEST_PREFIX "${model}: ")
    endif()
endforeach()
