# Mulberry: built from the repository root (see ../../CMakeLists.txt), which sets up the
# dependencies, the shared compile settings and the MYZEL_* options. Layout and targets:
# cmake/MyzelPlugin.cmake.
#   src/dsp/common          Mulberry's DSP building blocks (transport, heads, losses, clocked domain)
#   src/dsp/machines/<m>    one directory per delay machine
#   src/dsp/chain           the full signal chain: routing, loop, hold, slots
project(Mulberry VERSION 0.1.0 LANGUAGES C CXX)

myzel_add_plugin(Mulberry
    CODE Mlby
    VST3_CATEGORIES Fx Delay
    CLAP_FEATURES audio-effect delay stereo mono)
