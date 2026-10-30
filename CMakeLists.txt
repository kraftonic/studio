# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)

# Must be set before project() so the VST3 SDK does not pick its own default.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(KraftonicStudio
    VERSION 0.1.0
    DESCRIPTION "Kraftonic Studio: instruments and effects"
    HOMEPAGE_URL "https://github.com/kraftonic/studio"
    LANGUAGES C CXX)

# The VST3 SDK and VSTGUI contain Objective-C++ on macOS.
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

# Single-config generators (Ninja, Makefiles) default to Release, so the
# real-time performance tests measure optimised code.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# When a plug-in's own repository adds Studio with FetchContent or
# add_subdirectory, it gets the modules and the build helpers, but not the
# starters or Studio's own tests (see "Studio as a dependency" in docs/BUILDING.md).
option(KRAFTONIC_BUILD_PLUGINS "Build VST3 plug-ins (downloads the Steinberg VST3 SDK)" ON)
option(KRAFTONIC_BUILD_TESTS "Build unit tests for the plug-ins being built (downloads Catch2)" ON)
option(KRAFTONIC_BUILD_STARTERS "Build the plug-ins in plugins/ (the starters and your own)" ${PROJECT_IS_TOP_LEVEL})
option(KRAFTONIC_BUILD_MODULE_TESTS "Build the tests of Studio's own modules" ${PROJECT_IS_TOP_LEVEL})
# macOS only: each plug-in also becomes an Audio Unit (AUv2), the VST3 wrapped
# by the VST3 SDK's AU wrapper (downloads Apple's AudioUnitSDK).
include(CMakeDependentOption)
cmake_dependent_option(KRAFTONIC_BUILD_AU "Also build an Audio Unit (.component) of each plug-in (macOS)" ON
    "APPLE;KRAFTONIC_BUILD_PLUGINS" OFF)
# Each plug-in also becomes a CLAP plug-in (.clap) on every platform
# (downloads the CLAP headers).
cmake_dependent_option(KRAFTONIC_BUILD_CLAP "Also build a CLAP plug-in (.clap) of each plug-in" ON
    "KRAFTONIC_BUILD_PLUGINS" OFF)
option(KRAFTONIC_WARNINGS_AS_ERRORS "Treat warnings in Kraftonic code as errors" OFF)
if(NOT KRAFTONIC_BUILD_TESTS)
    set(KRAFTONIC_BUILD_MODULE_TESTS OFF) # they need Catch2
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(KRAFTONIC_STUDIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Kraftonic Studio source folder")
include(KraftonicWarnings)
include(KraftonicBundle)
include(KraftonicDependencies)
include(KraftonicProductVersion)
include(KraftonicPlugin)

if(KRAFTONIC_BUILD_PLUGINS)
    kraftonic_fetch_vst3sdk()
    if(KRAFTONIC_BUILD_AU)
        kraftonic_fetch_audiounitsdk()
    endif()
endif()
if(KRAFTONIC_BUILD_CLAP)
    kraftonic_fetch_clap()
endif()
# Catch2 after the SDK: KraftonicDependencies documents why (Windows _UNICODE).
if(KRAFTONIC_BUILD_TESTS)
    enable_testing()
    kraftonic_fetch_catch2()
    include(Catch)
endif()

# Platform modules. Dependencies only point "down":
# plugins -> modules -> core. See docs/ARCHITECTURE.md.
add_subdirectory(modules/core)
add_subdirectory(modules/fx)
add_subdirectory(modules/sampler)
add_subdirectory(modules/piano)
add_subdirectory(modules/synth)
add_subdirectory(modules/wavetable)
add_subdirectory(modules/formats)
add_subdirectory(modules/ui)

# The plug-ins: the starters (copy one with scripts/new-product.sh) and your own.
if(KRAFTONIC_BUILD_STARTERS)
    add_subdirectory(plugins/startersynth)
    add_subdirectory(plugins/startereffect)
endif()

