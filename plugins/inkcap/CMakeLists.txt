# Inkcap: built from the repository root (see ../../CMakeLists.txt), which sets up the
# dependencies, the shared compile settings and the MYZEL_* options. Layout and targets:
# cmake/MyzelPlugin.cmake.
#   src/dsp                 ModuleInfo (modules, types, Glitch's grid), DamageCurves
#   src/dsp/common          SeedRng, SeedCode (frozen), GridClock, CaptureBuffer, Crossfade
#   src/dsp/modules/<m>     one directory per module: glitch, warble, crush, bits, hiss, room
#                           (Module.h: the interface they share; every source is globbed)
#   src/dsp/chain           the full signal chain: module order, Match, Mix, latency, telemetry
#   src/ui                  the glass face: InkcapEditor and the lane's kit (src/ui/DESIGN.md)
project(Inkcap VERSION 0.1.0 LANGUAGES C CXX)

myzel_add_plugin(Inkcap
    CODE Inkc
    VST3_CATEGORIES Fx Distortion
    CLAP_FEATURES audio-effect multi-effects distortion glitch stereo mono)

# The display's monospaced face, JetBrains Mono (SIL OFL 1.1, src/ui/assets/fonts/OFL.txt).
# Inter comes with the shared kit.
juce_add_binary_data(InkcapAssets
    HEADER_NAME InkcapAssets.h
    NAMESPACE InkcapAssets
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/assets/fonts/JetBrainsMono-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/assets/fonts/JetBrainsMono-Medium.ttf)
set_target_properties(InkcapAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(InkcapShared INTERFACE InkcapAssets)
