# SPDX-License-Identifier: MIT
#
# Kraftonic's warning policy. Applied only to code we own, never to
# third-party sources (the VST3 SDK and VSTGUI sources compile into plug-in targets).

if(MSVC)
    set(KRAFTONIC_WARNING_FLAGS /W4 /permissive- /utf-8)
    set(KRAFTONIC_WERROR_FLAG /WX)
else()
    set(KRAFTONIC_WARNING_FLAGS
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Wnon-virtual-dtor)
    set(KRAFTONIC_WERROR_FLAG -Werror)
endif()
if(KRAFTONIC_WARNINGS_AS_ERRORS)
    list(APPEND KRAFTONIC_WARNING_FLAGS ${KRAFTONIC_WERROR_FLAG})
endif()
# Global, so the functions below work from any directory, including a
# plug-in repository that adds Studio as a subproject.
set_property(GLOBAL PROPERTY KRAFTONIC_WARNING_FLAGS "${KRAFTONIC_WARNING_FLAGS}")

# For targets whose sources are all ours.
function(kraftonic_set_warnings target)
    get_property(flags GLOBAL PROPERTY KRAFTONIC_WARNING_FLAGS)
    target_compile_options(${target} PRIVATE ${flags})
endfunction()

# For targets that mix our sources with third-party ones (e.g. VST3 plug-ins).
function(kraftonic_set_source_warnings)
    get_property(flags GLOBAL PROPERTY KRAFTONIC_WARNING_FLAGS)
    set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "${flags}")
endfunction()
