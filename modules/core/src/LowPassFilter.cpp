// SPDX-License-Identifier: MIT
#include "kraftonic/core/LowPassFilter.h"
#include "kraftonic/core/Math.h"

#include <algorithm>
#include <cmath>

namespace kraftonic::core
{
void LowPassFilter::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void LowPassFilter::reset() noexcept
{
    integrator1 = 0.0;
    integrator2 = 0.0;
}

float LowPassFilter::process (float input, double cutoffHz, float resonance) noexcept
{
    const double cutoff = std::clamp (cutoffHz, 20.0, 0.45 * sampleRate);
    const double r = static_cast<double> (std::clamp (resonance, 0.0f, 1.0f));
    const double q = 0.7071067811865476 + 5.292893218813452 * r * r;
    const double g = std::tan (kPi * cutoff / sampleRate);
    const double k = 1.0 / q;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    const double v3 = static_cast<double> (input) - integrator2;
    const double v1 = a1 * integrator1 + a2 * v3;
    const double v2 = integrator2 + a2 * integrator1 + a3 * v3;
    integrator1 = 2.0 * v1 - integrator1;
    integrator2 = 2.0 * v2 - integrator2;
    return static_cast<float> (v2);
}
} // namespace kraftonic::core
