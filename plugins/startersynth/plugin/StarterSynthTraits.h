// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Synth — product traits for the shared VST3 base.
//
//  This struct is the only thing the plug-in layer needs from a product. The
//  base (modules/formats, kraftonic::vst3) turns it into a complete
//  VST3 processor and controller: MIDI to events, parameters, automation,
//  state save/load, the preset list and the editor. See
//  modules/formats/README.md for every member.
//
//  SDK-free, so tests/StarterSynthPluginTests.cpp checks state, presets and
//  value text without a host.
// ============================================================================

#include "StarterSynthPresets.h"
#include "StarterSynthEngine.h"
#include "StarterSynthParameters.h"
#include "StarterSynthValueText.h"

#include <kraftonic/formats/vst3/AdapterIds.h>

#include <cstddef>
#include <cstdint>

namespace startersynth::plugin
{
namespace ids = kraftonic::formats::vst3;

struct StarterSynthTraits
{
    // --- The contract: parameter table, parameter struct, engine ------------
    using ParamId = startersynth::ParamId;
    using Params = StarterSynthParameters;
    using Engine = StarterSynthEngine;
    static constexpr int kNumParams = startersynth::kNumParams;

    static const ParamInfo* paramTable() noexcept { return kParamInfo.data(); }
    static const ParamInfo& paramInfo (ParamId id) noexcept { return startersynth::paramInfo (id); }
    static float toPlain (ParamId id, double normalized) noexcept { return startersynth::toPlain (id, normalized); }
    static double toNormalized (ParamId id, float plain) noexcept { return startersynth::toNormalized (id, plain); }

    // --- Identity: PERMANENT once released ----------------------------------
    // Hosts find the plug-in in saved songs by these class IDs, and the state
    // codec refuses data whose magic is not this product's. Generate new ones
    // for a new product (scripts/new-product.sh does) and never change them
    // after a release. Bump kStateVersion only with a migration.
    static constexpr std::uint32_t kStateMagic = 0xF32A8CEAu;
    static constexpr std::uint32_t kStateVersion = 1u;
    static constexpr std::uint32_t kProcessorFuid[4] = {0x3A5EC4BFu, 0xDBAC45B7u, 0x9C8EF4DFu, 0x961D0DDBu};
    static constexpr std::uint32_t kControllerFuid[4] = {0x9547887Cu, 0x528C4A46u, 0xABEA87F7u, 0xF626092Du};

    // --- Names hosts display -------------------------------------------------
    static constexpr const char* kVendor = "Kraftonic";
    static constexpr const char* kPluginName = "Kraftonic Starter Synth";
    static constexpr const char* kControllerName = "Kraftonic Starter Synth Controller";
    static constexpr const char* kVendorUrl = "https://github.com/kraftonic/studio";
    static constexpr const char* kVendorEmail = "";

    // --- Factory presets ------------------------------------------------------
    static std::size_t numPresets() noexcept { return kNumPresets; }
    static const FactoryPreset* presets() noexcept { return kPresets; }
    static constexpr const char* kProgramListName = "Factory Presets";

    // --- Hidden host-side parameters: the shared defaults ---------------------
    static constexpr std::uint32_t kSustainParamId = ids::kDefaultSustainParamId;
    static constexpr std::uint32_t kAllNotesOffParamId = ids::kDefaultAllNotesOffParamId;
    static constexpr std::uint32_t kAllSoundOffParamId = ids::kDefaultAllSoundOffParamId;
    static constexpr std::uint32_t kModWheelParamId = ids::kDefaultModWheelParamId;
    static constexpr std::uint32_t kPitchBendParamId = ids::kDefaultPitchBendParamId;
    static constexpr std::uint32_t kProgramParamId = ids::kDefaultProgramParamId;
    static constexpr std::uint32_t kBypassParamId = ids::kDefaultBypassParamId;

    // --- Editor: the generated panel in ui/ -----------------------------------
    static constexpr const char* kUidescFile = "startersynth.uidesc";
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

    /** How long the plug-in keeps sounding after the last note-off: the
        release (its exponential tail needs about 2x) plus smoothing margin. */
    static double tailSeconds (const Params& params) noexcept
    {
        return 2.0 * static_cast<double> (params.get (ParamId::release)) + 0.1;
    }
};

} // namespace startersynth::plugin
