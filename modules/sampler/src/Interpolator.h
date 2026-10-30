// SPDX-License-Identifier: MIT
#pragma once

// Band-limited sample interpolation: a 24-tap Kaiser-windowed sinc (beta 9), stored as
// 512 fractional phases (linearly interpolated between neighbours). Separate
// kernels for pitch ratios above 1 lower the cutoff, so a sample transposed
// up does not fold its top octave back down as aliasing.

#include "kraftonic/sampler/SampleBank.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace kraftonic::sampler::detail
{
inline constexpr int kInterpolationTaps = 24;
inline constexpr int kHalfTaps = kInterpolationTaps / 2;
inline constexpr int kPhases = 512;
static_assert (kSamplePadding >= kHalfTaps + 1, "sample padding too small for the interpolator");

class SincTables
{
public:
    // Upper pitch-ratio bound of each kernel (source frames per output frame).
    static constexpr std::array<double, 7> kRatioBounds { 1.0, 1.19, 1.41, 1.68, 2.0, 2.83, 4.0 };
    static constexpr int kNumKernels = static_cast<int> (kRatioBounds.size());

    SincTables()
    {
        for (int k = 0; k < kNumKernels; ++k)
            build (k, 0.455 / kRatioBounds[static_cast<std::size_t> (k)]);
    }

    static int kernelFor (double ratio) noexcept
    {
        for (int k = 0; k < kNumKernels; ++k)
            if (ratio <= kRatioBounds[static_cast<std::size_t> (k)] * 1.0001)
                return k;
        return kNumKernels - 1;
    }

    /** Coefficients for taps at offsets -7..+8 around integer frame i, for
        an output at i + frac (frac in [0, 1)). */
    void coefficients (int kernel, float frac, float* out) const noexcept
    {
        const float scaled = frac * static_cast<float> (kPhases);
        int phase = static_cast<int> (scaled);
        phase = phase < 0 ? 0 : (phase >= kPhases ? kPhases - 1 : phase);
        const float t = scaled - static_cast<float> (phase);
        const float* a = table[static_cast<std::size_t> (kernel)][static_cast<std::size_t> (phase)].data();
        const float* b = table[static_cast<std::size_t> (kernel)][static_cast<std::size_t> (phase + 1)].data();
        for (int j = 0; j < kInterpolationTaps; ++j)
            out[j] = a[j] + t * (b[j] - a[j]);
    }

private:
    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            const double h = x / (2.0 * k);
            term *= h * h;
            sum += term;
            if (term < 1e-12 * sum)
                break;
        }
        return sum;
    }

    void build (int kernel, double cutoff)
    {
        constexpr double beta = 9.0;
        constexpr double pi = 3.14159265358979323846;
        const double i0Beta = besselI0 (beta);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = static_cast<double> (p) / kPhases;
            std::array<double, kInterpolationTaps> h {};
            double sum = 0.0;
            for (int j = 0; j < kInterpolationTaps; ++j)
            {
                const double x = static_cast<double> (j - (kHalfTaps - 1)) - frac; // tap offset from the output point
                const double arg = 2.0 * cutoff * x;
                const double sinc = std::abs (arg) < 1e-9 ? 1.0 : std::sin (pi * arg) / (pi * arg);
                const double r = x / static_cast<double> (kHalfTaps);
                const double window = r * r >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta;
                h[static_cast<std::size_t> (j)] = sinc * window;
                sum += h[static_cast<std::size_t> (j)];
            }
            for (int j = 0; j < kInterpolationTaps; ++j)
                table[static_cast<std::size_t> (kernel)][static_cast<std::size_t> (p)][static_cast<std::size_t> (j)]
                    = static_cast<float> (h[static_cast<std::size_t> (j)] / sum); // unity gain at DC
        }
    }

    std::array<std::array<std::array<float, kInterpolationTaps>, kPhases + 1>, kNumKernels> table {};
};

/** Shared, built once on first use (not real-time; the Sampler touches it in prepare()). */
inline const SincTables& sincTables()
{
    static const SincTables tables;
    return tables;
}
} // namespace kraftonic::sampler::detail
