// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin self-test — product traits.
//
//  Minimal traits struct showing how a product instantiates the shared VST3
//  base: contract binding, identity, presets, state magic, formatting hook.
//  Test-only; never installed or shipped.
// ============================================================================

#include "SelfTestEngine.h"
#include "kraftonic/formats/Preset.h"
#include "kraftonic/formats/vst3/AdapterIds.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace kraftonic::formats::selftest
{

struct SelfTestTraits
{
    // Contract binding.
    using ParamId = selftest::ParamId;
    using Params = SelfTestParameters;
    using Engine = SelfTestEngine;
    static constexpr int kNumParams = selftest::kNumParams;

    static const ParamInfo* paramTable() noexcept
    {
        return kParamInfo.data();
    }

    static const ParamInfo& paramInfo (ParamId id) noexcept
    {
        return selftest::paramInfo (id);
    }

    static float toPlain (ParamId id, double normalized) noexcept
    {
        return selftest::toPlain (id, normalized);
    }

    static double toNormalized (ParamId id, float plain) noexcept
    {
        return selftest::toNormalized (id, plain);
    }

    // State identity (own magic; the shared v1 layout).
    static constexpr std::uint32_t kStateMagic = 0x5453544Bu; // 'KTST' little-endian
    static constexpr std::uint32_t kStateVersion = 1u;

    // Factory presets (plain values by stable key; ASCII names only).
    static std::size_t numPresets() noexcept
    {
        return 2;
    }

    static const FactoryPreset* presets() noexcept
    {
        static constexpr PresetValue init[] = {
            {"outputGain", -6.0F}, {"wave", 0.0F}, {"attack", 0.01F}};
        static constexpr PresetValue square[] = {
            {"outputGain", -9.0F}, {"wave", 1.0F}, {"attack", 0.03F}};
        static constexpr FactoryPreset table[2] {
            {"Init", init, 3},
            {"Soft Square", square, 3},
        };
        return table;
    }

    // Plug-in identity (PERMANENT: generated once via uuidgen, never change).
    static constexpr std::uint32_t kProcessorFuid[4]
        = {0xDB34F340u, 0xFA6E42D8u, 0x8DDE9E35u, 0x7C92830Bu};
    static constexpr std::uint32_t kControllerFuid[4]
        = {0x2C943503u, 0x487D4257u, 0xA66CD4D1u, 0xBC19B070u};
    static constexpr const char* kVendor = "Kraftonic";
    static constexpr const char* kPluginName = "Kraftonic Plugin SelfTest";
    static constexpr const char* kControllerName = "Kraftonic Plugin SelfTest Controller";
    static constexpr const char* kVendorUrl = "https://github.com/kraftonic/studio";
    static constexpr const char* kVendorEmail = "";

    // Adapter-local IDs (shared defaults) and program list.
    static constexpr std::uint32_t kSustainParamId = vst3::kDefaultSustainParamId;
    static constexpr std::uint32_t kAllNotesOffParamId = vst3::kDefaultAllNotesOffParamId;
    static constexpr std::uint32_t kAllSoundOffParamId = vst3::kDefaultAllSoundOffParamId;
    static constexpr std::uint32_t kModWheelParamId = vst3::kDefaultModWheelParamId;
    static constexpr std::uint32_t kPitchBendParamId = vst3::kDefaultPitchBendParamId;
    static constexpr std::uint32_t kProgramParamId = vst3::kDefaultProgramParamId;
    static constexpr std::uint32_t kBypassParamId = vst3::kDefaultBypassParamId;
    static constexpr const char* kProgramListName = "Factory Presets";

    // Output-meter path (HasOutputMeterIds): the shared processor emits
    // stereo peaks at ~30 Hz and the controller caches them for UI polling.
    static constexpr std::uint32_t kMeterLevelParamIdL = vst3::kDefaultMeterLevelParamIdL;
    static constexpr std::uint32_t kMeterLevelParamIdR = vst3::kDefaultMeterLevelParamIdR;

    // On-screen keyboard path (HasUiKeyboard): gallery keys play the engine
    // and MIDI-held notes light the keys.
    static constexpr std::uint32_t kHeldNotesParamIdBase = vst3::kDefaultHeldNotesParamIdBase;

    // Minimal stock-VSTGUI editor used to exercise parameter binding.
    static constexpr const char* kUidescFile = "selftest.uidesc";
    static constexpr const char* kViewName = "view";

    // Value formatting hook: stepped wave renders as names (the discrete
    // string list), gain as dB, attack as ms/s.
    static void formatPlainValue (ParamId id, float plain, char* text, std::size_t size) noexcept
    {
        if (text == nullptr || size == 0)
            return;
        text[0] = '\0';
        if (id == ParamId::wave)
        {
            const int step = static_cast<int> (plain + 0.5F);
            const char* name = "Sine";
            if (step == 1)
                name = "Square";
            else if (step >= 2)
                name = "Triangle";
            std::snprintf (text, size, "%s", name);
        }
        else if (id == ParamId::outputGain)
        {
            std::snprintf (text, size, "%.1f dB", static_cast<double> (plain));
        }
        else
        {
            const double v = static_cast<double> (plain);
            if (v >= 1.0)
                std::snprintf (text, size, "%.2f s", v);
            else
                std::snprintf (text, size, "%.2f ms", v * 1000.0);
        }
    }

    static bool parsePlainValue (ParamId id, const char* text, float& plain) noexcept
    {
        if (text == nullptr)
            return false;
        while (*text == ' ' || *text == '\t')
            ++text;
        if (id == ParamId::wave)
        {
            if (std::strcmp (text, "Sine") == 0)
                plain = 0.0F;
            else if (std::strcmp (text, "Square") == 0)
                plain = 1.0F;
            else if (std::strcmp (text, "Triangle") == 0)
                plain = 2.0F;
            else
            {
                char* end = nullptr;
                const double number = std::strtod (text, &end);
                if (end == text || !std::isfinite (number))
                    return false;
                plain = static_cast<float> (number);
            }
            return std::isfinite (plain);
        }
        char* end = nullptr;
        const double number = std::strtod (text, &end);
        if (end == text || !std::isfinite (number))
            return false;
        while (*end == ' ' || *end == '\t')
            ++end;
        double scale = 1.0;
        if (*end == '\0')
        {
            scale = 1.0; // bare number in plain units
        }
        else if (*end == 'm' && end[1] == 's')
        {
            scale = 0.001;
        }
        else if ((*end == 's' || *end == 'd') && id != ParamId::outputGain)
        {
            scale = 1.0;
        }
        else if (*end == 'd' && end[1] == 'B' && id == ParamId::outputGain)
        {
            scale = 1.0;
        }
        else
        {
            return false;
        }
        plain = static_cast<float> (number * scale);
        return std::isfinite (plain);
    }

    // Tail: attack ramp plus the fixed 0.15 s release, with margin.
    static double tailSeconds (const Params& params) noexcept
    {
        return static_cast<double> (params.get (ParamId::attack)) + 0.25;
    }
};

} // namespace kraftonic::formats::selftest
