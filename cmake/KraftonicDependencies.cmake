# SPDX-License-Identifier: MIT
#
# Third-party dependencies, pinned to exact commits. Review THIRD_PARTY_NOTICES.md
# before adding or upgrading anything.
#
# Offline / local checkout: -DFETCHCONTENT_SOURCE_DIR_VST3SDK=/path/to/vst3sdk
# (and -DFETCHCONTENT_SOURCE_DIR_AUDIOUNITSDK=/path/to/AudioUnitSDK on macOS,
# -DFETCHCONTENT_SOURCE_DIR_CLAP=/path/to/clap for CLAP builds)

include(FetchContent)

# Steinberg VST3 SDK 3.8.1 (MIT), with VSTGUI 4 (BSD-3-Clause).
set(KRAFTONIC_VST3SDK_TAG "3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96") # v3.8.1_build_84
# Apple AudioUnitSDK 1.4.0 (Apache-2.0), macOS Audio Unit builds only.
set(KRAFTONIC_AUDIOUNITSDK_TAG "bd98b31feff57a15989fcfab4cd86dc63382b1ac") # AudioUnitSDK-1.4.0
# CLAP 1.2.10 (MIT), headers only: the CLAP builds of the plug-ins.
set(KRAFTONIC_CLAP_TAG "195b42a004144fab0b3cf95e9c067187d15365b7") # 1.2.10
# Catch2 v3.16.0 (BSL-1.0), tests only.
set(KRAFTONIC_CATCH2_TAG "fd79eadb5bc1760e7cbae12fd45b0d0040d1bb73") # v3.16.0

macro(kraftonic_fetch_vst3sdk)
    # SDK options: no SDK examples except the hosting tools (validator).
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE) # CI runs it explicitly

    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG        ${KRAFTONIC_VST3SDK_TAG}
        GIT_SUBMODULES base cmake pluginterfaces public.sdk vstgui4
        GIT_SHALLOW    FALSE
        # Populate only; we add the SDK ourselves below (its hosting tools - validator,
        # editorhost - are part of the default build so every generator can run them).
        SOURCE_SUBDIR  kraftonic-populate-only)
    FetchContent_MakeAvailable(vst3sdk)
    add_subdirectory("${vst3sdk_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/vst3sdk")
    smtg_enable_vst3_sdk()
endmacro()

# Apple's AudioUnitSDK, which the VST3 SDK's AU wrapper is built on. Its own
# project is Xcode-only, so we build its sources as a static library here.
macro(kraftonic_fetch_audiounitsdk)
    FetchContent_Declare(audiounitsdk
        GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
        GIT_TAG        ${KRAFTONIC_AUDIOUNITSDK_TAG}
        GIT_SHALLOW    FALSE
        SOURCE_SUBDIR  kraftonic-populate-only)
    FetchContent_MakeAvailable(audiounitsdk)
    set(ausdk "${audiounitsdk_SOURCE_DIR}/src/AudioUnitSDK")
    add_library(kraftonic_audiounitsdk STATIC
        "${ausdk}/AUBase.cpp" "${ausdk}/AUBuffer.cpp" "${ausdk}/AUBufferAllocator.cpp"
        "${ausdk}/AUEffectBase.cpp" "${ausdk}/AUInputElement.cpp" "${ausdk}/AUMIDIBase.cpp"
        "${ausdk}/AUMIDIEffectBase.cpp" "${ausdk}/AUOutputElement.cpp" "${ausdk}/AUPlugInDispatch.cpp"
        "${ausdk}/AUScopeElement.cpp" "${ausdk}/ComponentBase.cpp" "${ausdk}/MusicDeviceBase.cpp")
    target_include_directories(kraftonic_audiounitsdk SYSTEM PUBLIC "${audiounitsdk_SOURCE_DIR}/include")
    # Its headers use std::expected: C++23 for it and the wrapper that includes them.
    target_compile_features(kraftonic_audiounitsdk PUBLIC cxx_std_23)
    target_link_libraries(kraftonic_audiounitsdk PUBLIC
        "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation")
endmacro()

# The CLAP headers (no library to build): target kraftonic_clap_sdk.
macro(kraftonic_fetch_clap)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        ${KRAFTONIC_CLAP_TAG}
        GIT_SHALLOW    FALSE
        SOURCE_SUBDIR  kraftonic-populate-only)
    FetchContent_MakeAvailable(clap)
    add_library(kraftonic_clap_sdk INTERFACE)
    target_include_directories(kraftonic_clap_sdk SYSTEM INTERFACE "${clap_SOURCE_DIR}/include")
endmacro()

macro(kraftonic_fetch_catch2)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${KRAFTONIC_CATCH2_TAG}
        GIT_SHALLOW    FALSE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    # The VST3 SDK adds -D_UNICODE project-wide on Windows, which makes Catch2
    # emit wmain() instead of main() and the test executables fail to link.
    if(WIN32)
        target_compile_definitions(Catch2WithMain PRIVATE DO_NOT_USE_WMAIN)
    endif()
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endmacro()
