// SPDX-License-Identifier: MIT
//
// Kraftonic Starter Synth VST3 factory and CLAP entry: the whole plug-in layer.
//
// The shared base supplies a complete processor and controller from the
// traits, so a product needs no VST3 code of its own. Write a controller
// subclass only when the plug-in needs custom behaviour (an effect, for
// example, turns off the MIDI mapping in a small subclass).
// "VST" is a Steinberg trademark; used here only to name the SDK format.

#include "StarterSynthTraits.h"

#include <kraftonic/formats/clap/ClapEntry.h>
#include <kraftonic/formats/vst3/Vst3Controller.h>
#include <kraftonic/formats/vst3/Vst3Factory.h>
#include <kraftonic/formats/vst3/Vst3Processor.h>

#include "projectversion.h" // generated from VERSION: FULL_VERSION_STR

namespace startersynth::plugin
{
using Processor = kraftonic::formats::vst3::Processor<StarterSynthTraits>;
using Controller = kraftonic::formats::vst3::Controller<StarterSynthTraits>;
} // namespace startersynth::plugin

KRAFTONIC_VST3_FACTORY (startersynth::plugin::StarterSynthTraits,
                        startersynth::plugin::Processor,
                        startersynth::plugin::Controller,
                        FULL_VERSION_STR,
                        Steinberg::Vst::PlugType::kInstrumentSynth)

// The CLAP plug-in (built from this file by kraftonic_add_plugin; empty in the VST3 build).
KRAFTONIC_CLAP_ENTRY (startersynth::plugin::StarterSynthTraits, FULL_VERSION_STR,
                     CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                     CLAP_PLUGIN_FEATURE_STEREO)
