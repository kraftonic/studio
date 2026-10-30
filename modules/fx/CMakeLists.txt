# SPDX-License-Identifier: MIT
#
# kraftonic_fx: the shared effects library.
# Framework-independent: no plug-in SDK, UI toolkit or OS dependency.
# See include/kraftonic/fx/Effect.h for the contract every effect follows.

add_library(kraftonic_fx STATIC
    src/FxModule.cpp
    src/Drive.cpp
    src/Phaser.cpp
    src/Chorus.cpp
    src/Delay.cpp
    src/Reverb.cpp
    src/EQ.cpp)
add_library(kraftonic::fx ALIAS kraftonic_fx)
target_include_directories(kraftonic_fx PUBLIC include)
target_link_libraries(kraftonic_fx PUBLIC kraftonic::core)
set_target_properties(kraftonic_fx PROPERTIES POSITION_INDEPENDENT_CODE ON)
kraftonic_set_warnings(kraftonic_fx)

if(KRAFTONIC_BUILD_MODULE_TESTS)
    add_subdirectory(tests)
endif()
