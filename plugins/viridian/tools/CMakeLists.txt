# Developer tools. Each tool is only built when its sources exist.

# Measurement: renders every built model through the real chain and checks the quality gates;
# compare.py checks its curves against ngspice. The DSP only (no plugin, no UI).
file(GLOB VIRIDIAN_MEASURE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/measure/*.cpp)
if(VIRIDIAN_MEASURE_SOURCES)
    add_executable(ViridianMeasure ${VIRIDIAN_MEASURE_SOURCES})
    target_include_directories(ViridianMeasure PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR})
    target_link_libraries(ViridianMeasure PRIVATE ViridianDsp)
    target_compile_definitions(ViridianMeasure PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=0)
endif()

# The Console through the chain against ngspice (tools/measure/compare.py), where python3 with
# numpy and scipy and ngspice exist: gates the magnitude and prints the phase per rate (PLAN.md
# Settled 13: phase is reported, not gated).
if(TARGET ViridianMeasure)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    find_program(VIRIDIAN_NGSPICE ngspice)
    if(Python3_Interpreter_FOUND AND VIRIDIAN_NGSPICE)
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import numpy, scipy"
                        RESULT_VARIABLE viridian_measure_modules OUTPUT_QUIET ERROR_QUIET)
        if(viridian_measure_modules EQUAL 0)
            add_test(NAME "console: the chain against ngspice (tools/measure)"
                     COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/measure/compare.py
                             --measure $<TARGET_FILE:ViridianMeasure> --keep ${CMAKE_CURRENT_BINARY_DIR}/console-curves.jsonl)
        endif()
    endif()
    if(NOT TEST "console: the chain against ngspice (tools/measure)")
        message(STATUS "Viridian: the ngspice comparison is not registered (needs python3 with numpy and scipy, and ngspice)")
    endif()
endif()
