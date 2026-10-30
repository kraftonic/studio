# SPDX-License-Identifier: MIT
# Test names start with "startersynth: " so scripts/check.sh can select them.

# Measured DSP tests (Catch2), SDK-free: they also run in the fast DSP loop.
add_executable(startersynth_tests StarterSynthTests.cpp)
target_link_libraries(startersynth_tests PRIVATE startersynth_dsp Catch2::Catch2WithMain)
kraftonic_set_warnings(startersynth_tests)
catch_discover_tests(startersynth_tests TEST_PREFIX "startersynth: ")

# No allocation on the audio thread, and enough speed headroom.
add_executable(startersynth_realtime StarterSynthRealtime.cpp)
target_link_libraries(startersynth_realtime PRIVATE startersynth_dsp)
kraftonic_set_warnings(startersynth_realtime)
add_test(NAME "startersynth: realtime and performance" COMMAND startersynth_realtime)

# The plug-in layer (traits, state, presets, value text) without a host.
add_executable(startersynth_plugin_tests StarterSynthPluginTests.cpp)
target_include_directories(startersynth_plugin_tests PRIVATE ../plugin)
target_link_libraries(startersynth_plugin_tests PRIVATE
    startersynth_dsp kraftonic::formats Catch2::Catch2WithMain)
kraftonic_set_warnings(startersynth_plugin_tests)
catch_discover_tests(startersynth_plugin_tests TEST_PREFIX "startersynth: plugin: ")

# Listening render (not a test): startersynth_render out.wav
add_executable(startersynth_render StarterSynthRender.cpp)
target_include_directories(startersynth_render PRIVATE ../plugin)
target_link_libraries(startersynth_render PRIVATE startersynth_dsp kraftonic::formats)
kraftonic_set_warnings(startersynth_render)

# begin template-only (scripts/new-product.sh removes this block from copies)
# scripts/new-product.sh turns this product into a new one: check that the
# copy is fully renamed and has a new identity (about a second, no compile).
if(NOT WIN32)
    add_test(NAME "startersynth: new-product.sh renames the copy and gives it a new identity"
        COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/tests/new-product-test.sh" synth)
endif()
# end template-only
