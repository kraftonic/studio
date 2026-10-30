// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic core — INSTRUMENT I/O CONTRACT.
//
//  The types every instrument engine uses to talk to a host adapter (VST3
//  now; CLAP, standalone and hardware later). No plug-in SDK types appear
//  here. Product engines use these through their own namespace, for example
//  kraftonic::startersynth::Event.
// ============================================================================

#include <cstdint>

namespace kraftonic::core
{
struct ProcessSetup
{
    double sampleRate = 48000.0; // any rate in [22050, 384000]
    int maxBlockSize = 512;      // largest numSamples process() will receive
};

/** A performance event, already translated from MIDI/VST3 by the adapter. */
struct Event
{
    enum class Type : std::uint8_t
    {
        noteOn,       // note, value = velocity in (0, 1]; velocity 0 is treated as noteOff
        noteOff,      // note, value = release velocity (may be ignored)
        sustainPedal, // value in [0, 1]; >= 0.5 means pedal down
        allNotesOff,  // release every note (respects nothing; pedal is reset)
        allSoundOff,  // silence immediately
        modWheel,     // value in [0, 1] (MIDI CC1); engines without a use ignore it
        pitchBend,    // value in [-1, 1], 0 = centre; range is the engine's choice
        polyPressure, // note = MIDI pitch, value in [0, 1]
        channelPressure // value in [0, 1]
    };

    Type type = Type::noteOn;
    int sampleOffset = 0; // position within the current block, 0 <= offset < numSamples
    int note = 0;         // MIDI note 0..127 (noteOn / noteOff only)
    float value = 0.0f;
};

/** Output buffers for one block. The engine WRITES (overwrites) both
    channels. `right` may equal `left` only if the adapter wants mono. */
struct AudioBlock
{
    float* left = nullptr;
    float* right = nullptr;
    int numSamples = 0;
};

/** Upper bound an adapter must respect per process() call; extra events in
    one block may be dropped by the adapter (never by allocating). */
inline constexpr int kMaxEventsPerBlock = 512;
} // namespace kraftonic::core
