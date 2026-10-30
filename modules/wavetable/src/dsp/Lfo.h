// SPDX-License-Identifier: MIT
#pragma once

#include "Random.h"
#include "kraftonic/core/Math.h"

#include <cmath>

namespace kraftonic::wavetable::detail
{
/** Bipolar LFO: sine, triangle, saw (rising), square, sample & hold.
    The output passes through a ~1.5 ms one-pole so square, S&H, saw resets
    and shape switches never click. Phase 0 starts every shape at its "rest"
    point (sine and triangle at 0). */
class Lfo
{
public:
    enum Shape
    {
        sine = 0,
        triangle = 1,
        saw = 2,
        square = 3,
        sampleHold = 4
    };

    void prepare (double sampleRate) noexcept
    {
        smoothCoef = static_cast<float> (std::exp (-1.0 / (0.0015 * sampleRate)));
    }

    /** Restarts at phase 0. `snap` jumps the output there (silent voice);
        otherwise the output glides over ~1.5 ms (sounding voice). */
    void reset (std::uint32_t seed, bool snap = true) noexcept
    {
        phase = 0.0;
        random.setSeed (seed);
        held = random.nextBipolar();
        if (snap)
        {
            smoothed = 0.0f;
            first = true;
        }
    }

    /** `increment` = rate / sampleRate. */
    float next (double increment, int shape) noexcept
    {
        const float raw = shapeValue (shape);
        if (first)
        {
            smoothed = raw;
            first = false;
        }
        smoothed = raw + (smoothed - raw) * smoothCoef;
        phase += increment;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = random.nextBipolar();
        }
        return smoothed;
    }

    float current() const noexcept { return smoothed; }

    /** sin (2 pi p) for p in [0, 1): refined parabola, max error ~0.001
        (plenty for an LFO, and much cheaper than std::sin). */
    static float fastSine (float p) noexcept
    {
        const float u = 2.0f * p - 1.0f;                // sin (2 pi p) = -sin (pi u)
        const float s = 4.0f * u * (1.0f - std::abs (u)); // parabola through sin (pi u)
        return -(0.225f * (s * std::abs (s) - s) + s);
    }

private:
    float shapeValue (int shape) const noexcept
    {
        const auto p = static_cast<float> (phase);
        switch (shape)
        {
            case triangle: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case saw: return 2.0f * p - 1.0f;
            case square: return p < 0.5f ? 1.0f : -1.0f;
            case sampleHold: return held;
            default: return fastSine (p);
        }
    }

    double phase = 0.0;
    float held = 0.0f;
    float smoothed = 0.0f;
    float smoothCoef = 0.0f;
    bool first = true;
    Random random;
};
} // namespace kraftonic::wavetable::detail
