# SPDX-License-Identifier: MIT
#
# Build step of an Audio Unit (see kraftonic_add_audio_unit in
# KraftonicPlugin.cmake): copies the built .vst3 into the .component as
# Contents/Resources/plugin.vst3 (where the VST3 SDK's AU wrapper loads it),
# writes the component's Info.plist and seals the bundle ad hoc.
#
# The AU type, name and vendor come from the VST3's moduleinfo.json, so the
# plug-in's traits stay the one source of them: a VST3 class in the
# "Instrument" category is a music device (aumu), anything else an effect (aufx).
#
# cmake -DVST3=<x.vst3> -DCOMPONENT=<x.component> -DIDENTIFIER=<bundle id>
#       -DVERSION=<x.y.z> -DSUBTYPE=<code> -DMANUFACTURER=<Code> -P KraftonicAudioUnit.cmake
foreach(var VST3 COMPONENT IDENTIFIER VERSION SUBTYPE MANUFACTURER)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "KraftonicAudioUnit.cmake: ${var} not set")
    endif()
endforeach()

file(READ "${VST3}/Contents/Resources/moduleinfo.json" json)
string(JSON vendor GET "${json}" "Factory Info" "Vendor")
string(JSON count LENGTH "${json}" "Classes")
math(EXPR last "${count} - 1")
set(name "")
foreach(i RANGE ${last})
    string(JSON category GET "${json}" "Classes" ${i} "Category")
    if(category STREQUAL "Audio Module Class")
        string(JSON name GET "${json}" "Classes" ${i} "Name")
        string(JSON subcategories GET "${json}" "Classes" ${i} "Sub Categories")
        break()
    endif()
endforeach()
if(name STREQUAL "")
    message(FATAL_ERROR "${VST3}: no audio module class in moduleinfo.json")
endif()
# The channel layouts the shared VST3 base accepts: an instrument has a stereo
# output; an effect a stereo or mono input and a stereo output (BusLayout.h).
set(type aufx)
set(channels "2 2;1 2")
if(subcategories MATCHES "\"Instrument\"")
    set(type aumu)
    set(channels "0 2")
endif()
set(channel_plist "")
foreach(pair ${channels})
    string(REPLACE " " ";" pair "${pair}")
    list(GET pair 0 inputs)
    list(GET pair 1 outputs)
    string(APPEND channel_plist "\t\t<dict>\n\t\t\t<key>Inputs</key>\n\t\t\t<string>${inputs}</string>\n"
        "\t\t\t<key>Outputs</key>\n\t\t\t<string>${outputs}</string>\n\t\t</dict>\n")
endforeach()

# Hosts list "Vendor: Name"; drop the vendor from the start of the name.
string(FIND "${name}" "${vendor} " at)
if(at EQUAL 0)
    string(LENGTH "${vendor} " skip)
    string(SUBSTRING "${name}" ${skip} -1 name)
endif()
foreach(var vendor name)
    string(REPLACE "&" "&amp;" ${var} "${${var}}")
    string(REPLACE "<" "&lt;" ${var} "${${var}}")
    string(REPLACE ">" "&gt;" ${var} "${${var}}")
endforeach()
string(REPLACE "." ";" parts "${VERSION}.0.0")
list(GET parts 0 major)
list(GET parts 1 minor)
list(GET parts 2 patch)
math(EXPR au_version "${major} * 65536 + ${minor} * 256 + ${patch}")
get_filename_component(executable "${COMPONENT}" NAME_WE)

set(resources "${COMPONENT}/Contents/Resources")
file(REMOVE_RECURSE "${resources}/plugin.vst3")
file(MAKE_DIRECTORY "${resources}")
execute_process(COMMAND cp -R "${VST3}" "${resources}/plugin.vst3" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "could not copy ${VST3} into ${COMPONENT}")
endif()

file(WRITE "${COMPONENT}/Contents/Info.plist" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
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
	<string>${name}</string>
	<key>CFBundlePackageType</key>
	<string>BNDL</string>
	<key>CFBundleShortVersionString</key>
	<string>${VERSION}</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>${VERSION}</string>
	<key>AudioComponents</key>
	<array>
		<dict>
			<key>type</key>
			<string>${type}</string>
			<key>subtype</key>
			<string>${SUBTYPE}</string>
			<key>manufacturer</key>
			<string>${MANUFACTURER}</string>
			<key>name</key>
			<string>${vendor}: ${name}</string>
			<key>description</key>
			<string>${name}</string>
			<key>version</key>
			<integer>${au_version}</integer>
			<key>factoryFunction</key>
			<string>AUWrapperFactory</string>
		</dict>
	</array>
	<key>AudioUnit SupportedNumChannels</key>
	<array>
${channel_plist}	</array>
</dict>
</plist>
")

execute_process(COMMAND codesign --force --sign - "${COMPONENT}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE log)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "codesign failed for ${COMPONENT}: ${log}")
endif()
