# SPDX-License-Identifier: MIT
#
# Prints the project version from the root CMakeLists.txt (project(... VERSION x.y.z)).
# Used by packaging scripts and CI so installers carry the CMake project version.
# Usage: cmake -P cmake/KraftonicPrintVersion.cmake
#        cmake -DPRODUCT=<id> -P cmake/KraftonicPrintVersion.cmake   (plugins/<id>/VERSION)
cmake_minimum_required(VERSION 3.25)

get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED PRODUCT)
    set(_file "${_root}/plugins/${PRODUCT}/VERSION")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "No ${_file}")
    endif()
    file(STRINGS "${_file}" _v LIMIT_COUNT 1)
    string(STRIP "${_v}" _v)
    if(NOT _v MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "${_file}: '${_v}' is not x.y.z")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${_v}")
    return()
endif()
file(READ "${_root}/CMakeLists.txt" _text)
string(REGEX MATCH "project[ \t]*\\([^)]*[ \t\r\n]VERSION[ \t\r\n]+([0-9]+(\\.[0-9]+)*)" _match "${_text}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "No project(... VERSION x.y.z) found in ${_root}/CMakeLists.txt")
endif()
# message() writes to stderr; use echo so callers can capture stdout.
execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${CMAKE_MATCH_1}")
