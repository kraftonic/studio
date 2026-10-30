# SPDX-License-Identifier: MIT
#
# kraftonic_sampler: sample playback engine and sample-bank loader
#. Framework-independent like
# modules/core: no plug-in SDK, UI toolkit or OS header.

add_library(kraftonic_sampler STATIC
    src/WavFile.cpp
    src/SampleBank.cpp
    src/SampleLocations.cpp
    src/Sampler.cpp)
add_library(kraftonic::sampler ALIAS kraftonic_sampler)
target_include_directories(kraftonic_sampler PUBLIC include)
target_link_libraries(kraftonic_sampler PUBLIC kraftonic::core)
set_target_properties(kraftonic_sampler PROPERTIES POSITION_INDEPENDENT_CODE ON)
kraftonic_set_warnings(kraftonic_sampler)

# Offline sample-preparation tool (never shipped).
add_executable(kraftonic_sample_prep tools/SamplePrep.cpp)
target_link_libraries(kraftonic_sample_prep PRIVATE kraftonic::sampler)
kraftonic_set_warnings(kraftonic_sample_prep)

if(KRAFTONIC_BUILD_MODULE_TESTS)
    add_subdirectory(tests)
endif()
