// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin self-test — tiny sine instrument.
//
//  Test-only product used to prove the shared VST3 base: a 3-parameter
//  polyphonic sine synth (continuous gain + attack, one stepped wave
//  switch). SDK-free and real-time safe (no allocation, locks, I/O or
//  exceptions in setParameters/process/reset; fixed 8-voice pool).
//  Never installed or shipped; its bundle exists only for validator runs.
// ============================================================================

#include "kraftonic/core/Instrument.h"
#include "kraftonic/core/Parameters.h"

#include <array>
#include <cstdint>

namespace kraftonic::formats::selftest
{

enum class ParamId : std::uint32_t
{
    outputGain = 0, // dB
    wave = 1,       // 0 sine, 1 square, 2 triangle
    attack = 2,     // seconds
    count
};

inline constexpr int kNumParams = static_cast<int> (ParamId::count);

using core::ParamScale;
using ParamInfo = core::ParamInfo<ParamId>;

inline constexpr std::array<ParamInfo, kNumParams> kParamInfo {{
    {ParamId::outputGain, "outputGain", "Output", "dB", -60.0F, 6.0F, -6.0F,
     ParamScale::linear, 0},
    {ParamId::wave, "wave", "Wave", "", 0.0F, 2.0F, 0.0F, ParamScale::linear, 2},
    {ParamId::attack, "attack", "Attack", "s", 0.001F, 0.5F, 0.01F,
     ParamScale::logarithmic, 0},
}};

inline const ParamInfo& paramInfo (ParamId id) noexcept
{
    return kParamInfo[static_cast<std::size_t> (id)];
}

inline float toPlain (ParamId id, double normalized) noexcept
{
    return core::toPlain (paramInfo (id), normalized);
}

inline double toNormalized (ParamId id, float plain) noexcept
{
    return core::toNormalized (paramInfo (id), plain);
}

/** All parameter values in plain (engine) units. Trivially copyable. */
struct SelfTestParameters
{
    std::array<float, kNumParams> values {};

    static SelfTestParameters defaults() noexcept
    {
        SelfTestParameters s {};
        for (const auto& p : kParamInfo)
            s.values[static_cast<std::size_t> (p.id)] = p.defaultValue;
        return s;
    }

    float get (ParamId id) const noexcept
    {
        return values[static_cast<std::size_t> (id)];
    }

    void set (ParamId id, float plain) noexcept
    {
        values[static_cast<std::size_t> (id)] = core::clampPlain (paramInfo (id), plain);
    }
};

using core::AudioBlock;
using core::Event;
using core::ProcessSetup;

/**
 * Eight-voice test engine. Sine/square/triangle oscillators with linear
 * attack, fixed 0.15 s release, sustain-pedal hold, and smoothed output
 * gain. Output is finite, dual-mono, silent when idle, and stays under
 * 0 dBFS in normal playing.
 */
class SelfTestEngine
{
public:
    SelfTestEngine() = default;
    ~SelfTestEngine() = default;
    SelfTestEngine (const SelfTestEngine&) = delete;
    SelfTestEngine& operator= (const SelfTestEngine&) = delete;

    void prepare (const ProcessSetup& setup);
    void reset() noexcept;
    void setParameters (const SelfTestParameters& params) noexcept;
    void process (const AudioBlock& block, const Event* events, int numEvents) noexcept;

    int activeVoiceCount() const noexcept;

private:
    struct Voice
    {
        bool active = false;
        bool sustained = false; // held by the pedal past noteOff
        bool releasing = false;
        int note = -1;
        double phase = 0.0;
        double freq = 440.0;
        float velocity = 0.0F;
        float env = 0.0F; // 0..1 attack/decay level
    };

    static constexpr int kVoices = 8;
    static constexpr float kReleaseSeconds = 0.15F;

    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;

    std::array<Voice, kVoices> voices_ {};
    std::array<int, 128> noteVoice_ {}; // note -> voice index or -1
    double sampleRate_ = 48000.0;
    float gainLin_ = 0.5F;   // smoothed output gain (linear)
    float gainTarget_ = 0.5F;
    float attackSecs_ = 0.01F;
    int wave_ = 0;
    bool pedalDown_ = false;
    bool initialised_ = false;
};

} // namespace kraftonic::formats::selftest
