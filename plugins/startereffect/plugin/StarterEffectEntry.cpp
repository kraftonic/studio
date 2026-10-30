// SPDX-License-Identifier: MIT
//
// Kraftonic Starter Effect VST3 factory and CLAP entry: the whole plug-in layer.
//
// The shared effect processor and controller come from the traits. The one
// thing an effect changes is the controller's MIDI mapping: the base maps
// sustain, mod wheel and pitch bend for instruments, but an effect has no
// event input. This small subclass is also the place to add editor
// behaviour later (createSubController for custom panel views).
// "VST" is a Steinberg trademark; used here only to name the SDK format.

#include "StarterEffectTraits.h"

#include <kraftonic/formats/clap/ClapEntry.h>
#include <kraftonic/formats/vst3/Vst3Controller.h>
#include <kraftonic/formats/vst3/Vst3Factory.h>
#include <kraftonic/formats/vst3/effect/Vst3EffectProcessor.h>

#include "projectversion.h" // generated from VERSION: FULL_VERSION_STR

namespace startereffect::plugin
{
using Processor = kraftonic::formats::vst3::effect::EffectProcessor<StarterEffectTraits>;

class Controller : public kraftonic::formats::vst3::Controller<StarterEffectTraits>
{
public:
    static Steinberg::FUnknown* createInstance (void* /*context*/)
    {
        return static_cast<Steinberg::Vst::IEditController*> (new Controller());
    }

    Steinberg::tresult PLUGIN_API getMidiControllerAssignment (Steinberg::int32 /*busIndex*/,
                                                               Steinberg::int16 /*channel*/,
                                                               Steinberg::Vst::CtrlNumber /*number*/,
                                                               Steinberg::Vst::ParamID& /*tag*/) SMTG_OVERRIDE
    {
        return Steinberg::kResultFalse; // an effect takes no MIDI
    }
};
} // namespace startereffect::plugin

KRAFTONIC_VST3_FACTORY (startereffect::plugin::StarterEffectTraits,
                        startereffect::plugin::Processor,
                        startereffect::plugin::Controller,
                        FULL_VERSION_STR,
                        Steinberg::Vst::PlugType::kFxDelay)

// The CLAP plug-in (built from this file by kraftonic_add_plugin; empty in the VST3 build).
KRAFTONIC_CLAP_ENTRY (startereffect::plugin::StarterEffectTraits, FULL_VERSION_STR,
                     CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY,
                     CLAP_PLUGIN_FEATURE_STEREO)
