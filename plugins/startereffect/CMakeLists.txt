# SPDX-License-Identifier: MIT
#
# Kraftonic Starter Effect: a small stereo drive + delay that shows how an
# effect is built on the Kraftonic kit. Read README.md first.

# Per-product version from VERSION (bundle Info.plist, moduleinfo.json, factory).
include(KraftonicProductVersion)
kraftonic_product_version()

# The engine: plain C++ on kraftonic::fx (drive, delay) and kraftonic::core
# (smoothing). No plug-in SDK, so it builds and tests in the fast
# DSP loop.
add_library(startereffect_dsp STATIC dsp/StarterEffectEngine.cpp)
target_include_directories(startereffect_dsp PUBLIC dsp)
target_link_libraries(startereffect_dsp PUBLIC kraftonic::core kraftonic::fx)
set_target_properties(startereffect_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
kraftonic_set_warnings(startereffect_dsp)

# The VST3 bundle: the engine plus the shared base (kraftonic::vst3, which
# brings the Steinberg SDK and VSTGUI along). kraftonic_add_plugin
# (cmake/KraftonicPlugin.cmake) also adds the panel, its kit bitmaps and fonts,
# the ctest "ui: startereffect panel" and the bundle info. It needs the SDK, so
# DSP-only builds skip it.
if(KRAFTONIC_BUILD_PLUGINS)
    kraftonic_add_plugin(KraftonicStarterEffect
        PRODUCT startereffect
        BUNDLE_IDENTIFIER "com.kraftonic.startereffect"
        COMPANY_NAME "Kraftonic"
        AU_SUBTYPE "Stfx"
        AU_MANUFACTURER "Kftc"
        SOURCES
            plugin/StarterEffectTraits.h
            plugin/StarterEffectValueText.h
            plugin/StarterEffectPresets.h
            plugin/StarterEffectEntry.cpp
        INCLUDE_DIRS plugin
        LINK startereffect_dsp
        UIDESC ui/startereffect.uidesc
        UI_GENERATOR ui/make_startereffect_uidesc.py
        PARAMS_HEADER dsp/StarterEffectParameters.h
    )
endif()

if(KRAFTONIC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
