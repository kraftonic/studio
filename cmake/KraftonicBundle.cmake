# SPDX-License-Identifier: MIT
#
# kraftonic_set_bundle_info(<target> <id> [<bundle identifier>])
# Fills the macOS bundle Info.plist (name, identifier, by default
# com.kraftonic.<id>, and the project version). VST3 SDK 3.8.1's
# smtg_target_set_bundle sets these only under the Xcode generator, so Ninja
# builds shipped an Info.plist with empty versions. These target properties work with every generator.
# Also adds a checked moduleinfo.json step (see KraftonicModuleInfo.cmake).
# Call it after smtg_add_vst3plugin(), so it runs after the SDK's own step.
function(kraftonic_set_bundle_info target id)
    if(TARGET moduleinfotool AND SMTG_CREATE_MODULE_INFO AND (APPLE OR SMTG_LINUX))
        get_target_property(bundle_path ${target} SMTG_PLUGIN_PACKAGE_PATH)
        set(sign OFF)
        if(APPLE)
            set(sign ON)
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:moduleinfotool>" "-DBUNDLE=${bundle_path}"
                    "-DVERSION=${PROJECT_VERSION}" "-DSIGN=${sign}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KraftonicModuleInfo.cmake"
            VERBATIM)
    endif()
    if(NOT APPLE)
        return()
    endif()
    set(identifier "com.kraftonic.${id}")
    if(ARGC GREATER 2)
        set(identifier "${ARGV2}")
    endif()
    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "${target}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${identifier}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_STRING "${PROJECT_VERSION}"
    )
endfunction()
