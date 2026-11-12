# Viridian: built from the repository root (see ../../CMakeLists.txt), which sets up the
# dependencies, the shared compile settings and the MYZEL_* options. Layout and targets:
# cmake/MyzelPlugin.cmake.
#   src/dsp                 ModelInfo (slot mapping per model) and EqParams
#   src/dsp/models/<m>      one directory per EQ model
#   src/dsp/chain           the full signal chain
project(Viridian VERSION 0.1.0 LANGUAGES C CXX)

# GENERIC_EDITOR until Viridian has its own panel and Theme (PLAN.md, M4).
myzel_add_plugin(Viridian
    CODE Vrdn
    VST3_CATEGORIES Fx EQ
    CLAP_FEATURES audio-effect equalizer stereo mono
    GENERIC_EDITOR)
