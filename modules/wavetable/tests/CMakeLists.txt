# SPDX-License-Identifier: MIT
add_executable(kraftonic_wavetable_tests
    WaveContractTests.cpp
    WaveTableTests.cpp
    WaveDspTests.cpp)
target_link_libraries(kraftonic_wavetable_tests PRIVATE kraftonic::wavetable Catch2::Catch2WithMain)
kraftonic_set_warnings(kraftonic_wavetable_tests)
catch_discover_tests(kraftonic_wavetable_tests TEST_PREFIX "wavetable: ")

add_executable(kraftonic_wavetable_realtime WaveRealtimeTests.cpp)
target_link_libraries(kraftonic_wavetable_realtime PRIVATE kraftonic::wavetable)
kraftonic_set_warnings(kraftonic_wavetable_realtime)
add_test(NAME "wavetable: realtime and performance" COMMAND kraftonic_wavetable_realtime)

