# SPDX-License-Identifier: MIT
#
# kraftonic_piano: the shared piano layer: piano models,
# voices, a sampled grand and the piano bus.
# Framework-independent like modules/core: no plug-in SDK, UI
# toolkit or OS header.

add_library(kraftonic_piano STATIC
    src/ModelVoice.cpp
    src/PianoLayer.cpp)
add_library(kraftonic::piano ALIAS kraftonic_piano)
target_include_directories(kraftonic_piano PUBLIC include)
target_link_libraries(kraftonic_piano PUBLIC kraftonic::core PRIVATE kraftonic::sampler kraftonic::fx)
set_target_properties(kraftonic_piano PROPERTIES POSITION_INDEPENDENT_CODE ON)
kraftonic_set_warnings(kraftonic_piano)

if(KRAFTONIC_BUILD_MODULE_TESTS)
    add_subdirectory(tests)
endif()
