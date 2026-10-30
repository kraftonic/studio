// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic sampler: the playback engine.
//
//  Plays one SampleBank with the same shape as every Kraftonic instrument
//  (kraftonic/core/Instrument.h):
//    - prepare() and setBank() are NOT real-time (they allocate);
//    - each block: setParameters() once, then process (block, events, n);
//    - process() is real-time safe: no allocation, locks, I/O or exceptions,
//      events are applied at their sampleOffset, the output is identical for
//      any block size, and always finite.
//
//  Playing behaviour:
//    - 24-tap band-limited interpolation (kernels switch with the pitch ratio);
//    - velocity layers crossfade linearly over InstrumentSettings::velocityCrossfade;
//    - release-trigger zones fire when the damper falls (key up, or pedal up);
//    - sustain pedal holds released notes; pedal-down/up noise zones;
//    - optional sympathetic string resonance while the pedal is down;
//    - voice stealing fades the stolen voice over 3 ms (never a hard cut).
// ============================================================================

#include "kraftonic/core/Instrument.h"
#include "kraftonic/sampler/SampleBank.h"

#include <memory>

namespace kraftonic::sampler
{
class Sampler
{
public:
    /** Hard voice limit. One voice plays one zone, so a note with two
        crossfading layers uses two. */
    static constexpr int kMaxVoices = 192;

    struct Parameters
    {
        float gainDb = 0.0f;
        float tuneCents = 0.0f;
        float pitchBendSemitones = 2.0f;  // range of a full pitchBend event
        float releaseScale = 1.0f;        // multiplies every release time (0.1 .. 10)
        float velocitySensitivity = 1.0f; // multiplies the manifest's velocity_range_db (0 .. 2)
        float pedalResonance = -1.0f;     // 0 .. 1, < 0 = the manifest's value
        float releaseNoiseDb = 0.0f;      // level of release-trigger zones (hammer/damper noise)
        float stereoWidth = 1.0f;         // 0 = mono, 1 = as recorded, up to 2
        int polyphony = 0;                // 0 = the manifest's value
    };

    Sampler();
    ~Sampler();
    Sampler (const Sampler&) = delete;
    Sampler& operator= (const Sampler&) = delete;

    /** NOT real-time. */
    void prepare (const core::ProcessSetup& setup);

    /** NOT real-time, and not concurrently with process(). Silences all
        voices. A null bank makes the sampler output silence. */
    void setBank (std::shared_ptr<const SampleBank> bank);
    const SampleBank* bank() const noexcept;

    void reset() noexcept;
    void setParameters (const Parameters& params) noexcept;

    /** Overwrites block.left/right. */
    void process (const core::AudioBlock& block, const core::Event* events, int numEvents) noexcept;

    int activeVoiceCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace kraftonic::sampler
