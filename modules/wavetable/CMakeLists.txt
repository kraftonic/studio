# SPDX-License-Identifier: MIT
#
# kraftonic_wavetable: the wavetable synth engine.
# Framework-independent like
# modules/core: no plug-in SDK, UI toolkit or OS header. The headers in
# include/ (WaveEngine.h, WaveParameters.h) are a shared contract.

add_library(kraftonic_wavetable STATIC
    src/WaveEngine.cpp
    src/dsp/Fft.cpp
    src/dsp/Wavetables.cpp)
add_library(kraftonic::wavetable ALIAS kraftonic_wavetable)
target_include_directories(kraftonic_wavetable PUBLIC include)
target_link_libraries(kraftonic_wavetable PUBLIC kraftonic::core kraftonic::fx)
set_target_properties(kraftonic_wavetable PROPERTIES POSITION_INDEPENDENT_CODE ON)
kraftonic_set_warnings(kraftonic_wavetable)

if(KRAFTONIC_BUILD_MODULE_TESTS)
    add_subdirectory(tests)
endif()
