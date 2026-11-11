# Developer tools. Each tool is only built when its sources exist.

# UI snapshot: renders the editor to PNG files (headless-friendly, use with xvfb-run).
file(GLOB UMBER_UI_SNAPSHOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui_snapshot/*.cpp)
if(UMBER_UI_SNAPSHOT_SOURCES)
    juce_add_console_app(UmberUiSnapshot PRODUCT_NAME "UmberUiSnapshot")
    target_sources(UmberUiSnapshot PRIVATE ${UMBER_UI_SNAPSHOT_SOURCES})
    target_link_libraries(UmberUiSnapshot PRIVATE UmberShared)
    target_compile_definitions(UmberUiSnapshot PRIVATE JUCE_STANDALONE_APPLICATION=1)
endif()

# Calibration: measures every model through the full chain and writes the Auto gain tables.
file(GLOB UMBER_CALIBRATE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/calibrate/*.cpp)
if(UMBER_CALIBRATE_SOURCES)
    juce_add_console_app(UmberCalibrate PRODUCT_NAME "UmberCalibrate")
    target_sources(UmberCalibrate PRIVATE ${UMBER_CALIBRATE_SOURCES})
    target_link_libraries(UmberCalibrate PRIVATE UmberShared)
    target_compile_definitions(UmberCalibrate PRIVATE JUCE_STANDALONE_APPLICATION=1)
endif()
