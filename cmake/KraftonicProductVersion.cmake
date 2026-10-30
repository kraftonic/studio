# SPDX-License-Identifier: MIT
#
# kraftonic_product_version()
# Per-product versions: reads plugins/<id>/VERSION
# (one line, x.y.z) and sets PROJECT_VERSION and its _MAJOR/_MINOR/_PATCH parts
# for the calling product directory and everything below it. Bundle info,
# moduleinfo.json and the SDK version file then carry the product's version
# instead of the Studio version. Call it first in plugins/<id>/CMakeLists.txt.
# A macro, so the variables land in the caller's directory scope.
macro(kraftonic_product_version)
    set(_kraftonic_version_file "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_kraftonic_version_file}")
    file(STRINGS "${_kraftonic_version_file}" _kraftonic_version LIMIT_COUNT 1)
    string(STRIP "${_kraftonic_version}" _kraftonic_version)
    if(NOT _kraftonic_version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        message(FATAL_ERROR "${_kraftonic_version_file}: '${_kraftonic_version}' is not x.y.z")
    endif()
    set(PROJECT_VERSION "${_kraftonic_version}")
    set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
    unset(_kraftonic_version)
    unset(_kraftonic_version_file)
endmacro()
