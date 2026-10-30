// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>

namespace kraftonic::core
{
/** Stereo-linked, memoryless soft knee for rare full-scale peaks.

    Samples at or below -1 dBFS pass through bit for bit. Above that point a
    rational curve approaches 1 without a hard corner or a finite input that
    reaches full scale. Both channels receive the same gain, preserving stereo
    balance. No allocation, state, or lookahead is needed on the audio thread.
*/
class StereoPeakProtector
{
public:
    static constexpr float kThreshold = 0.8912509f;

    static void process (float& left, float& right) noexcept
    {
        const float peak = std::max (std::abs (left), std::abs (right));
        if (peak <= kThreshold)
            return;
        const double remaining = 1.0 - static_cast<double> (kThreshold);
        const double excess = static_cast<double> (peak - kThreshold);
        const double shaped = static_cast<double> (kThreshold) + excess / (1.0 + excess / remaining);
        const float gain = static_cast<float> (shaped / peak);
        left *= gain;
        right *= gain;
    }
};
} // namespace kraftonic::core
