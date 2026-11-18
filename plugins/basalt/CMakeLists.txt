# Basalt: built from the repository root (see ../../CMakeLists.txt), which sets up the
# dependencies, the shared compile settings and the MYZEL_* options. Layout and targets:
# cmake/MyzelPlugin.cmake.
#   src/dsp                 the modes every part shares (Model, VCA detector, Character, Stereo, Key, Listen, Latency)
#   src/dsp/chain           the signal chain: key path, lanes and model dispatch, delays, Mix, Listen, Output, Bypass
#   src/dsp/models/<m>      one directory per model (NOTES.md)
#   src/dsp/dynamics        the dynamics core: detector, gain computer, ballistics (NOTES.md)
#   src/dsp/character       Warm, Iron, Edge and their oversampling (NOTES.md)
#   src/plugin              processor, parameters, buses, session state (latency mode)
project(Basalt VERSION 0.1.0 LANGUAGES C CXX)

# GENERIC_EDITOR until Basalt has its workbench editor (PLAN.md, M4). Code Bslt is provisional
# until the name screen and frozen in M0 after it (PLAN.md, Plugin metadata).
myzel_add_plugin(Basalt
    CODE Bslt
    VST3_CATEGORIES Fx Dynamics
    CLAP_FEATURES audio-effect compressor mono stereo
    LV2_CLASS CompressorPlugin
    GENERIC_EDITOR)
