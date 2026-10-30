// SPDX-License-Identifier: MIT
#pragma once

// A fake effect for the adapter tests (VST3 and CLAP), SDK-free: a two-
// parameter contract and an engine that delays its input by kFakeLatency
// and adds trim * (mode + 1) as DC, so both timing and parameter arrival
// are visible in the output.

#include "kraftonic/core/Instrument.h"
#include "kraftonic/core/Parameters.h"
#include "kraftonic/formats/Preset.h"
#include "kraftonic/formats/vst3/AdapterIds.h"
#include "kraftonic/formats/vst3/effect/EffectTraits.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace fakeeffect
{
using kraftonic::core::AudioBlock;
using kraftonic::core::Event;
using kraftonic::core::ParamScale;
using kraftonic::core::ProcessSetup;

enum class ParamId : std::uint32_t
{
    trim = 0, // continuous gain in dB, applied as DC so automation is visible
    mode = 1, // stepped 0..2, scales the DC (proves stepped automation)
    count
};

constexpr int kNumParams = static_cast<int> (ParamId::count);
using ParamInfo = kraftonic::core::ParamInfo<ParamId>;

constexpr std::array<ParamInfo, kNumParams> kParamInfo {{
    {ParamId::trim, "trim", "Trim", "dB", -12.0F, 12.0F, 0.0F, ParamScale::linear, 0},
    {ParamId::mode, "mode", "Mode", "", 0.0F, 2.0F, 0.0F, ParamScale::linear, 2},
}};

inline const ParamInfo& paramInfo (ParamId id) noexcept
{
    return kParamInfo[static_cast<std::size_t> (id)];
}

inline float toPlain (ParamId id, double normalized) noexcept
{
    return kraftonic::core::toPlain (paramInfo (id), normalized);
}

inline double toNormalized (ParamId id, float plain) noexcept
{
    return kraftonic::core::toNormalized (paramInfo (id), plain);
}

struct FakeEffectParameters
{
    std::array<float, kNumParams> values {};

    static FakeEffectParameters defaults() noexcept
    {
        FakeEffectParameters p {};
        for (const auto& info : kParamInfo)
            p.values[static_cast<std::size_t> (info.id)] = info.defaultValue;
        return p;
    }

    float get (ParamId id) const noexcept { return values[static_cast<std::size_t> (id)]; }

    void set (ParamId id, float plain) noexcept
    {
        values[static_cast<std::size_t> (id)]
            = kraftonic::core::clampPlain (paramInfo (id), plain);
    }
};

constexpr int kFakeLatency = 16;

/** Delays the input by kFakeLatency and adds trim * (mode + 1) as DC, so
    both timing and parameter arrival are visible in the output. */
class FakeEffectEngine
{
public:
    void prepare (const ProcessSetup&) { reset(); }

    void reset() noexcept
    {
        for (auto& s : ringL_)
            s = 0.0F;
        for (auto& s : ringR_)
            s = 0.0F;
        pos_ = 0;
    }

    void setParameters (const FakeEffectParameters& p) noexcept
    {
        dc_ = p.get (ParamId::trim) * (p.get (ParamId::mode) + 1.0F);
    }

    void process (const AudioBlock& block) noexcept
    {
        for (int i = 0; i < block.numSamples; ++i)
        {
            const float delayedL = ringL_[pos_];
            const float delayedR = ringR_[pos_];
            ringL_[pos_] = block.left[i];
            ringR_[pos_] = block.right[i];
            pos_ = (pos_ + 1) % kFakeLatency;
            block.left[i] = delayedL + dc_;
            block.right[i] = delayedR + dc_;
        }
    }

    int latencySamples() const noexcept { return kFakeLatency; }

private:
    float ringL_[kFakeLatency] {};
    float ringR_[kFakeLatency] {};
    int pos_ = 0;
    float dc_ = 0.0F;
};

struct FakeEffectTraits
{
    using ParamId = fakeeffect::ParamId;
    using Params = FakeEffectParameters;
    using Engine = kraftonic::formats::vst3::effect::EffectEngineAdapter<FakeEffectEngine>;
    static constexpr int kNumParams = fakeeffect::kNumParams;

    static const ParamInfo* paramTable() noexcept { return kParamInfo.data(); }
    static const ParamInfo& paramInfo (ParamId id) noexcept { return fakeeffect::paramInfo (id); }
    static float toPlain (ParamId id, double n) noexcept { return fakeeffect::toPlain (id, n); }
    static double toNormalized (ParamId id, float p) noexcept { return fakeeffect::toNormalized (id, p); }

    static constexpr std::uint32_t kStateMagic = 0x46415854u; // 'TXAF'
    static constexpr std::uint32_t kStateVersion = 1u;

    static std::size_t numPresets() noexcept { return 1; }
    static const kraftonic::formats::FactoryPreset* presets() noexcept
    {
        static constexpr kraftonic::formats::PresetValue loud[] = {{"trim", 6.0F}};
        static constexpr kraftonic::formats::FactoryPreset table[1] {{"Loud", loud, 1}};
        return table;
    }

    static constexpr std::uint32_t kProcessorFuid[4] = {0x11111111u, 0x22222222u, 0x33333333u,
                                                        0x44444444u};
    static constexpr std::uint32_t kControllerFuid[4] = {0x55555555u, 0x66666666u, 0x77777777u,
                                                         0x88888888u};
    static constexpr const char* kVendor = "Kraftonic";
    static constexpr const char* kPluginName = "Kraftonic Effect Test";
    static constexpr const char* kControllerName = "Kraftonic Effect Test Controller";
    static constexpr const char* kVendorUrl = "https://github.com/kraftonic/studio";
    static constexpr const char* kVendorEmail = "";

    static constexpr std::uint32_t kSustainParamId
        = kraftonic::formats::vst3::kDefaultSustainParamId;
    static constexpr std::uint32_t kAllNotesOffParamId
        = kraftonic::formats::vst3::kDefaultAllNotesOffParamId;
    static constexpr std::uint32_t kAllSoundOffParamId
        = kraftonic::formats::vst3::kDefaultAllSoundOffParamId;
    static constexpr std::uint32_t kModWheelParamId
        = kraftonic::formats::vst3::kDefaultModWheelParamId;
    static constexpr std::uint32_t kPitchBendParamId
        = kraftonic::formats::vst3::kDefaultPitchBendParamId;
    static constexpr std::uint32_t kProgramParamId
        = kraftonic::formats::vst3::kDefaultProgramParamId;
    static constexpr std::uint32_t kBypassParamId
        = kraftonic::formats::vst3::kDefaultBypassParamId;
    static constexpr const char* kProgramListName = "Factory Presets";
    static constexpr const char* kUidescFile = nullptr;
    static constexpr const char* kViewName = "view";

    static void formatPlainValue (ParamId id, float plain, char* text, std::size_t size) noexcept
    {
        if (text == nullptr || size == 0)
            return;
        if (id == ParamId::mode)
            std::snprintf (text, size, "%d", static_cast<int> (plain + 0.5F));
        else
            std::snprintf (text, size, "%.1f dB", static_cast<double> (plain));
    }

    static bool parsePlainValue (ParamId id, const char* text, float& plain) noexcept
    {
        if (text == nullptr)
            return false;
        plain = std::strtof (text, nullptr);
        if (id == ParamId::mode)
            plain = std::round (plain);
        return true;
    }

    static double tailSeconds (const Params& /*params*/) noexcept { return 0.0; }
};

static_assert (kraftonic::formats::vst3::effect::EffectTraits<FakeEffectTraits>);

} // namespace fakeeffect
