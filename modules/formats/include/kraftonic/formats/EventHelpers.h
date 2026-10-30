// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — SDK-free host-event helpers.
//
//  Pure translation of MIDI-note semantics into core::Event values, shared by
//  every host adapter so products never reimplement velocity/pedal mapping.
//  No plug-in SDK types appear here; the VST3/AU/CLAP adapters call these
//  with values they already extracted. Everything is noexcept and
//  allocation-free, so the audio thread may use it directly.
// ============================================================================

#include "kraftonic/core/Instrument.h"

#include <cstdint>
#include <cmath>

namespace kraftonic::formats
{

/** Clamps a MIDI pitch to the contract range 0..127. */
inline int clampNote (int pitch) noexcept
{
    if (pitch < 0)
        return 0;
    if (pitch > 127)
        return 127;
    return pitch;
}

/** Clamps a host event offset into [0, numSamples - 1]. */
inline int clampOffset (std::int32_t offset, int numSamples) noexcept
{
    if (numSamples < 1)
        return 0;
    if (offset < 0)
        return 0;
    if (offset >= static_cast<std::int32_t> (numSamples))
        return numSamples - 1;
    return static_cast<int> (offset);
}

/** Clamps a host 0..1 value to float range. */
inline float clamp01 (double v) noexcept
{
    if (!std::isfinite (v))
        return 0.0F;
    if (v < 0.0)
        return 0.0F;
    if (v > 1.0)
        return 1.0F;
    return static_cast<float> (v);
}

/**
 * Builds a noteOn event. Velocity 0 means noteOff by contract (and by MIDI
 * convention), so it is translated here and adapters never branch on it.
 */
inline core::Event makeNoteEvent (int pitch, double velocity, int offset) noexcept
{
    core::Event out {};
    out.sampleOffset = offset;
    out.note = clampNote (pitch);
    const float vel = clamp01 (velocity);
    if (vel <= 0.0F)
    {
        out.type = core::Event::Type::noteOff;
        out.value = 0.0F;
    }
    else
    {
        out.type = core::Event::Type::noteOn;
        out.value = vel;
    }
    return out;
}

/** Builds a noteOff event (release velocity may be ignored by engines). */
inline core::Event makeNoteOffEvent (int pitch, double releaseVelocity, int offset) noexcept
{
    core::Event out {};
    out.type = core::Event::Type::noteOff;
    out.sampleOffset = offset;
    out.note = clampNote (pitch);
    out.value = clamp01 (releaseVelocity);
    return out;
}

/** Builds a sustain-pedal event from a normalized CC64 value. */
inline core::Event makeSustainEvent (double normalized, int offset) noexcept
{
    core::Event out {};
    out.type = core::Event::Type::sustainPedal;
    out.sampleOffset = offset;
    out.value = clamp01 (normalized);
    return out;
}

/** Builds a mod-wheel event from a normalized CC1 value (0..1). */
inline core::Event makeModWheelEvent (double normalized, int offset) noexcept
{
    core::Event out {};
    out.type = core::Event::Type::modWheel;
    out.sampleOffset = offset;
    out.value = clamp01 (normalized);
    return out;
}

/** Builds a per-note pressure event (VST3 note expression or MIDI poly pressure). */
inline core::Event makePolyPressureEvent (int pitch, double normalized, int offset) noexcept
{
    core::Event out {};
    out.type = core::Event::Type::polyPressure;
    out.sampleOffset = offset;
    out.note = clampNote (pitch);
    out.value = clamp01 (normalized);
    return out;
}

/** Builds a channel pressure event from a normalized 0..1 value. */
inline core::Event makeChannelPressureEvent (double normalized, int offset) noexcept
{
    core::Event out {};
    out.type = core::Event::Type::channelPressure;
    out.sampleOffset = offset;
    out.value = clamp01 (normalized);
    return out;
}

/** Clamps a pitch-bend amount into the contract range [-1, 1]. */
inline float clampPitchBend (double v) noexcept
{
    if (!std::isfinite (v))
        return 0.0F;
    if (v < -1.0)
        return -1.0F;
    if (v > 1.0)
        return 1.0F;
    return static_cast<float> (v);
}

/**
 * Maps a normalized host pitch-bend parameter (0..1, centre 0.5, as delivered
 * through IMidiMapping) onto the contract range [-1, 1].
 */
inline float pitchBendFromNormalized (double normalized) noexcept
{
    return clampPitchBend (normalized * 2.0 - 1.0);
}

/**
 * Maps a 14-bit MIDI pitch-bend value (two 7-bit bytes, centre 8192) onto the
 * contract range [-1, 1], 0 meaning centre.
 */
inline float pitchBendFromMidi14 (int low7, int high7) noexcept
{
    int low = low7;
    int high = high7;
    if (low < 0)
        low = 0;
    if (low > 127)
        low = 127;
    if (high < 0)
        high = 0;
    if (high > 127)
        high = 127;
    const int raw = low + high * 128;
    return clampPitchBend ((static_cast<double> (raw) - 8192.0) / 8192.0);
}

/** Builds a pitch-bend event from a contract-range value (-1..1, 0 centre). */
inline core::Event makePitchBendEvent (float bipolar, int offset) noexcept
{
    core::Event out {};
    out.type = core::Event::Type::pitchBend;
    out.sampleOffset = offset;
    out.value = clampPitchBend (static_cast<double> (bipolar));
    return out;
}

/**
 * Momentary CC buttons (all-notes-off, all-sound-off, bypass) fire on the
 * pressed half of their range only.
 */
inline bool isMomentaryTrigger (double normalized) noexcept
{
    return normalized >= 0.5;
}

} // namespace kraftonic::formats
