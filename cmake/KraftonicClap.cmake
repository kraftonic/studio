# SPDX-License-Identifier: MIT
#
# Build step of a macOS CLAP bundle (see kraftonic_add_clap in
# KraftonicPlugin.cmake): copies the panel files (the .uidesc, kit bitmaps
# and fonts) from the built .vst3's Contents/Resources, writes the bundle's
# Info.plist and seals the bundle ad hoc.
#
# cmake -DVST3=<x.vst3> -DCLAP=<x.clap> -DIDENTIFIER=<bundle id> -DVERSION=<x.y.z> -P KraftonicClap.cmake
foreach(var VST3 CLAP IDENTIFIER VERSION)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "KraftonicClap.cmake: ${var} not set")
    endif()
endforeach()

set(resources "${CLAP}/Contents/Resources")
file(REMOVE_RECURSE "${resources}")
file(MAKE_DIRECTORY "${resources}")
file(GLOB entries "${VST3}/Contents/Resources/*")
foreach(entry ${entries})
    get_filename_component(name "${entry}" NAME)
    if(name STREQUAL "moduleinfo.json") # VST3 only
        continue()
    endif()
    file(COPY "${entry}" DESTINATION "${resources}")
endforeach()

get_filename_component(executable "${CLAP}" NAME_WE)
file(WRITE "${CLAP}/Contents/Info.plist" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>${executable}</string>
	<key>CFBundleIdentifier</key>
	<string>${IDENTIFIER}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>${executable}</string>
	<key>CFBundlePackageType</key>
	<string>BNDL</string>
	<key>CFBundleShortVersionString</key>
	<string>${VERSION}</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>${VERSION}</string>
</dict>
</plist>
")

execute_process(COMMAND codesign --force --sign - "${CLAP}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE log)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "codesign failed for ${CLAP}: ${log}")
endif()
