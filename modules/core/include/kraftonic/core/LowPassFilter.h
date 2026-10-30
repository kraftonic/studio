// SPDX-License-Identifier: MIT
#pragma once

namespace kraftonic::core
{
/** Two-pole TPT state-variable low-pass filter with per-sample coefficients.

    Resonance maps to a finite Q (0.707 to 6), so full resonance rings but
    remains stable. This linear topology does not generate saturation harmonics;
    oversampling would add cost without addressing a nonlinear alias source.
    The oscillators already band-limit their discontinuities.
*/
class LowPassFilter
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    float process (float input, double cutoffHz, float resonance) noexcept;

private:
    double sampleRate = 44100.0;
    double integrator1 = 0.0;
    double integrator2 = 0.0;
};
} // namespace kraftonic::core
