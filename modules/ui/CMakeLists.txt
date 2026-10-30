# SPDX-License-Identifier: MIT
#
# kraftonic ui: the shared kit panel.
#   tools/kraftonic_panel.py   panel description -> .uidesc + mapping table
#   tools/check_panel.py       .uidesc checks (XML, bounds, overlap, tags, params, bitmaps)
#   tools/kit_resources.py     .uidesc -> bundle resource list (bitmaps, fonts)
#   tools/snapshot/            kraftonic_editor_snapshot (macOS dev tool)
#   kit/generator/             renders the kit PNGs (developer step, not a build step)
#   kit/assets/                curated PNGs (1x + 2x), manifest.json, fonts
# There is no C++ library: stock VSTGUI views plus kit bitmaps produce the
# look. See README.md.

set(KRAFTONIC_UI_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/tools" CACHE INTERNAL "modules/ui tools folder")
set(KRAFTONIC_UI_KIT_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/kit/assets" CACHE INTERNAL "kit bitmaps and fonts")

# Python drives the generator, the checks and the resource list, so it is
# needed for plug-in builds as well as tests.
if(KRAFTONIC_BUILD_TESTS OR KRAFTONIC_BUILD_PLUGINS)
    find_package(Python3 3.9 COMPONENTS Interpreter)
    # Cached so product folders (another directory scope) can use it too.
    set(KRAFTONIC_UI_PYTHON "${Python3_EXECUTABLE}" CACHE INTERNAL "Python for the ui tools")
endif()

# kraftonic_add_ui_kit_resources(<target> UIDESC <file.uidesc>)
# Adds the .uidesc plus every kit bitmap it uses (1x and 2x) to the bundle
# root, and the kit fonts to Resources/Fonts, where the pinned VSTGUI loads
# bundle fonts from on macOS, Windows and Linux (see modules/ui/README.md).
# The SDK copies resources by file name only, so the .uidesc declares flat
# basename paths (see kraftonic_panel.py); VSTGUI resolves name#2x.png
# automatically.
function(kraftonic_add_ui_kit_resources target)
    cmake_parse_arguments(ARG "" "UIDESC" "" ${ARGN})
    if(NOT ARG_UIDESC)
        message(FATAL_ERROR "kraftonic_add_ui_kit_resources: UIDESC is required")
    endif()
    if(NOT KRAFTONIC_UI_PYTHON)
        message(FATAL_ERROR "kraftonic_add_ui_kit_resources: Python 3.9+ is required")
    endif()
    # Re-run configure when the panel or the kit manifest changes, so a
    # regenerate that adds a bitmap cannot leave the bundle stale.
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS "${ARG_UIDESC}" "${KRAFTONIC_UI_KIT_ASSETS}/manifest.json")
    foreach(_kind bitmaps fonts)
        execute_process(
            COMMAND "${KRAFTONIC_UI_PYTHON}" -B "${KRAFTONIC_UI_TOOLS}/kit_resources.py"
                --uidesc "${ARG_UIDESC}" --assets "${KRAFTONIC_UI_KIT_ASSETS}" --kind ${_kind}
            OUTPUT_VARIABLE _kit_${_kind} OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _kit_result)
        if(NOT _kit_result EQUAL 0)
            message(FATAL_ERROR "kraftonic_add_ui_kit_resources: kit_resources.py (${_kind}) failed for ${ARG_UIDESC}")
        endif()
    endforeach()
    # Quoted: file names contain '#' (the 2x scale) and must stay literal.
    smtg_target_add_plugin_resources(${target} RESOURCES "${ARG_UIDESC}" "${_kit_bitmaps}")
    smtg_target_add_plugin_resources(${target} RESOURCES "${_kit_fonts}" OUTPUT_SUBDIRECTORY Fonts)
endfunction()

if(KRAFTONIC_BUILD_MODULE_TESTS)
    if(Python3_Interpreter_FOUND)
        add_test(NAME "ui: panel library self-test"
            COMMAND "${KRAFTONIC_UI_PYTHON}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_kraftonic_panel.py")
    else()
        message(STATUS "Python 3.9+ not found: ui: panel tests are skipped")
    endif()
endif()

# kraftonic_add_panel_test(<product> UIDESC <file> GENERATOR <script> [PARAMS_HEADER <h>] [ENUM <name>])
# Registers "ui: <product> panel": the product's .uidesc is valid, every view is
# in bounds and clear of its siblings, every bound tag exists, every ParamId in
# PARAMS_HEADER is on the panel exactly once, and the committed files match a
# fresh run of GENERATOR.
function(kraftonic_add_panel_test product)
    cmake_parse_arguments(ARG "" "UIDESC;GENERATOR;PARAMS_HEADER;ENUM" "" ${ARGN})
    if(NOT KRAFTONIC_BUILD_TESTS OR NOT KRAFTONIC_UI_PYTHON)
        return()
    endif()
    set(args "${ARG_UIDESC}")
    if(ARG_GENERATOR)
        list(APPEND args --generator "${ARG_GENERATOR}")
    endif()
    if(ARG_PARAMS_HEADER)
        list(APPEND args --params-header "${ARG_PARAMS_HEADER}")
    endif()
    if(ARG_ENUM)
        list(APPEND args --enum "${ARG_ENUM}")
    endif()
    add_test(NAME "ui: ${product} panel"
        COMMAND "${KRAFTONIC_UI_PYTHON}" -B "${KRAFTONIC_UI_TOOLS}/check_panel.py" ${args})
endfunction()

if(KRAFTONIC_BUILD_PLUGINS AND SMTG_MAC)
    add_subdirectory(tools/snapshot)
endif()
