// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Synth — the parameter table.
//
//  This file is the contract between the engine, the plug-in, the panel and
//  the presets. Everything else reads it:
//    - the VST3 base creates one host parameter per row (id = VST3 ParamID);
//    - the panel generator (ui/make_startersynth_uidesc.py) parses the enum;
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

namespace startersynth
{
namespace core = kraftonic::core;

using ParamScale = core::ParamScale;

enum class ParamId : std::uint32_t
{
    osc1Wave = 0,
    osc2Wave = 1,
    osc2Detune = 2,
    oscMix = 3,
    cutoff = 4,
    resonance = 5,
    envAmount = 6,
    attack = 7,
    decay = 8,
    sustain = 9,
    release = 10,
    glide = 11,
    outputGain = 12,
    count
};

inline constexpr int kNumParams = static_cast<int> (ParamId::count);
using ParamInfo = core::ParamInfo<ParamId>;

/** Oscillator shapes, in the order of the osc1Wave / osc2Wave steps. */
enum class Wave
{
    saw = 0,
    square = 1,
    sine = 2
};

// Columns: id, key, display name, unit, min, max, default, scale, stepCount.
// Plain values are engine units (Hz, seconds, dB, 0..1); stepCount > 0 makes
// a switch with stepCount + 1 positions. Times and cutoff use a log scale so
// the host slider spends as much travel on 10 ms..100 ms as on 1 s..10 s.
inline constexpr std::array<ParamInfo, kNumParams> kParamInfo {{
    {ParamId::osc1Wave, "osc1Wave", "Osc 1 Wave", "", 0.0F, 2.0F, 0.0F, ParamScale::linear, 2},
    {ParamId::osc2Wave, "osc2Wave", "Osc 2 Wave", "", 0.0F, 2.0F, 1.0F, ParamScale::linear, 2},
    {ParamId::osc2Detune, "osc2Detune", "Osc 2 Detune", "ct", -50.0F, 50.0F, 7.0F, ParamScale::linear, 0},
    {ParamId::oscMix, "oscMix", "Osc Mix", "%", 0.0F, 1.0F, 0.5F, ParamScale::linear, 0},
    {ParamId::cutoff, "cutoff", "Cutoff", "Hz", 20.0F, 20000.0F, 2000.0F, ParamScale::logarithmic, 0},
    {ParamId::resonance, "resonance", "Resonance", "%", 0.0F, 1.0F, 0.2F, ParamScale::linear, 0},
    {ParamId::envAmount, "envAmount", "Env Amount", "oct", -4.0F, 4.0F, 2.0F, ParamScale::linear, 0},
    {ParamId::attack, "attack", "Attack", "s", 0.001F, 5.0F, 0.005F, ParamScale::logarithmic, 0},
    {ParamId::decay, "decay", "Decay", "s", 0.005F, 5.0F, 0.3F, ParamScale::logarithmic, 0},
    {ParamId::sustain, "sustain", "Sustain", "%", 0.0F, 1.0F, 0.7F, ParamScale::linear, 0},
    {ParamId::release, "release", "Release", "s", 0.005F, 5.0F, 0.3F, ParamScale::logarithmic, 0},
    {ParamId::glide, "glide", "Glide", "s", 0.0F, 1.0F, 0.0F, ParamScale::linear, 0},
    {ParamId::outputGain, "outputGain", "Output", "dB", -60.0F, 0.0F, -6.0F, ParamScale::linear, 0},
}};

constexpr const ParamInfo& paramInfo (ParamId id) noexcept
{
    return kParamInfo[static_cast<std::size_t> (id)];
}

inline float toPlain (ParamId id, double normalized) noexcept { return core::toPlain (paramInfo (id), normalized); }
inline double toNormalized (ParamId id, float plain) noexcept { return core::toNormalized (paramInfo (id), plain); }

/** One plain value per parameter. The engine receives this once per block. */
struct StarterSynthParameters
{
    std::array<float, kNumParams> values {};

    static StarterSynthParameters defaults() noexcept
    {
        StarterSynthParameters result;
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
} // namespace startersynth
