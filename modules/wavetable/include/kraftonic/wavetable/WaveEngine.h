// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Wave — ENGINE CONTRACT.
//
//  The only interface between the wavetable DSP and any host
//  adapter. Same shape as the other instrument engines, so the shared
//  plug-in layer can drive it. No plug-in SDK types here.
// ============================================================================

#include "WaveParameters.h"
#include "kraftonic/core/Instrument.h"

#include <memory>

namespace kraftonic::wavetable
{
inline constexpr int kMaxVoices = 16;

using core::kMaxEventsPerBlock;
using core::ProcessSetup;
using core::Event;
using core::AudioBlock;

/** The Kraftonic Wave wavetable synth engine.

    Lifecycle (all calls from one thread at a time):
        WaveEngine engine;              // may allocate
        engine.prepare (setup);           // NOT real-time: may allocate; resets all voices
        loop:
            engine.setParameters (params);   // real-time safe, once per block
            engine.process (block, events, numEvents); // real-time safe
        engine.reset();                   // real-time safe: immediate silence

    Real-time guarantees for setParameters / process / reset / activeVoiceCount:
    no allocation, no locks, no I/O, no exceptions, bounded time.

    Behaviour guarantees:
    - Events are applied sample-accurately at their sampleOffset and must be
      sorted by sampleOffset (ties keep array order).
    - Output is finite and silent (exactly 0) when no voice is sounding.
    - Parameter changes are smoothed; no clicks on note start/stop/steal.
    - Wavetables are band-limited per octave (mip-mapped), so there is no audible aliasing at 44.1 kHz.
    - modWheel and pitchBend events are handled (see WaveParameters.h).
    - Output for a given event/parameter stream does not depend on how the
      host splits it into blocks.
*/
class WaveEngine
{
public:
    WaveEngine();
    ~WaveEngine();
    WaveEngine (const WaveEngine&) = delete;
    WaveEngine& operator= (const WaveEngine&) = delete;

    void prepare (const ProcessSetup& setup);
    void reset() noexcept;
    void setParameters (const WaveParameters& params) noexcept;
    void process (const AudioBlock& block, const Event* events, int numEvents) noexcept;

    int activeVoiceCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace kraftonic::wavetable
