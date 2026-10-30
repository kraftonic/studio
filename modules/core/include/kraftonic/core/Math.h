// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>

namespace kraftonic::core
{
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

/** Equal-tempered MIDI note to frequency, A4 (note 69) = 440 Hz. */
inline double midiNoteToHz (double note) noexcept
{
    return 440.0 * std::exp2 ((note - 69.0) / 12.0);
}

inline float decibelsToGain (float db, float minusInfinityDb = -96.0f) noexcept
{
    return db <= minusInfinityDb ? 0.0f : std::pow (10.0f, db * 0.05f);
}

/** Returns x wrapped into [0, 1). */
inline double wrapPhase (double x) noexcept
{
    return x - std::floor (x);
}

inline bool isFinite (float x) noexcept
{
    return std::isfinite (x);
}
} // namespace kraftonic::core
