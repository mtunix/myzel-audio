# Developer tools. Each tool is only built when its sources exist.

# Measurement: renders through the real chain and checks the quality gates. Pure DSP (no JUCE modules).
file(GLOB INKCAP_MEASURE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/measure/*.cpp)
if(INKCAP_MEASURE_SOURCES)
    add_executable(InkcapMeasure ${INKCAP_MEASURE_SOURCES} ${INKCAP_DSP_SOURCES})
    target_include_directories(InkcapMeasure PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR})
    target_link_libraries(InkcapMeasure PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

# Calibration (M3): Match's tables (src/dsp/chain/MatchTables.h) and the voicing ladder. Pure DSP.
file(GLOB INKCAP_CALIBRATE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/calibrate/*.cpp)
if(INKCAP_CALIBRATE_SOURCES)
    add_executable(InkcapCalibrate ${INKCAP_CALIBRATE_SOURCES} ${INKCAP_DSP_SOURCES})
    target_include_directories(InkcapCalibrate PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests
                               ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
    find_package(Threads REQUIRED)
    target_link_libraries(InkcapCalibrate PRIVATE
        Threads::Threads
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

# UI snapshot: renders the editor to PNG files and runs the editor self-test (headless with xvfb-run).
file(GLOB INKCAP_UI_SNAPSHOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui_snapshot/*.cpp)
if(INKCAP_UI_SNAPSHOT_SOURCES)
    juce_add_console_app(InkcapUiSnapshot PRODUCT_NAME "InkcapUiSnapshot")
    target_sources(InkcapUiSnapshot PRIVATE ${INKCAP_UI_SNAPSHOT_SOURCES})
    target_link_libraries(InkcapUiSnapshot PRIVATE InkcapShared)
    # Modal loops: the self-test pumps the message loop for asynchronous work (menus, dialogs).
    target_compile_definitions(InkcapUiSnapshot PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
endif()
