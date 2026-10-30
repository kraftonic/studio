# SPDX-License-Identifier: MIT
#
# Post-build script: (re)writes a VST3 bundle's moduleinfo.json and fails the
# build unless it is valid JSON with the expected version.
#
# Why: on macOS, the first run of the SDK's moduleinfotool against a freshly
# built bundle exits 0 but leaves moduleinfo.json empty (reproduced in clean
# build trees; an immediate second run is correct). The SDK's own post-build
# step does not check its output, so empty files shipped.
#
# cmake -DTOOL=<moduleinfotool> -DBUNDLE=<x.vst3> -DVERSION=<x.y.z> [-DSIGN=ON] -P KraftonicModuleInfo.cmake
foreach(var TOOL BUNDLE VERSION)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "KraftonicModuleInfo.cmake: ${var} not set")
    endif()
endforeach()
set(out "${BUNDLE}/Contents/Resources/moduleinfo.json")
get_filename_component(tool_dir "${TOOL}" DIRECTORY)

set(ok FALSE)
foreach(attempt RANGE 1 5)
    execute_process(
        COMMAND "${TOOL}" -create -version "${VERSION}" -path "${BUNDLE}" -output "${out}"
        WORKING_DIRECTORY "${tool_dir}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE log ERROR_VARIABLE log)
    set(json "")
    if(EXISTS "${out}")
        file(READ "${out}" json)
    endif()
    if(rc EQUAL 0 AND NOT json STREQUAL "")
        string(JSON got ERROR_VARIABLE err GET "${json}" "Version")
        if(NOT err AND got STREQUAL VERSION)
            set(ok TRUE)
            break()
        endif()
    endif()
endforeach()
if(NOT ok)
    message(FATAL_ERROR "moduleinfo.json for ${BUNDLE} is empty, invalid or has the wrong version after 5 attempts: ${log}")
endif()

if(SIGN)
    execute_process(COMMAND codesign -f -s - "${out}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "codesign failed for ${out}")
    endif()
endif()
