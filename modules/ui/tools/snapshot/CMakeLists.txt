# SPDX-License-Identifier: MIT
#
# kraftonic_editor_snapshot (macOS dev tool, never shipped): renders the editor
# of any built .vst3 bundle to PNG without a host or screen-recording permission.
#   kraftonic_editor_snapshot <bundle.vst3> <out.png> [scale]

add_executable(kraftonic_editor_snapshot
    EditorSnapshot.mm
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm"
)
set_source_files_properties(
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm"
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
# SDK headers are not warning-clean at our level: include them as system headers.
target_include_directories(kraftonic_editor_snapshot SYSTEM PRIVATE "${vst3sdk_SOURCE_DIR}")
target_link_libraries(kraftonic_editor_snapshot PRIVATE sdk_hosting "-framework AppKit" "-framework QuartzCore")
target_compile_options(kraftonic_editor_snapshot PRIVATE -fobjc-arc)
kraftonic_set_source_warnings(EditorSnapshot.mm)
# VST3 interfaces are COM-style (lifetime through release()): their destructors
# are non-virtual by design.
set_property(SOURCE EditorSnapshot.mm APPEND PROPERTY COMPILE_OPTIONS "-Wno-non-virtual-dtor")
