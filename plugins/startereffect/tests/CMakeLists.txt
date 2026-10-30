# SPDX-License-Identifier: MIT
# Test names start with "startereffect: " so scripts/check.sh can select them.

# Measured DSP tests (Catch2), SDK-free: they also run in the fast DSP loop.
add_executable(startereffect_tests StarterEffectTests.cpp)
target_include_directories(startereffect_tests PRIVATE ../plugin)
target_link_libraries(startereffect_tests PRIVATE startereffect_dsp kraftonic::formats Catch2::Catch2WithMain)
kraftonic_set_warnings(startereffect_tests)
catch_discover_tests(startereffect_tests TEST_PREFIX "startereffect: ")

# No allocation on the audio thread, and enough speed headroom.
add_executable(startereffect_realtime StarterEffectRealtime.cpp)
target_link_libraries(startereffect_realtime PRIVATE startereffect_dsp)
kraftonic_set_warnings(startereffect_realtime)
add_test(NAME "startereffect: realtime and performance" COMMAND startereffect_realtime)

# The plug-in layer (traits, state, presets, value text) without a host.
add_executable(startereffect_plugin_tests StarterEffectPluginTests.cpp)
target_include_directories(startereffect_plugin_tests PRIVATE ../plugin)
target_link_libraries(startereffect_plugin_tests PRIVATE
    startereffect_dsp kraftonic::formats Catch2::Catch2WithMain)
kraftonic_set_warnings(startereffect_plugin_tests)
catch_discover_tests(startereffect_plugin_tests TEST_PREFIX "startereffect: plugin: ")

# Listening render (not a test): startereffect_render out.wav
add_executable(startereffect_render StarterEffectRender.cpp)
target_include_directories(startereffect_render PRIVATE ../plugin)
target_link_libraries(startereffect_render PRIVATE startereffect_dsp kraftonic::formats)
kraftonic_set_warnings(startereffect_render)

# begin template-only (scripts/new-product.sh removes this block from copies)
# scripts/new-product.sh turns this product into a new one: check that the
# copy is fully renamed and has a new identity (about a second, no compile).
if(NOT WIN32)
    add_test(NAME "startereffect: new-product.sh renames the copy and gives it a new identity"
        COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/tests/new-product-test.sh" effect)
endif()
# end template-only
