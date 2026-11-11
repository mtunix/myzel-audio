cmake_minimum_required(VERSION 3.25)

# Myzel Audio: every plugin in one build. Each plugin lives in plugins/<name>/ and
# brings its own project() (name and version); dependencies and compile settings
# shared by all plugins are set up here once.
project(MyzelAudio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(MYZEL_ALL_PLUGINS cerulean umber)
set(MYZEL_PLUGINS "${MYZEL_ALL_PLUGINS}" CACHE STRING
    "Plugins to build, semicolon-separated (default: all of ${MYZEL_ALL_PLUGINS})")

option(MYZEL_BUILD_TESTS "Build the Catch2 test suites" ON)
option(MYZEL_BUILD_TOOLS "Build developer tools (measurement, calibration, UI snapshot)" ON)
option(MYZEL_ENABLE_LTO "Link-time optimisation (slow links; for release builds)" ON)
option(MYZEL_COPY_PLUGINS "Install built plugins into ~/.vst3, ~/.lv2, ~/.clap after building" OFF)

# Vendor identity, used by every plugin's juce_add_plugin().
set(MYZEL_COMPANY_NAME "Myzel Audio")
set(MYZEL_ID_PREFIX "com.myzelaudio")  # bundle and CLAP IDs: com.myzelaudio.<plugin>
set(MYZEL_LV2_PREFIX "urn:myzelaudio")  # LV2 URIs: urn:myzelaudio:<plugin>
set(MYZEL_MANUFACTURER_CODE Myzl)

foreach(plugin IN LISTS MYZEL_PLUGINS)
    if(NOT plugin IN_LIST MYZEL_ALL_PLUGINS)
        message(FATAL_ERROR "Unknown plugin '${plugin}' in MYZEL_PLUGINS (known: ${MYZEL_ALL_PLUGINS})")
    endif()
endforeach()

# Dependencies ----------------------------------------------------------------
# Set CPM_SOURCE_CACHE (e.g. ~/.cache/CPM) to share downloads between build dirs.
include(cmake/CPM.cmake)

CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)

CPMAddPackage(
    NAME clap-juce-extensions
    GITHUB_REPOSITORY free-audio/clap-juce-extensions
    GIT_TAG 6bde108329ff9fc24404b79b8b397261a75cf6a8
    GIT_SUBMODULES "clap-libs/clap" "clap-libs/clap-helpers")

CPMAddPackage(
    NAME melatonin_blur
    GITHUB_REPOSITORY sudara/melatonin_blur
    GIT_TAG fcc18578d9db3f9400c74ee1bb2eb1070ee959f9
    DOWNLOAD_ONLY YES)

if("umber" IN_LIST MYZEL_PLUGINS)
    CPMAddPackage(
        NAME chowdsp_wdf
        GITHUB_REPOSITORY Chowdhury-DSP/chowdsp_wdf
        GIT_TAG 43bcd295e8403de913f9966f4fd7ff9d17c9f1a5)
endif()

if(MYZEL_BUILD_TESTS)
    CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        VERSION 3.16.1
        GIT_SHALLOW ON)

    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    include(Catch)
    enable_testing()
endif()

# JUCE derives a module's name from its directory, but the CPM cache uses hashed
# directory names, so expose the module under its real name.
set(MYZEL_MODULES_DIR ${CMAKE_BINARY_DIR}/juce_modules)
file(MAKE_DIRECTORY ${MYZEL_MODULES_DIR})
file(CREATE_LINK ${melatonin_blur_SOURCE_DIR} ${MYZEL_MODULES_DIR}/melatonin_blur SYMBOLIC COPY_ON_ERROR)
juce_add_module(${MYZEL_MODULES_DIR}/melatonin_blur)

# Compile settings shared by every plugin ----------------------------------------
add_library(MyzelJuceConfig INTERFACE)

target_compile_definitions(MyzelJuceConfig INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(UNIX AND NOT APPLE)
    target_compile_definitions(MyzelJuceConfig INTERFACE JUCE_JACK=1 JUCE_ALSA=1)
endif()

target_link_libraries(MyzelJuceConfig INTERFACE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if(MYZEL_ENABLE_LTO)
    target_link_libraries(MyzelJuceConfig INTERFACE juce::juce_recommended_lto_flags)
endif()

# Plugins ---------------------------------------------------------------------
foreach(plugin IN LISTS MYZEL_PLUGINS)
    add_subdirectory(plugins/${plugin})
endforeach()
