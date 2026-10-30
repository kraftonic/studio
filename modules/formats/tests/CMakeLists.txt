# SPDX-License-Identifier: MIT
add_subdirectory(selftest)

add_executable(kraftonic_formats_tests
    PendingParamUpdatesTests.cpp
    DisplayTextTests.cpp
    EventHelpersTests.cpp
    PresetTests.cpp
    OutputMeterTests.cpp
    UiNoteQueueTests.cpp
    HeldNotesTests.cpp
    StateCodecTests.cpp
    SteppedParamTests.cpp
    EffectHelpersTests.cpp
    selftest/SelfTestEngine.cpp
    selftest/SelfTestEngineTests.cpp)
target_include_directories(kraftonic_formats_tests PRIVATE selftest)
target_link_libraries(kraftonic_formats_tests PRIVATE kraftonic::formats Catch2::Catch2WithMain)
kraftonic_set_warnings(kraftonic_formats_tests)
catch_discover_tests(kraftonic_formats_tests TEST_PREFIX "formats: ")

# VST3 adapter tests (needs the SDK + VSTGUI, so only with plugins): drive the
# shared Parameter<Traits> and the promoted effect processor with no host.
if(KRAFTONIC_BUILD_PLUGINS)
    # memorystream.cpp is SDK code (not in the SDK libraries); exempt it from our warnings.
    set(plugin_sdk_memorystream "${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp")
    add_executable(kraftonic_formats_vst3_tests
        vst3/SteppedParameterTests.cpp
        vst3/EffectProcessorTests.cpp
        vst3/UiNotePathTests.cpp
        vst3/LinuxModuleStubs.cpp
        selftest/SelfTestEngine.cpp
        "${plugin_sdk_memorystream}")
    set_source_files_properties("${plugin_sdk_memorystream}" PROPERTIES COMPILE_OPTIONS "-w")
    target_include_directories(kraftonic_formats_vst3_tests PRIVATE selftest)
    target_include_directories(kraftonic_formats_vst3_tests SYSTEM PRIVATE
        "${vst3sdk_SOURCE_DIR}" "${vst3sdk_SOURCE_DIR}/vstgui4")
    target_link_libraries(kraftonic_formats_vst3_tests PRIVATE
        kraftonic::vst3 Catch2::Catch2WithMain
        # The SDK include set pulls in VSTGUI headers; the GNU linker needs
        # the core library for their inline code (same as the products' VST3 tests).
        vstgui sdk_hosting)
    if(APPLE)
        target_link_libraries(kraftonic_formats_vst3_tests PRIVATE "-framework CoreFoundation")
    endif()
    kraftonic_set_warnings(kraftonic_formats_vst3_tests)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(kraftonic_formats_vst3_tests PRIVATE -Wno-non-virtual-dtor)
    endif()
    catch_discover_tests(kraftonic_formats_vst3_tests TEST_PREFIX "formats: vst3: ")
endif()

# CLAP adapter tests: the CLAP headers only (no SDK, no host), so they build
# with any CLAP-enabled configuration.
if(KRAFTONIC_BUILD_CLAP)
    add_executable(kraftonic_formats_clap_tests
        clap/ClapPluginTests.cpp
        selftest/SelfTestEngine.cpp)
    target_include_directories(kraftonic_formats_clap_tests PRIVATE selftest)
    target_link_libraries(kraftonic_formats_clap_tests PRIVATE kraftonic::clap Catch2::Catch2WithMain)
    kraftonic_set_warnings(kraftonic_formats_clap_tests)
    catch_discover_tests(kraftonic_formats_clap_tests TEST_PREFIX "formats: ")
endif()
