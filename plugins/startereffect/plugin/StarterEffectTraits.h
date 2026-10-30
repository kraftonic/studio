// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Effect — product traits for the shared VST3 base.
//
//  This struct is the only thing the plug-in layer needs from a product. The
//  shared effect processor (modules/formats, vst3/effect/) and controller
//  turn it into a complete VST3 effect: stereo buses, sample-accurate
//  automation, click-free bypass, state save/load, the preset list and the
//  editor. See modules/formats/README.md for every member.
//
//  SDK-free, so tests/StarterEffectPluginTests.cpp checks state, presets and
//  value text without a host.
// ============================================================================

#include "StarterEffectPresets.h"
#include "StarterEffectEngine.h"
#include "StarterEffectParameters.h"
#include "StarterEffectValueText.h"

#include <kraftonic/formats/vst3/AdapterIds.h>
#include <kraftonic/formats/vst3/effect/EffectTraits.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace startereffect::plugin
{
namespace ids = kraftonic::formats::vst3;

/** The base checks traits with the instrument-shaped contract; the adapter
    adds a process (block, events, count) that ignores the (absent) events. */
using StarterEffectHostEngine = kraftonic::formats::vst3::effect::EffectEngineAdapter<StarterEffectEngine>;

struct StarterEffectTraits
{
    // --- The contract: parameter table, parameter struct, engine ------------
    using ParamId = startereffect::ParamId;
    using Params = StarterEffectParameters;
    using Engine = StarterEffectHostEngine;
    static constexpr int kNumParams = startereffect::kNumParams;

    static const ParamInfo* paramTable() noexcept { return kParamInfo.data(); }
    static const ParamInfo& paramInfo (ParamId id) noexcept { return startereffect::paramInfo (id); }
    static float toPlain (ParamId id, double normalized) noexcept { return startereffect::toPlain (id, normalized); }
    static double toNormalized (ParamId id, float plain) noexcept { return startereffect::toNormalized (id, plain); }

    // --- Identity: PERMANENT once released ----------------------------------
    // Hosts find the plug-in in saved songs by these class IDs, and the state
    // codec refuses data whose magic is not this product's. Generate new ones
    // for a new product (scripts/new-product.sh does) and never change them
    // after a release. Bump kStateVersion only with a migration.
    static constexpr std::uint32_t kStateMagic = 0xF34B0B0Au;
    static constexpr std::uint32_t kStateVersion = 1u;
    static constexpr std::uint32_t kProcessorFuid[4] = {0x2D076510u, 0x591F4E94u, 0x995EED69u, 0xDEED0BDAu};
    static constexpr std::uint32_t kControllerFuid[4] = {0x7EFC3816u, 0x4F814147u, 0xB7D800ECu, 0xCA03C2BBu};

    // --- Names hosts display -------------------------------------------------
    static constexpr const char* kVendor = "Kraftonic";
    static constexpr const char* kPluginName = "Kraftonic Starter Effect";
    static constexpr const char* kControllerName = "Kraftonic Starter Effect Controller";
    static constexpr const char* kVendorUrl = "https://github.com/kraftonic/studio";
    static constexpr const char* kVendorEmail = "";

    // --- Factory presets ------------------------------------------------------
    static std::size_t numPresets() noexcept { return kNumPresets; }
    static const FactoryPreset* presets() noexcept { return kPresets; }
    static constexpr const char* kProgramListName = "Factory Presets";

    // --- Hidden host-side parameters: the shared defaults ---------------------
    // (The MIDI ones are unused by an effect; the base still declares them, hidden.)
    static constexpr std::uint32_t kSustainParamId = ids::kDefaultSustainParamId;
    static constexpr std::uint32_t kAllNotesOffParamId = ids::kDefaultAllNotesOffParamId;
    static constexpr std::uint32_t kAllSoundOffParamId = ids::kDefaultAllSoundOffParamId;
    static constexpr std::uint32_t kModWheelParamId = ids::kDefaultModWheelParamId;
    static constexpr std::uint32_t kPitchBendParamId = ids::kDefaultPitchBendParamId;
    static constexpr std::uint32_t kProgramParamId = ids::kDefaultProgramParamId;
    static constexpr std::uint32_t kBypassParamId = ids::kDefaultBypassParamId;

    // --- Editor: the generated panel in ui/ -----------------------------------
    static constexpr const char* kUidescFile = "startereffect.uidesc";
    static constexpr const char* kViewName = "view";

    // --- Text and tail ----------------------------------------------------------
    static void formatPlainValue (ParamId id, float plain, char* text, std::size_t size) noexcept
    {
        plugin::formatPlainValue (id, plain, text, size);
    }

    static bool parsePlainValue (ParamId id, const char* text, float& plain) noexcept
    {
        return plugin::parsePlainValue (id, text, plain);
    }

    /** How long the echoes ring after the input stops: repeats until they
        fall below -60 dB (each one is `feedback` times the last), capped. */
    static double tailSeconds (const Params& params) noexcept
    {
        const double time = params.get (ParamId::delayTime) / 1000.0;
        const double feedback = params.get (ParamId::feedback);
        const double repeats = feedback > 0.001 ? std::log (0.001) / std::log (feedback) : 0.0;
        return std::min (time * (repeats + 1.0) + 0.1, 60.0);
    }
};

} // namespace startereffect::plugin
