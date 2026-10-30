// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — VST3 factory registration.
//
//  One macro that registers a product's processor + controller pair built on
//  the shared base. All strings and IDs come from the product traits, so the
//  factory entry is four lines:
//
//      #include "kraftonic/formats/vst3/Vst3Factory.h"
//      KRAFTONIC_VST3_FACTORY (MyTraits, MyProcessor, MyController,
//                              "1.0.0", Steinberg::Vst::PlugType::kInstrumentSynth)
//
//  "VST" is a Steinberg trademark; used here only to name the SDK format.
// ============================================================================

#include "kraftonic/formats/vst3/Vst3SdkIncludes.h"

// Note: kVstAudioEffectClass, kVstVersionString and
// kVstComponentControllerClass are SDK macros expanding to literals, so they
// must stay unqualified here (they are visible through Vst3SdkIncludes.h).
#define KRAFTONIC_VST3_FACTORY(Traits, ProcessorClass, ControllerClass, \
                               VersionString, SubCategory) \
BEGIN_FACTORY_DEF (Traits::kVendor, Traits::kVendorUrl, Traits::kVendorEmail) \
DEF_CLASS2 (INLINE_UID_FROM_FUID (ProcessorClass::cid), \
            Steinberg::PClassInfo::kManyInstances, \
            kVstAudioEffectClass, \
            Traits::kPluginName, \
            Steinberg::Vst::kDistributable, \
            SubCategory, \
            VersionString, \
            kVstVersionString, \
            ProcessorClass::createInstance) \
DEF_CLASS2 (INLINE_UID_FROM_FUID (ControllerClass::cid), \
            Steinberg::PClassInfo::kManyInstances, \
            kVstComponentControllerClass, \
            Traits::kControllerName, \
            0, \
            "", \
            VersionString, \
            kVstVersionString, \
            ControllerClass::createInstance) \
END_FACTORY
