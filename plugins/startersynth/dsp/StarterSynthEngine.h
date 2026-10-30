// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Synth — the sound engine.
//
//  Signal flow of one voice:
//
//      osc 1 ─┐
//             ├─ mix ─> resonant low-pass ─> amp ─> sum of voices ─> output
//      osc 2 ─┘             ^                 ^
//                           └──── ADSR ───────┘   (one envelope, two jobs)
//
//  The engine knows nothing about VST3: it takes plain parameter values and
//  SDK-free events (kraftonic/core/Instrument.h), so tests and the WAV
//  renderer drive it directly. The plug-in layer only translates host data.
//
//  Real-time rules (AGENTS.md): prepare() may allocate; reset(),
//  setParameters() and process() never allocate, lock, wait or throw.
// ============================================================================

#include "StarterSynthParameters.h"

#include <kraftonic/core/Adsr.h>
#include <kraftonic/core/Instrument.h>
#include <kraftonic/core/Smoother.h>
#include <kraftonic/core/VoiceAllocator.h>
#include <kraftonic/synth/Filters.h>
#include <kraftonic/synth/Oscillator.h>

#include <array>

namespace startersynth
{
namespace synth = kraftonic::synth;

using core::AudioBlock;
using core::Event;
using core::ProcessSetup;

/** Notes that can sound at once. More voices cost more CPU, linearly. */
inline constexpr int kMaxVoices = 8;

/** Each voice is scaled by this so a full chord stays under 0 dBFS. */
inline constexpr float kVoiceGain = 0.25F;

class StarterSynthEngine
{
public:
    void prepare (const ProcessSetup& setup);
    void reset() noexcept;
    void setParameters (const StarterSynthParameters& params) noexcept;
    void process (const AudioBlock& block, const Event* events, int numEvents) noexcept;

    /** Voices still producing sound (held or releasing). For tests and meters. */
    int activeVoiceCount() const noexcept { return allocator_.numSounding(); }

private:
    struct Voice
    {
        synth::Oscillator osc1;
        synth::Oscillator osc2;
        synth::StateVariableFilter filter;
        core::Adsr env;
        float pitch = 60.0F;       // current pitch as a fractional MIDI note (glides)
        float targetPitch = 60.0F; // the played note
        float velocity = 0.0F;
        bool sustained = false;    // key released while the sustain pedal is down
    };

    void handleEvent (const Event& event) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void releaseVoice (int index) noexcept;
    /** Renders samples [start, end) of the block, all voices summed. */
    void render (float* left, float* right, int start, int end) noexcept;

    std::array<Voice, kMaxVoices> voices_ {};
    core::VoiceAllocator<kMaxVoices> allocator_;

    // Continuous controls glide to each new value over 20 ms instead of
    // jumping, so automation and knob moves never click.
    core::LinearSmoother mix_, detune_, cutoffOctaves_, resonance_, envAmount_, gain_;

    StarterSynthParameters params_ = StarterSynthParameters::defaults();
    double sampleRate_ = 48000.0;
    float glideCoefficient_ = 1.0F; // 1 = no glide
    float lastPitch_ = -1.0F;       // the most recent note, where the next one glides from
    float appliedResonance_ = -1.0F; // last value given to the voice filters
    bool pedalDown_ = false;
    bool snapSmoothers_ = true; // after reset(): jump to the next values, no ramp
};
} // namespace startersynth
