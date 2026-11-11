# UI assets: the bundled Inter typeface (SIL OFL 1.1, see assets/fonts/OFL.txt).
# Everything else in the UI is drawn procedurally.
juce_add_binary_data(UmberUiAssets
    HEADER_NAME UmberAssets.h
    NAMESPACE UmberAssets
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/assets/fonts/Inter-Medium.otf
        ${CMAKE_CURRENT_LIST_DIR}/assets/fonts/Inter-SemiBold.otf
        ${CMAKE_CURRENT_LIST_DIR}/assets/fonts/InterDisplay-Black.otf)

set_target_properties(UmberUiAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
