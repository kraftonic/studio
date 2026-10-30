# SPDX-License-Identifier: MIT
#
# kraftonic_formats: plug-in format adapters.
#
# Two layers:
#   kraftonic::formats  SDK-free helpers (this target). Unit-tested without a host.
#   kraftonic::vst3     shared VST3 base classes in include/kraftonic/formats/vst3
#                       (templates over a product traits struct; needs the VST3
#                       SDK + VSTGUI).
#   kraftonic::clap     the CLAP adapter in include/kraftonic/formats/clap (same
#                       traits; needs the CLAP headers, and its editor needs
#                       kraftonic::vst3 for the panel).

add_library(kraftonic_formats INTERFACE)
add_library(kraftonic::formats ALIAS kraftonic_formats)
target_include_directories(kraftonic_formats INTERFACE include)
target_link_libraries(kraftonic_formats INTERFACE kraftonic::core)

if(KRAFTONIC_BUILD_PLUGINS)
    add_library(kraftonic_vst3 INTERFACE)
    add_library(kraftonic::vst3 ALIAS kraftonic_vst3)
    target_include_directories(kraftonic_vst3 INTERFACE include)
    # vstgui_support uses sdk symbols: list it first for single-pass linkers (GNU ld).
    target_link_libraries(kraftonic_vst3 INTERFACE kraftonic::formats vstgui_support sdk)
endif()

if(KRAFTONIC_BUILD_CLAP)
    add_library(kraftonic_clap INTERFACE)
    add_library(kraftonic::clap ALIAS kraftonic_clap)
    target_include_directories(kraftonic_clap INTERFACE include)
    target_link_libraries(kraftonic_clap INTERFACE kraftonic::formats kraftonic_clap_sdk)
endif()

if(KRAFTONIC_BUILD_MODULE_TESTS)
    add_subdirectory(tests)
endif()
