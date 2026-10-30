# SPDX-License-Identifier: MIT
#
# The public helpers a product's CMake uses, in Studio itself (plugins/*)
# and in a plug-in's own repository that adds Studio as a subproject.
#
#   kraftonic_enable_studio()     once, in the plug-in repository's top level
#   kraftonic_product_version()   first in plugins/<id>/CMakeLists.txt (KraftonicProductVersion.cmake)
#   kraftonic_add_plugin(...)     the VST3 bundle, the CLAP plug-in (and on macOS the Audio Unit), in plugins/<id>/CMakeLists.txt
#   kraftonic_set_warnings(<t>)   Kraftonic's warning policy for the product's own targets

# kraftonic_enable_studio()
# Call it in the top-level CMakeLists.txt of a plug-in repository, right after
# FetchContent_MakeAvailable(kraftonic_studio) (or add_subdirectory), before
# adding the products. It applies the VST3 SDK's compiler settings (symbol
# visibility, fast math, _UNICODE and the runtime on Windows, ...) and the
# Catch2 test setup to that directory. A macro: both are directory-scoped, and
# Studio's own scope does not reach a sibling directory.
macro(kraftonic_enable_studio)
    list(APPEND CMAKE_MODULE_PATH "${KRAFTONIC_STUDIO_DIR}/cmake")
    if(KRAFTONIC_BUILD_PLUGINS)
        FetchContent_GetProperties(vst3sdk)
        smtg_enable_vst3_sdk()
    endif()
    if(KRAFTONIC_BUILD_TESTS)
        enable_testing()
        FetchContent_GetProperties(Catch2)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
        include(Catch)
    endif()
endmacro()

# kraftonic_add_plugin(<Target>
#     PRODUCT <id>                    the product folder name (bundle id default, test names)
#     SOURCES <files>...              plug-in sources; our warnings apply to the C++ files
#     [LINK <targets>...]             the product's engine, e.g. <id>_dsp
#     [INCLUDE_DIRS <dirs>...]
#     [BUNDLE_IDENTIFIER <com.vendor.id>] [COMPANY_NAME <vendor>]
#     [AU_SUBTYPE <code> AU_MANUFACTURER <Code>]  4-character Audio Unit codes
#     [UIDESC <file.uidesc> [UI_GENERATOR <make_<id>_uidesc.py>] [PARAMS_HEADER <Parameters.h>]])
# Builds <Target>.vst3 on the shared VST3 base (kraftonic::vst3) with
# the product's version, the panel and the kit bitmaps and fonts it uses, and
# a checked moduleinfo.json. With UIDESC it also registers the ctest
# "ui: <id> panel" and an <id>_panel target that reruns UI_GENERATOR.
# With KRAFTONIC_BUILD_AU (macOS) the target <Target>_AU builds
# <Target>.component next to the .vst3 (see kraftonic_add_audio_unit).
# With KRAFTONIC_BUILD_CLAP the target <Target>_CLAP builds <Target>.clap
# there too (see kraftonic_add_clap); its CLAP id is the BUNDLE_IDENTIFIER.
# Relative paths are relative to the calling folder.
function(kraftonic_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT;BUNDLE_IDENTIFIER;COMPANY_NAME;AU_SUBTYPE;AU_MANUFACTURER;UIDESC;UI_GENERATOR;PARAMS_HEADER"
        "SOURCES;LINK;INCLUDE_DIRS" ${ARGN})
    if(NOT ARG_PRODUCT OR NOT ARG_SOURCES)
        message(FATAL_ERROR "kraftonic_add_plugin: PRODUCT and SOURCES are required")
    endif()
    if(NOT ARG_BUNDLE_IDENTIFIER)
        set(ARG_BUNDLE_IDENTIFIER "com.kraftonic.${ARG_PRODUCT}")
    endif()
    foreach(arg UIDESC UI_GENERATOR PARAMS_HEADER)
        if(ARG_${arg})
            get_filename_component(ARG_${arg} "${ARG_${arg}}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
    endforeach()

    # The bundle file name stays space-free: a space breaks the SDK's Windows
    # resource copy. Hosts show the traits' kPluginName instead.
    smtg_add_vst3plugin(${target} PACKAGE_NAME "${target}" SOURCES_LIST ${ARG_SOURCES})
    smtg_target_configure_version_file(${target}) # projectversion.h from VERSION
    target_include_directories(${target} PRIVATE ${ARG_INCLUDE_DIRS} "${CMAKE_CURRENT_BINARY_DIR}")
    # SDK headers are not clean at our warning level: include them as system headers.
    target_include_directories(${target} SYSTEM PRIVATE "${vst3sdk_SOURCE_DIR}" "${vst3sdk_SOURCE_DIR}/vstgui4")
    target_link_libraries(${target} PRIVATE kraftonic::vst3 ${ARG_LINK})

    if(ARG_UIDESC)
        # The panel: the generated .uidesc plus the kit bitmaps and fonts it uses.
        kraftonic_add_ui_kit_resources(${target} UIDESC "${ARG_UIDESC}")
        # ctest "ui: <id> panel": no overlaps, every parameter on the panel
        # once, and the committed files match a fresh run of the generator.
        kraftonic_add_panel_test(${ARG_PRODUCT} UIDESC "${ARG_UIDESC}"
            GENERATOR "${ARG_UI_GENERATOR}" PARAMS_HEADER "${ARG_PARAMS_HEADER}")
        if(ARG_UI_GENERATOR AND KRAFTONIC_UI_PYTHON)
            add_custom_target(${ARG_PRODUCT}_panel
                COMMAND "${CMAKE_COMMAND}" -E env "KRAFTONIC_UI_TOOLS=${KRAFTONIC_UI_TOOLS}"
                        "${KRAFTONIC_UI_PYTHON}" -B "${ARG_UI_GENERATOR}"
                COMMENT "Regenerating the ${ARG_PRODUCT} panel"
                VERBATIM)
        endif()
    endif()

    kraftonic_set_bundle_info(${target} ${ARG_PRODUCT} "${ARG_BUNDLE_IDENTIFIER}")
    if(SMTG_MAC)
        set(company)
        if(ARG_COMPANY_NAME)
            set(company COMPANY_NAME "${ARG_COMPANY_NAME}")
        endif()
        smtg_target_set_bundle(${target} BUNDLE_IDENTIFIER "${ARG_BUNDLE_IDENTIFIER}" ${company})
    endif()
    if(KRAFTONIC_BUILD_AU)
        if(ARG_AU_SUBTYPE AND ARG_AU_MANUFACTURER)
            kraftonic_add_audio_unit(${target} "${ARG_BUNDLE_IDENTIFIER}" "${ARG_AU_SUBTYPE}" "${ARG_AU_MANUFACTURER}")
        else()
            message(WARNING "${target}: no AU_SUBTYPE and AU_MANUFACTURER in kraftonic_add_plugin, so no Audio Unit")
        endif()
    endif()

    if(KRAFTONIC_BUILD_CLAP)
        kraftonic_add_clap(${target} "${ARG_BUNDLE_IDENTIFIER}"
            SOURCES ${ARG_SOURCES} LINK ${ARG_LINK} INCLUDE_DIRS ${ARG_INCLUDE_DIRS})
    endif()

    # Our warnings apply to our sources only, never to SDK sources. VST3
    # interfaces are COM-style (non-virtual destructors by design).
    set(ours ${ARG_SOURCES})
    list(FILTER ours INCLUDE REGEX "\\.(c|cc|cpp|cxx|m|mm)$")
    if(ours)
        kraftonic_set_source_warnings(${ours})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            set_property(SOURCE ${ours} APPEND PROPERTY COMPILE_OPTIONS "-Wno-non-virtual-dtor")
        endif()
    endif()
endfunction()

# kraftonic_add_audio_unit(<Target> <bundle identifier> <subtype> <manufacturer>)
# The Audio Unit (AUv2) of a VST3 plug-in, macOS only: the VST3 SDK's AU
# wrapper on Apple's AudioUnitSDK, with a copy of <Target>.vst3 inside
# (Contents/Resources/plugin.vst3). Target <Target>_AU writes
# <Target>.component next to the .vst3. The AU type (aumu instrument, aufx
# effect), name and vendor come from the VST3's moduleinfo.json; the subtype
# and manufacturer codes are the plug-in's permanent AU identity, like its FUIDs.
# The VST3 SDK's smtg_target_add_auv2 needs the Xcode generator; this works with any.
function(kraftonic_add_audio_unit target bundle_id subtype manufacturer)
    set(code "[A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9]")
    if(NOT subtype MATCHES "^${code}$" OR NOT manufacturer MATCHES "^${code}$" OR NOT manufacturer MATCHES "[A-Z]")
        message(FATAL_ERROR "${target}: AU_SUBTYPE and AU_MANUFACTURER must be 4 letters or digits, "
            "and AU_MANUFACTURER needs an upper-case letter (Apple reserves all-lower-case codes)")
    endif()
    set(wrapper "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auwrapper")
    set(au ${target}_auwrapper)
    add_library(${au} MODULE "${wrapper}/auwrapper.mm" "${wrapper}/aucocoaview.mm" "${wrapper}/NSDataIBStream.mm")
    # The editor's Objective-C class name must be unique in the host process.
    string(MAKE_C_IDENTIFIER "KraftonicAUView_${manufacturer}_${subtype}_${PROJECT_VERSION}" view_class)
    target_compile_definitions(${au} PRIVATE SMTG_AUWRAPPER_USES_AUSDK
        SMTG_AUCocoaUIBase_CLASS_NAME=${view_class} CA_USE_AUDIO_PLUGIN_ONLY=0)
    target_include_directories(${au} SYSTEM PRIVATE "${vst3sdk_SOURCE_DIR}")
    target_link_libraries(${au} PRIVATE sdk_hosting kraftonic_audiounitsdk
        "-framework AudioUnit" "-framework CoreMIDI" "-framework Cocoa")
    set_target_properties(${au} PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION component
        OUTPUT_NAME ${target}
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>")
    # Every build: copy the current .vst3 in and write the Info.plist (cheap).
    get_target_property(vst3_path ${target} SMTG_PLUGIN_PACKAGE_PATH)
    add_custom_target(${target}_AU ALL
        COMMAND "${CMAKE_COMMAND}" "-DVST3=${vst3_path}" "-DCOMPONENT=$<TARGET_BUNDLE_DIR:${au}>"
                "-DIDENTIFIER=${bundle_id}.audiounit" "-DVERSION=${PROJECT_VERSION}"
                "-DSUBTYPE=${subtype}" "-DMANUFACTURER=${manufacturer}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KraftonicAudioUnit.cmake"
        COMMENT "Assembling ${target}.component"
        VERBATIM)
    add_dependencies(${target}_AU ${au} ${target})
endfunction()

# kraftonic_add_clap(<Target> <bundle identifier> SOURCES <files>... [LINK <targets>...] [INCLUDE_DIRS <dirs>...])
# The CLAP plug-in: the product's plug-in sources built once more with
# KRAFTONIC_CLAP defined, so the entry file's KRAFTONIC_CLAP_ENTRY line
# (kraftonic/formats/clap/ClapEntry.h) becomes the CLAP entry. The VST3
# factory in the same file stays: the CLAP editor creates the product's VST3
# controller from it to show the panel. The CLAP id is the bundle identifier
# (permanent once shipped). Target <Target>_CLAP writes <Target>.clap next to
# the .vst3: a bundle on macOS (with the panel's resources, so the editor
# works), a single shared library on Windows and Linux (no editor: hosts show
# generic controls, see modules/formats/README.md).
function(kraftonic_add_clap target bundle_id)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK;INCLUDE_DIRS" ${ARGN})
    set(module ${target}_clapmodule)
    add_library(${module} MODULE ${ARG_SOURCES})
    # The VST3 SDK's module entry: VSTGUI's init and the platform module handle.
    smtg_target_add_library_main(${module})
    target_include_directories(${module} PRIVATE ${ARG_INCLUDE_DIRS} "${CMAKE_CURRENT_BINARY_DIR}")
    target_include_directories(${module} SYSTEM PRIVATE "${vst3sdk_SOURCE_DIR}" "${vst3sdk_SOURCE_DIR}/vstgui4")
    target_link_libraries(${module} PRIVATE kraftonic::clap kraftonic::vst3 ${ARG_LINK})
    set(gui 0)
    if(APPLE)
        set(gui 1)
    endif()
    target_compile_definitions(${module} PRIVATE KRAFTONIC_CLAP=1 KRAFTONIC_CLAP_GUI=${gui}
        "KRAFTONIC_CLAP_ID=\"${bundle_id}\"")
    set_target_properties(${module} PROPERTIES
        OUTPUT_NAME ${target}
        PREFIX ""
        SUFFIX ".clap"
        CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>")
    add_dependencies(${module} ${target}) # projectversion.h, and the panel files on macOS
    if(APPLE)
        set_target_properties(${module} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap SUFFIX "")
        # Every build: the panel files from the .vst3, the Info.plist, an ad hoc seal (cheap).
        get_target_property(vst3_path ${target} SMTG_PLUGIN_PACKAGE_PATH)
        add_custom_target(${target}_CLAP ALL
            COMMAND "${CMAKE_COMMAND}" "-DVST3=${vst3_path}" "-DCLAP=$<TARGET_BUNDLE_DIR:${module}>"
                    "-DIDENTIFIER=${bundle_id}.clap" "-DVERSION=${PROJECT_VERSION}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KraftonicClap.cmake"
            COMMENT "Assembling ${target}.clap"
            VERBATIM)
    else()
        add_custom_target(${target}_CLAP ALL)
    endif()
    add_dependencies(${target}_CLAP ${module} ${target})
endfunction()
