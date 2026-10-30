// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Effect — parameter values as text.
//
//  Hosts and the panel's value fields show these strings, and parse what a
//  user types back into a plain value. Driven by the `unit` column of the
//  parameter table, so a new parameter usually needs no change here; only
//  switches with named positions (ping-pong) are listed explicitly.
//  SDK-free, so tests/StarterEffectPluginTests.cpp checks every round trip.
// ============================================================================

#include "StarterEffectParameters.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace startereffect::plugin
{

inline constexpr const char* kSwitchNames[] {"Off", "On"};

/** Position names for switch parameters, or nullptr for numeric ones. */
inline const char* const* stepNames (ParamId id) noexcept
{
    return id == ParamId::pingPong ? kSwitchNames : nullptr;
}

inline void formatPlainValue (ParamId id, float plain, char* text, std::size_t size) noexcept
{
    if (text == nullptr || size == 0)
        return;
    const auto& info = paramInfo (id);
    const auto v = static_cast<double> (plain);
    if (const auto* names = stepNames (id))
        std::snprintf (text, size, "%s", names[static_cast<int> (plain + 0.5F)]);
    else if (std::strcmp (info.unit, "%") == 0)
        std::snprintf (text, size, "%.0f %%", v * 100.0);
    else if (std::strcmp (info.unit, "Hz") == 0)
        std::snprintf (text, size, v >= 1000.0 ? "%.2f kHz" : "%.0f Hz", v >= 1000.0 ? v / 1000.0 : v);
    else if (std::strcmp (info.unit, "ms") == 0)
        std::snprintf (text, size, "%.0f ms", v);
    else
        std::snprintf (text, size, "%.1f %s", v, info.unit);
}

/** Accepts what formatPlainValue writes, plus bare numbers in display units
    ("50" for 50 %, "250", "2k" or "2 kHz"). Returns false for nonsense. */
inline bool parsePlainValue (ParamId id, const char* text, float& plain) noexcept
{
    if (text == nullptr)
        return false;
    while (std::isspace (static_cast<unsigned char> (*text)))
        ++text;
    const auto& info = paramInfo (id);
    if (const auto* names = stepNames (id))
        for (int i = 0; i <= info.stepCount; ++i)
            if (std::strncmp (text, names[i], std::strlen (names[i])) == 0)
            {
                plain = static_cast<float> (i);
                return true;
            }
    char* end = nullptr;
    double v = std::strtod (text, &end);
    if (end == text || !std::isfinite (v))
        return false;
    while (std::isspace (static_cast<unsigned char> (*end)))
        ++end;
    if (std::strcmp (info.unit, "%") == 0)
        v /= 100.0;
    else if (*end == 'k')
        v *= 1000.0; // "2 kHz", "2k"
    plain = static_cast<float> (v);
    return true;
}

} // namespace startereffect::plugin
