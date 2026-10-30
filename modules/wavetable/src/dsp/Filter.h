// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>

namespace kraftonic::wavetable::detail
{
/** Coefficients of a TPT (topology-preserving transform) state-variable
    filter: g = tan (pi fc / fs), k = 1 / Q. Shared by every SVF that runs at
    the same cutoff and resonance. */
struct SvfCoefficients
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4142135f;

    void set (float g, float damping) noexcept
    {
        k = damping;
        a1 = 1.0f / (1.0f + g * (g + damping));
        a2 = g * a1;
        a3 = g * a2;
    }
};

/** Zero-delay-feedback state-variable filter (linear, unconditionally stable
    for any g > 0 and k > 0). Produces low-, band- and high-pass at once. */
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void process (float x, const SvfCoefficients& c, float& low, float& band, float& high) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - c.k * v1 - v2;
    }

    float lowPass (float x, const SvfCoefficients& c) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;
    }
};

/** tanh saturation with first-order antiderivative anti-aliasing (ADAA):
    y[n] = (F (x[n]) - F (x[n-1])) / (x[n] - x[n-1]), F (x) = log cosh x.
    Suppresses the aliasing a plain tanh would fold back, at the cost of a
    half-sample delay. */
class AdaaTanh
{
public:
    void reset() noexcept
    {
        previous = 0.0f;
        previousF = 0.0;
    }

    /** Keeps the history current without computing output (bypass path). */
    void track (float x) noexcept
    {
        previous = x;
        previousF = logCosh (x);
    }

    float process (float x) noexcept
    {
        const double fx = logCosh (x);
        const double dx = static_cast<double> (x) - static_cast<double> (previous);
        float y;
        if (std::abs (dx) > 1.0e-5)
            y = static_cast<float> ((fx - previousF) / dx);
        else
            y = std::tanh (0.5f * (x + previous));
        previous = x;
        previousF = fx;
        return y;
    }

    static double logCosh (float x) noexcept
    {
        const double a = std::abs (static_cast<double> (x));
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
    }

private:
    float previous = 0.0f;
    double previousF = 0.0;
};
} // namespace kraftonic::wavetable::detail
