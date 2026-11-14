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

# The linear-network machinery: tests/network/*.cpp ---------------------------
# MatchedSection, FrequencyMap and Network against the reference circuits that
# tools/netlist generates into tests/network/generated (no JUCE).
file(GLOB VIRIDIAN_NETWORK_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/network/*.cpp)
add_executable(ViridianTests_network ${VIRIDIAN_NETWORK_TESTS} ${VIRIDIAN_DSP_COMMON_SOURCES})
target_include_directories(ViridianTests_network PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(ViridianTests_network PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2WithMain)
catch_discover_tests(ViridianTests_network TEST_PREFIX "network: ")

# tools/netlist against closed forms and ngspice, where python3 (sympy, numpy, mpmath) and
# ngspice are installed; elsewhere the generated headers are checked by the C++ tests alone.
find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(VIRIDIAN_NGSPICE ngspice)
if(Python3_Interpreter_FOUND AND VIRIDIAN_NGSPICE)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import sympy, numpy, mpmath"
                    RESULT_VARIABLE viridian_python_modules OUTPUT_QUIET ERROR_QUIET)
    if(viridian_python_modules EQUAL 0)
        add_test(NAME "network: tools/netlist reference circuits"
                 COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/netlist/validate.py)
    endif()
endif()
if(NOT TEST "network: tools/netlist reference circuits")
    message(STATUS "Viridian: tools/netlist validation not registered (needs python3 with sympy, numpy, mpmath, and ngspice)")
endif()

# Every generated header's stamp against its netlist and the files it includes: a header older
# than its netlist fails here wherever python3 runs, without sympy or ngspice.
if(Python3_Interpreter_FOUND)
    add_test(NAME "network: generated headers match their netlists"
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/netlist/sources.py)
endif()

# Regenerates every header a netlist names with '*@ generate' (checked in; not part of the build).
if(Python3_Interpreter_FOUND)
    add_custom_target(ViridianNetworks
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/netlist/netlist.py regenerate
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Regenerating Viridian's network headers from their netlists"
        VERBATIM)
endif()
