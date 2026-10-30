# SPDX-License-Identifier: MIT
add_executable(kraftonic_core_tests
    AdsrTests.cpp
    LowPassFilterTests.cpp
    ParametersTests.cpp
    SmootherTests.cpp
    StereoPeakProtectorTests.cpp
    VoiceAllocatorTests.cpp)
target_link_libraries(kraftonic_core_tests PRIVATE kraftonic::core Catch2::Catch2WithMain)
kraftonic_set_warnings(kraftonic_core_tests)
catch_discover_tests(kraftonic_core_tests TEST_PREFIX "core: ")
