// SPDX-License-Identifier: MIT
//
// Self-test plug-in factory. Test-only.
// "VST" is a Steinberg trademark; used here only to name the SDK format.

#include "SelfTestPlugin.h"
#include "kraftonic/formats/vst3/Vst3Factory.h"

KRAFTONIC_VST3_FACTORY (kraftonic::formats::selftest::SelfTestTraits,
                        kraftonic::formats::selftest::SelfTestProcessor,
                        kraftonic::formats::selftest::SelfTestController,
                        "1.0.0",
                        Steinberg::Vst::PlugType::kInstrumentSynth)
