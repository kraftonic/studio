// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic core — PARAMETER DESCRIPTION CONTRACT.
//
//  Every product describes its parameters with a table of ParamInfo<Id>,
//  where Id is the product's own `enum class ParamId : std::uint32_t`.
//  The numeric Id is also the VST3 ParamID, so it is permanent once shipped.
//
//  Host side values are normalized [0, 1]; DSP engines always receive
//  plain values (engine units: dB, seconds, Hz, 0..1, steps...).
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kraftonic::core
{
enum class ParamScale : std::uint8_t
{
    linear,     // normalized maps linearly to [min, max]
    logarithmic // normalized maps exponentially; min must be > 0
};

template <typename Id>
struct ParamInfo
{
    Id id;
    const char* key;  // stable machine name, used in presets and docs
    const char* name; // display name
    const char* unit; // display unit ("" for none)
    float minValue;
    float maxValue;
    float defaultValue;
    ParamScale scale;
    /** 0 = continuous. N > 0 = discrete with N + 1 values from min to max
        (for example a 3-way switch has stepCount 2, min 0, max 2). This is
        also the VST3 ParameterInfo::stepCount. Discrete parameters must be
        linear. */
    int stepCount = 0;
};

/** Normalized [0, 1] (host side) -> plain value (engine units). */
template <typename Id>
inline float toPlain (const ParamInfo<Id>& p, double normalized) noexcept
{
    const double n = std::clamp (normalized, 0.0, 1.0);
    if (p.stepCount > 0)
    {
        const double step = std::round (n * p.stepCount);
        return static_cast<float> (p.minValue + step * (p.maxValue - p.minValue) / p.stepCount);
    }
    if (p.scale == ParamScale::logarithmic)
        return static_cast<float> (p.minValue * std::pow (static_cast<double> (p.maxValue / p.minValue), n));
    return static_cast<float> (p.minValue + n * (p.maxValue - p.minValue));
}

/** Plain value -> normalized [0, 1]. Out-of-range values are clamped;
    discrete values snap to the nearest step. */
template <typename Id>
inline double toNormalized (const ParamInfo<Id>& p, float plain) noexcept
{
    const double v = std::clamp (static_cast<double> (plain), static_cast<double> (p.minValue),
                                 static_cast<double> (p.maxValue));
    const double range = static_cast<double> (p.maxValue - p.minValue);
    if (p.stepCount > 0)
        return std::round ((v - p.minValue) / range * p.stepCount) / p.stepCount;
    if (p.scale == ParamScale::logarithmic)
        return std::log (v / p.minValue) / std::log (static_cast<double> (p.maxValue / p.minValue));
    return (v - p.minValue) / range;
}

/** Clamps a plain value to the parameter's range (and step grid). */
template <typename Id>
inline float clampPlain (const ParamInfo<Id>& p, float plain) noexcept
{
    const float v = std::clamp (plain, p.minValue, p.maxValue);
    if (p.stepCount > 0)
        return toPlain (p, toNormalized (p, v));
    return v;
}
} // namespace kraftonic::core
