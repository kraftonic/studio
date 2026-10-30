# SPDX-License-Identifier: MIT
#
# kraftonic_core: reusable, framework-independent DSP building blocks.
# No plug-in SDK, UI toolkit or OS dependency may be added here.

add_library(kraftonic_core STATIC
    src/Adsr.cpp
    src/LowPassFilter.cpp)
add_library(kraftonic::core ALIAS kraftonic_core)
target_include_directories(kraftonic_core PUBLIC include)
target_compile_features(kraftonic_core PUBLIC cxx_std_20)
set_target_properties(kraftonic_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
kraftonic_set_warnings(kraftonic_core)

if(KRAFTONIC_BUILD_MODULE_TESTS)
    add_subdirectory(tests)
endif()
