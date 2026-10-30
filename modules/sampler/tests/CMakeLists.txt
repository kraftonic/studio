# SPDX-License-Identifier: MIT
add_executable(kraftonic_sampler_tests
    SampleBankTests.cpp
    SamplerTests.cpp)
target_link_libraries(kraftonic_sampler_tests PRIVATE kraftonic::sampler Catch2::Catch2WithMain)
kraftonic_set_warnings(kraftonic_sampler_tests)
catch_discover_tests(kraftonic_sampler_tests TEST_PREFIX "sampler: ")

add_executable(kraftonic_sampler_realtime SamplerRealtime.cpp)
target_link_libraries(kraftonic_sampler_realtime PRIVATE kraftonic::sampler)
kraftonic_set_warnings(kraftonic_sampler_realtime)
add_test(NAME "sampler: realtime and performance" COMMAND kraftonic_sampler_realtime)
