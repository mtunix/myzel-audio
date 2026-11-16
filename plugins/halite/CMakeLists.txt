# Halite: built from the repository root (see ../../CMakeLists.txt), which sets up the
# dependencies, the shared compile settings and the MYZEL_* options. Layout and targets:
# cmake/MyzelPlugin.cmake.
#   src/dsp                 the modes every part shares (Shape, Detection, Stereo, Key, Listen, Latency)
#   src/dsp/common          blocks Halite's parts share: the event ring, a real FFT, the STFT
#   src/dsp/detector        the Ess and Harsh detectors, the offline probe (NOTES.md)
#   src/dsp/shapes          the reduction shapes and the lanes the chain crossfades (NOTES.md; split/, spectral/ NOTES.md)
#   src/dsp/chain           the full signal chain: latency, reductions on the audio's clock, stereo, Listen, Bypass, Output
#   src/dsp/feed            the display feed the chain writes, and the message-thread spectrum, meters and cut curves (NOTES.md)
#   src/plugin              processor, parameters, buses, session state (latency mode), presets
#   src/ui                  the workbench editor and the lane's kit (src/ui/DESIGN.md)
#   tools/measure           HaliteMeasure: the detector, and with --reduction the chain, on the labelled corpora (gate code in tests/support)
#   tools/ui_snapshot       HaliteUiSnapshot: the editor to PNG, and the editor self-test
project(Halite VERSION 0.1.0 LANGUAGES C CXX)

myzel_add_plugin(Halite
    CODE Hlte
    VST3_CATEGORIES Fx Dynamics
    CLAP_FEATURES audio-effect de-esser mono stereo
    LV2_CLASS DynamicsPlugin)

# The workbench's typefaces, Inter and JetBrains Mono (SIL OFL 1.1; src/ui/assets/fonts/ATTRIBUTION.md).
juce_add_binary_data(HaliteAssets
    HEADER_NAME HaliteAssets.h
    NAMESPACE HaliteAssets
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/assets/fonts/Inter-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/assets/fonts/Inter-Medium.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/assets/fonts/Inter-SemiBold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/assets/fonts/JetBrainsMono-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/assets/fonts/JetBrainsMono-Medium.ttf)
set_target_properties(HaliteAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(HaliteShared INTERFACE HaliteAssets)
