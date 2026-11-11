# Umber: built from the repository root (see ../../CMakeLists.txt), which sets up the
# dependencies, the shared compile settings and the MYZEL_* options. Layout and targets:
# cmake/MyzelPlugin.cmake.
#   src/dsp/common      Umber's DSP building blocks (header-only where possible)
#   src/dsp/models/<m>  one directory per circuit model
#   src/dsp/chain       the full signal chain
project(Umber VERSION 0.1.0 LANGUAGES C CXX)

myzel_add_plugin(Umber
    CODE Umbr
    VST3_CATEGORIES Fx Distortion
    CLAP_FEATURES audio-effect distortion stereo mono
    DSP_LIBRARIES chowdsp::chowdsp_wdf)
