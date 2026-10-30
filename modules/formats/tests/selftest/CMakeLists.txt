# SPDX-License-Identifier: MIT
#
# Self-test instrument proving the shared VST3 base.
# Test-only: the bundle is never installed or shipped; it exists so the
# Steinberg validator and pluginval can exercise kraftonic::vst3.
# Its editor is a minimal stock-VSTGUI view for exercising parameter binding.

# Listening-WAV renderer (SDK-free; also built in the fast DSP loop).
add_executable(kraftonic_selftest_render render_wav.cpp SelfTestEngine.cpp)
target_include_directories(kraftonic_selftest_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kraftonic_selftest_render PRIVATE kraftonic::core)
kraftonic_set_warnings(kraftonic_selftest_render)

if(KRAFTONIC_BUILD_PLUGINS)
    # NOTE: the bundle file name stays space-free ("KraftonicPluginSelfTest")
    # on purpose. The name hosts display comes from the factory registration
    # (kPluginName), not from this directory name, but the SDK's
    # smtg_target_add_plugin_resource replaces the package name with the
    # MSBuild macro $(TargetFileName) in its resource copy command. That
    # macro expands at build time, after CMake has decided quoting, so a name
    # with a space splits the copy_if_different command line and the Windows
    # build fails.
    smtg_add_vst3plugin(KraftonicPluginSelfTest
        PACKAGE_NAME "KraftonicPluginSelfTest"
        SOURCES_LIST
            SelfTestEngine.h
            SelfTestEngine.cpp
            SelfTestTraits.h
            SelfTestPlugin.h
            SelfTestEntry.cpp
    )
    kraftonic_set_bundle_info(KraftonicPluginSelfTest selftest)
    target_include_directories(KraftonicPluginSelfTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Third-party SDK/VSTGUI headers are not warning-clean at our warning
    # level; treat them as system headers so their warnings never break our
    # build. (-isystem wins over the duplicate -I the SDK targets add.)
    target_include_directories(KraftonicPluginSelfTest SYSTEM PRIVATE
        "${vst3sdk_SOURCE_DIR}"
        "${vst3sdk_SOURCE_DIR}/vstgui4"
    )

    # sdk + vstgui_support arrive transitively through kraftonic::vst3.
    target_link_libraries(KraftonicPluginSelfTest
        PRIVATE
            kraftonic::vst3
    )

    smtg_target_add_plugin_resources(KraftonicPluginSelfTest
        RESOURCES
            selftest.uidesc
    )

    if(SMTG_MAC)
        smtg_target_set_bundle(KraftonicPluginSelfTest
            BUNDLE_IDENTIFIER "com.kraftonic.selftest"
            COMPANY_NAME "Kraftonic"
        )
    endif()

    # Project warnings apply to our sources only, never to SDK/VSTGUI sources.
    kraftonic_set_source_warnings(
        SelfTestEntry.cpp
        SelfTestEngine.cpp
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # VST3 interfaces are COM-style: lifetime runs through release(), so
        # their destructors are non-virtual by design and deriving from them
        # must not warn.
        set_property(SOURCE
            SelfTestEntry.cpp
            SelfTestEngine.cpp
            APPEND PROPERTY COMPILE_OPTIONS "-Wno-non-virtual-dtor")
    endif()

endif()
