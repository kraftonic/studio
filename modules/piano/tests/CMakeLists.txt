# SPDX-License-Identifier: MIT
add_executable(kraftonic_piano_tests PianoLayerTests.cpp)
target_link_libraries(kraftonic_piano_tests PRIVATE kraftonic::piano kraftonic::sampler Catch2::Catch2WithMain)
kraftonic_set_warnings(kraftonic_piano_tests)
catch_discover_tests(kraftonic_piano_tests TEST_PREFIX "piano: ")

add_executable(kraftonic_piano_realtime PianoRealtime.cpp)
target_link_libraries(kraftonic_piano_realtime PRIVATE kraftonic::piano kraftonic::sampler)
kraftonic_set_warnings(kraftonic_piano_realtime)
add_test(NAME "piano: realtime and performance" COMMAND kraftonic_piano_realtime)
# Wall-clock real-time gate: run alone so parallel ctest load cannot fail it.
set_tests_properties ("piano: realtime and performance" PROPERTIES RUN_SERIAL TRUE)
