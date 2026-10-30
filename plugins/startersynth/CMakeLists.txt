# SPDX-License-Identifier: MIT
#
# Kraftonic Starter Synth: a small polyphonic synth that shows how a plug-in
# is built on the Kraftonic kit. Read README.md first.

# Per-product version from VERSION (bundle Info.plist, moduleinfo.json, factory).
include(KraftonicProductVersion)
kraftonic_product_version()

# The engine: plain C++ on kraftonic::core (envelope, smoothing, voices) and
# kraftonic::synth (oscillators, filters). No plug-in SDK, so it builds and tests in the fast
# DSP loop.
add_library(startersynth_dsp STATIC dsp/StarterSynthEngine.cpp)
target_include_directories(startersynth_dsp PUBLIC dsp)
target_link_libraries(startersynth_dsp PUBLIC kraftonic::core kraftonic::synth)
set_target_properties(startersynth_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
kraftonic_set_warnings(startersynth_dsp)

# The VST3 bundle: the engine plus the shared base (kraftonic::vst3, which
# brings the Steinberg SDK and VSTGUI along). kraftonic_add_plugin
# (cmake/KraftonicPlugin.cmake) also adds the panel, its kit bitmaps and fonts,
# the ctest "ui: startersynth panel" and the bundle info. It needs the SDK, so
# DSP-only builds skip it.
if(KRAFTONIC_BUILD_PLUGINS)
    kraftonic_add_plugin(KraftonicStarterSynth
        PRODUCT startersynth
        BUNDLE_IDENTIFIER "com.kraftonic.startersynth"
        COMPANY_NAME "Kraftonic"
        AU_SUBTYPE "Stsy"
        AU_MANUFACTURER "Kftc"
        SOURCES
            plugin/StarterSynthTraits.h
            plugin/StarterSynthValueText.h
            plugin/StarterSynthPresets.h
            plugin/StarterSynthEntry.cpp
        INCLUDE_DIRS plugin
        LINK startersynth_dsp
        UIDESC ui/startersynth.uidesc
        UI_GENERATOR ui/make_startersynth_uidesc.py
        PARAMS_HEADER dsp/StarterSynthParameters.h
    )
endif()

if(KRAFTONIC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
