# Cerulean: built from the repository root (see ../../CMakeLists.txt), which sets up the
# dependencies, the shared compile settings and the MYZEL_* options. Layout and targets:
# cmake/MyzelPlugin.cmake.
#   src/dsp/common          shared reverb building blocks (header-only where possible)
#   src/dsp/algorithms/<a>  one directory per algorithm (engine)
#   src/dsp/chain           the full signal chain and the engine factory
project(Cerulean VERSION 0.1.0 LANGUAGES C CXX)

option(CERULEAN_ONLY_HALL "Build every algorithm as Hall (UI work while algorithms are in flux)" OFF)

file(GLOB_RECURSE dsp_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp)
set(dsp_definitions "")

if(CERULEAN_ONLY_HALL)
    list(FILTER dsp_sources EXCLUDE REGEX "/src/dsp/algorithms/(room|plate|spring|nimbus|halo|rise|billow|choir|echo)/")
    set(dsp_definitions CERULEAN_ONLY_HALL=1)
endif()

myzel_add_plugin(Cerulean
    CODE Crln
    VST3_CATEGORIES Fx Reverb
    CLAP_FEATURES audio-effect reverb stereo mono
    DSP_SOURCES ${dsp_sources}
    DSP_DEFINITIONS ${dsp_definitions})
