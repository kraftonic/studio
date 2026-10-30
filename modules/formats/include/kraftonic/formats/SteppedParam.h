// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — stepped-parameter plain-value convention.
//
//  SDK-free. VST3 hosts and VSTGUI derive option-menu entries, segment
//  buttons and stepped sliders from Parameter::toPlain/toNormalized: for a
//  discrete parameter with stepCount N they expect the plain value to be the
//  step index 0..N (the Steinberg StringListParameter convention), so entry
//  i shows the string for normalized value i/N. The shared base
//  Parameter<Traits> and the UI selectors both use these helpers, so menus
//  and selectors agree on every product.
// ============================================================================

#include <cmath>

namespace kraftonic::formats
{

/** Normalized [0, 1] -> step index [0, stepCount] for a discrete parameter. */
inline double stepIndexFromNormalized (int stepCount, double normalized) noexcept
{
    if (stepCount <= 0)
        return normalized < 0.0 ? 0.0 : (normalized > 1.0 ? 1.0 : normalized);
    const double steps = static_cast<double> (stepCount);
    const double clamped = normalized < 0.0 ? 0.0 : (normalized > 1.0 ? 1.0 : normalized);
    return std::round (clamped * steps);
}

/** Step index [0, stepCount] -> normalized [0, 1]. Out-of-range indices clamp. */
inline double normalizedFromStepIndex (int stepCount, double stepIndex) noexcept
{
    if (stepCount <= 0)
        return stepIndex < 0.0 ? 0.0 : (stepIndex > 1.0 ? 1.0 : stepIndex);
    const double steps = static_cast<double> (stepCount);
    const double rounded = std::round (stepIndex);
    const double clamped = rounded < 0.0 ? 0.0 : (rounded > steps ? steps : rounded);
    return clamped / steps;
}

} // namespace kraftonic::formats
