# SPDX-License-Identifier: MIT
# Shared, SDK-free synthesizer DSP building blocks.
add_library(kraftonic_synth STATIC src/SynthModule.cpp)
add_library(kraftonic::synth ALIAS kraftonic_synth)
target_include_directories(kraftonic_synth PUBLIC include)
target_link_libraries(kraftonic_synth PUBLIC kraftonic::core)
target_compile_features(kraftonic_synth PUBLIC cxx_std_20)
set_target_properties(kraftonic_synth PROPERTIES POSITION_INDEPENDENT_CODE ON)
kraftonic_set_warnings(kraftonic_synth)
if(KRAFTONIC_BUILD_MODULE_TESTS)
    add_subdirectory(tests)
endif()
