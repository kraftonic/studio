# SPDX-License-Identifier: MIT
add_executable(kraftonic_synth_tests SynthTests.cpp)
target_link_libraries(kraftonic_synth_tests PRIVATE kraftonic::synth Catch2::Catch2WithMain)
kraftonic_set_warnings(kraftonic_synth_tests)
catch_discover_tests(kraftonic_synth_tests TEST_PREFIX "synth: ")
add_executable(kraftonic_synth_realtime SynthRealtime.cpp)
target_link_libraries(kraftonic_synth_realtime PRIVATE kraftonic::synth)
kraftonic_set_warnings(kraftonic_synth_realtime)
add_test(NAME "synth: realtime and performance" COMMAND kraftonic_synth_realtime)
