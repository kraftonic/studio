// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Effect — the parameter table.
//
//  This file is the contract between the engine, the plug-in, the panel and
//  the presets. Everything else reads it:
//    - the VST3 base creates one host parameter per row (id = VST3 ParamID);
//    - the panel generator (ui/make_startereffect_uidesc.py) parses the enum;
//    - presets refer to parameters by their stable `key`.
//
//  To add a parameter: append an enum value before `count` and a row to
//  kParamInfo in the same position. Once a version ships, never renumber,
//  rename a key or reuse an id: saved songs store them.
// ============================================================================

#include <kraftonic/core/Parameters.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace startereffect
{
namespace core = kraftonic::core;

using ParamScale = core::ParamScale;

enum class ParamId : std::uint32_t
{
    drive = 0,
    tone = 1,
    delayTime = 2,
    feedback = 3,
    damping = 4,
    pingPong = 5,
    mix = 6,
    outputGain = 7,
    count
};

inline constexpr int kNumParams = static_cast<int> (ParamId::count);
using ParamInfo = core::ParamInfo<ParamId>;

// Columns: id, key, display name, unit, min, max, default, scale, stepCount.
// Plain values are engine units (ms, Hz, dB, 0..1); stepCount 1 is a switch.
inline constexpr std::array<ParamInfo, kNumParams> kParamInfo {{
    {ParamId::drive, "drive", "Drive", "%", 0.0F, 1.0F, 0.2F, ParamScale::linear, 0},
    {ParamId::tone, "tone", "Tone", "%", 0.0F, 1.0F, 0.5F, ParamScale::linear, 0},
    {ParamId::delayTime, "delayTime", "Delay Time", "ms", 10.0F, 1000.0F, 350.0F, ParamScale::logarithmic, 0},
    {ParamId::feedback, "feedback", "Feedback", "%", 0.0F, 0.9F, 0.35F, ParamScale::linear, 0},
    {ParamId::damping, "damping", "Damping", "Hz", 1000.0F, 18000.0F, 6000.0F, ParamScale::logarithmic, 0},
    {ParamId::pingPong, "pingPong", "Ping-Pong", "", 0.0F, 1.0F, 0.0F, ParamScale::linear, 1},
    {ParamId::mix, "mix", "Mix", "%", 0.0F, 1.0F, 0.3F, ParamScale::linear, 0},
    {ParamId::outputGain, "outputGain", "Output", "dB", -24.0F, 6.0F, 0.0F, ParamScale::linear, 0},
}};

constexpr const ParamInfo& paramInfo (ParamId id) noexcept
{
    return kParamInfo[static_cast<std::size_t> (id)];
}

inline float toPlain (ParamId id, double normalized) noexcept { return core::toPlain (paramInfo (id), normalized); }
inline double toNormalized (ParamId id, float plain) noexcept { return core::toNormalized (paramInfo (id), plain); }

/** One plain value per parameter. The engine receives this once per block. */
struct StarterEffectParameters
{
    std::array<float, kNumParams> values {};

    static StarterEffectParameters defaults() noexcept
    {
        StarterEffectParameters result;
        for (const auto& info : kParamInfo)
            result.values[static_cast<std::size_t> (info.id)] = info.defaultValue;
        return result;
    }

    float get (ParamId id) const noexcept { return values[static_cast<std::size_t> (id)]; }

    /** Clamps to the range (and step grid); a NaN or infinity becomes the default. */
    void set (ParamId id, float value) noexcept
    {
        const auto& info = paramInfo (id);
        values[static_cast<std::size_t> (id)] = std::isfinite (value) ? core::clampPlain (info, value) : info.defaultValue;
    }
};
} // namespace startereffect
