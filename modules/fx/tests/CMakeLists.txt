# SPDX-License-Identifier: MIT
add_executable(kraftonic_fx_tests
    EffectContractTests.cpp
    EffectsTests.cpp)
target_link_libraries(kraftonic_fx_tests PRIVATE kraftonic::fx Catch2::Catch2WithMain)
kraftonic_set_warnings(kraftonic_fx_tests)
catch_discover_tests(kraftonic_fx_tests TEST_PREFIX "fx: ")

add_executable(kraftonic_fx_realtime FxRealtimeTests.cpp)
target_link_libraries(kraftonic_fx_realtime PRIVATE kraftonic::fx)
kraftonic_set_warnings(kraftonic_fx_realtime)
add_test(NAME "fx: realtime and performance" COMMAND kraftonic_fx_realtime)
