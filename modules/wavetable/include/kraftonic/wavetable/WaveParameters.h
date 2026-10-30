// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Wave — PARAMETER CONTRACT.
//
//  Single source of truth for the Wave wavetable synth's parameters, shared by
//  the DSP engine, the VST3 plug-in, presets and saved state. Wavetables are
//  generated in code (no sample files); the table names are descriptive only.
//
//  Compatibility rules (saved projects depend on these once it ships):
//   - A ParamId number and its `key` are permanent. Never renumber, rename or
//     reuse one. Add new parameters at the end with new numbers.
//   - Ranges may only widen. Defaults may change only before a release.
//  Additions need a maintainer's agreement in an issue.
// ============================================================================

#include "kraftonic/core/Parameters.h"

#include <array>
#include <cstdint>

namespace kraftonic::wavetable
{
enum class ParamId : std::uint32_t
{
    outputGain = 0,
    oscATable = 1, // 0 Basic, 1 Harmonic, 2 Pulse sweep, 3 Formant, 4 Metallic, 5 Digital, 6 Organ, 7 Noise
    oscAPosition = 2, // 0..1 morph through the table
    oscAOctave = 3,
    oscASemitone = 4,
    oscAFine = 5,
    oscALevel = 6,
    oscAUnison = 7, // stacked copies
    oscADetune = 8, // spread across the stack
    oscASpread = 9, // stereo width of the stack
    oscBTable = 10, // 0 Basic, 1 Harmonic, 2 Pulse sweep, 3 Formant, 4 Metallic, 5 Digital, 6 Organ, 7 Noise
    oscBPosition = 11, // 0..1 morph through the table
    oscBOctave = 12,
    oscBSemitone = 13,
    oscBFine = 14,
    oscBLevel = 15,
    oscBUnison = 16, // stacked copies
    oscBDetune = 17, // spread across the stack
    oscBSpread = 18, // stereo width of the stack
    subLevel = 19, // sine sub oscillator
    subOctave = 20, // 0 = -1 octave, 1 = -2 octaves
    noiseLevel = 21,
    filterMode = 22, // 0 LP12, 1 LP24, 2 BP, 3 HP
    filterCutoff = 23,
    filterResonance = 24,
    filterDrive = 25,
    filterKeyTrack = 26,
    filterEnvAmount = 27, // mod envelope -> cutoff
    ampAttack = 28,
    ampDecay = 29,
    ampSustain = 30,
    ampRelease = 31,
    modAttack = 32,
    modDecay = 33,
    modSustain = 34,
    modRelease = 35,
    modEnvToPosition = 36, // mod envelope -> wave position (both oscs)
    lfo1Rate = 37,
    lfo1Shape = 38, // 0 sine, 1 triangle, 2 saw, 3 square, 4 sample & hold
    lfo1Depth = 39,
    lfo1Destination = 40, // 0 off, 1 pitch, 2 cutoff, 3 wave position, 4 level, 5 pan
    lfo2Rate = 41,
    lfo2Shape = 42, // 0 sine, 1 triangle, 2 saw, 3 square, 4 sample & hold
    lfo2Depth = 43,
    lfo2Destination = 44, // 0 off, 1 pitch, 2 cutoff, 3 wave position, 4 level, 5 pan
    glideTime = 45,
    voiceMode = 46, // 0 poly, 1 mono, 2 mono legato
    pitchBendRange = 47,
    modWheelToLfo1 = 48, // 0 = wheel unused; 1 = wheel fully scales LFO 1 depth
    velocityToLevel = 49,
    velocityToCutoff = 50,
    count
};

inline constexpr int kNumParams = static_cast<int> (ParamId::count);

using core::ParamScale;
using ParamInfo = core::ParamInfo<ParamId>;

// clang-format off
inline constexpr std::array<ParamInfo, kNumParams> kParamInfo {{
    { ParamId::outputGain, "outputGain", "Output", "dB", -60.0f, 6.0f, -6.0f, ParamScale::linear },
    { ParamId::oscATable, "oscATable", "Osc A Table", "", 0.0f, 7.0f, 0.0f, ParamScale::linear, 7 },
    { ParamId::oscAPosition, "oscAPosition", "Osc A Position", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::oscAOctave, "oscAOctave", "Osc A Octave", "oct", -3.0f, 3.0f, 0.0f, ParamScale::linear, 6 },
    { ParamId::oscASemitone, "oscASemitone", "Osc A Semi", "st", -12.0f, 12.0f, 0.0f, ParamScale::linear, 24 },
    { ParamId::oscAFine, "oscAFine", "Osc A Fine", "ct", -100.0f, 100.0f, 0.0f, ParamScale::linear },
    { ParamId::oscALevel, "oscALevel", "Osc A Level", "", 0.0f, 1.0f, 1.0f, ParamScale::linear },
    { ParamId::oscAUnison, "oscAUnison", "Osc A Unison", "", 1.0f, 8.0f, 1.0f, ParamScale::linear, 7 },
    { ParamId::oscADetune, "oscADetune", "Osc A Detune", "ct", 0.0f, 100.0f, 20.0f, ParamScale::linear },
    { ParamId::oscASpread, "oscASpread", "Osc A Spread", "", 0.0f, 1.0f, 0.5f, ParamScale::linear },
    { ParamId::oscBTable, "oscBTable", "Osc B Table", "", 0.0f, 7.0f, 0.0f, ParamScale::linear, 7 },
    { ParamId::oscBPosition, "oscBPosition", "Osc B Position", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::oscBOctave, "oscBOctave", "Osc B Octave", "oct", -3.0f, 3.0f, 0.0f, ParamScale::linear, 6 },
    { ParamId::oscBSemitone, "oscBSemitone", "Osc B Semi", "st", -12.0f, 12.0f, 0.0f, ParamScale::linear, 24 },
    { ParamId::oscBFine, "oscBFine", "Osc B Fine", "ct", -100.0f, 100.0f, 0.0f, ParamScale::linear },
    { ParamId::oscBLevel, "oscBLevel", "Osc B Level", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::oscBUnison, "oscBUnison", "Osc B Unison", "", 1.0f, 8.0f, 1.0f, ParamScale::linear, 7 },
    { ParamId::oscBDetune, "oscBDetune", "Osc B Detune", "ct", 0.0f, 100.0f, 20.0f, ParamScale::linear },
    { ParamId::oscBSpread, "oscBSpread", "Osc B Spread", "", 0.0f, 1.0f, 0.5f, ParamScale::linear },
    { ParamId::subLevel, "subLevel", "Sub", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::subOctave, "subOctave", "Sub Octave", "", 0.0f, 1.0f, 0.0f, ParamScale::linear, 1 },
    { ParamId::noiseLevel, "noiseLevel", "Noise", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::filterMode, "filterMode", "Filter Mode", "", 0.0f, 3.0f, 1.0f, ParamScale::linear, 3 },
    { ParamId::filterCutoff, "filterCutoff", "Cutoff", "Hz", 20.0f, 20000.0f, 12000.0f, ParamScale::logarithmic },
    { ParamId::filterResonance, "filterResonance", "Resonance", "", 0.0f, 1.0f, 0.1f, ParamScale::linear },
    { ParamId::filterDrive, "filterDrive", "Filter Drive", "dB", 0.0f, 24.0f, 0.0f, ParamScale::linear },
    { ParamId::filterKeyTrack, "filterKeyTrack", "Key Track", "", 0.0f, 1.0f, 0.5f, ParamScale::linear },
    { ParamId::filterEnvAmount, "filterEnvAmount", "Filter Env", "oct", -8.0f, 8.0f, 0.0f, ParamScale::linear },
    { ParamId::ampAttack, "ampAttack", "Amp Attack", "s", 0.001f, 10.0f, 0.002f, ParamScale::logarithmic },
    { ParamId::ampDecay, "ampDecay", "Amp Decay", "s", 0.005f, 10.0f, 0.3f, ParamScale::logarithmic },
    { ParamId::ampSustain, "ampSustain", "Amp Sustain", "", 0.0f, 1.0f, 0.8f, ParamScale::linear },
    { ParamId::ampRelease, "ampRelease", "Amp Release", "s", 0.005f, 10.0f, 0.3f, ParamScale::logarithmic },
    { ParamId::modAttack, "modAttack", "Mod Attack", "s", 0.001f, 10.0f, 0.001f, ParamScale::logarithmic },
    { ParamId::modDecay, "modDecay", "Mod Decay", "s", 0.005f, 10.0f, 0.3f, ParamScale::logarithmic },
    { ParamId::modSustain, "modSustain", "Mod Sustain", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::modRelease, "modRelease", "Mod Release", "s", 0.005f, 10.0f, 0.3f, ParamScale::logarithmic },
    { ParamId::modEnvToPosition, "modEnvToPosition", "Env > Position", "", -1.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::lfo1Rate, "lfo1Rate", "LFO 1 Rate", "Hz", 0.02f, 40.0f, 2.0f, ParamScale::logarithmic },
    { ParamId::lfo1Shape, "lfo1Shape", "LFO 1 Shape", "", 0.0f, 4.0f, 0.0f, ParamScale::linear, 4 },
    { ParamId::lfo1Depth, "lfo1Depth", "LFO 1 Depth", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::lfo1Destination, "lfo1Destination", "LFO 1 Dest", "", 0.0f, 5.0f, 3.0f, ParamScale::linear, 5 },
    { ParamId::lfo2Rate, "lfo2Rate", "LFO 2 Rate", "Hz", 0.02f, 40.0f, 0.3f, ParamScale::logarithmic },
    { ParamId::lfo2Shape, "lfo2Shape", "LFO 2 Shape", "", 0.0f, 4.0f, 0.0f, ParamScale::linear, 4 },
    { ParamId::lfo2Depth, "lfo2Depth", "LFO 2 Depth", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::lfo2Destination, "lfo2Destination", "LFO 2 Dest", "", 0.0f, 5.0f, 2.0f, ParamScale::linear, 5 },
    { ParamId::glideTime, "glideTime", "Glide", "s", 0.0f, 2.0f, 0.0f, ParamScale::linear },
    { ParamId::voiceMode, "voiceMode", "Voice Mode", "", 0.0f, 2.0f, 0.0f, ParamScale::linear, 2 },
    { ParamId::pitchBendRange, "pitchBendRange", "Bend Range", "st", 1.0f, 24.0f, 2.0f, ParamScale::linear, 23 },
    { ParamId::modWheelToLfo1, "modWheelToLfo1", "Mod Wheel", "", 0.0f, 1.0f, 0.0f, ParamScale::linear },
    { ParamId::velocityToLevel, "velocityToLevel", "Vel > Level", "", 0.0f, 1.0f, 0.5f, ParamScale::linear },
    { ParamId::velocityToCutoff, "velocityToCutoff", "Vel > Cutoff", "oct", 0.0f, 8.0f, 0.0f, ParamScale::linear },
}};
// clang-format on

constexpr const ParamInfo& paramInfo (ParamId id) noexcept
{
    return kParamInfo[static_cast<std::size_t> (id)];
}

/** Normalized [0, 1] (host side) -> plain value (engine units). */
inline float toPlain (ParamId id, double normalized) noexcept
{
    return core::toPlain (paramInfo (id), normalized);
}

/** Plain value -> normalized [0, 1]. Out-of-range values are clamped. */
inline double toNormalized (ParamId id, float plain) noexcept
{
    return core::toNormalized (paramInfo (id), plain);
}

/** All parameter values in plain (engine) units. Trivially copyable. */
struct WaveParameters
{
    std::array<float, kNumParams> values {};

    static WaveParameters defaults() noexcept
    {
        WaveParameters s;
        for (const auto& p : kParamInfo)
            s.values[static_cast<std::size_t> (p.id)] = p.defaultValue;
        return s;
    }

    float get (ParamId id) const noexcept { return values[static_cast<std::size_t> (id)]; }

    /** Sets a plain value, clamped to the parameter's range and step grid. */
    void set (ParamId id, float plain) noexcept
    {
        values[static_cast<std::size_t> (id)] = core::clampPlain (paramInfo (id), plain);
    }
};
} // namespace kraftonic::wavetable
