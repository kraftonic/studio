// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — parameter value text, shared by every format.
//
//  SDK-free. Hosts show a parameter's value as text and let the user type
//  one: both go through the product's formatting hook (Traits::
//  formatPlainValue / parsePlainValue) in engine units, so every format
//  (VST3 Parameter<Traits>, the CLAP params extension) shows and accepts the
//  same strings. Text is UTF-8; the VST3 adapter converts it to UTF-16.
// ============================================================================

#include <cmath>
#include <cstddef>

namespace kraftonic::formats
{

/** Text for a contract parameter at host-normalized value [0, 1]. */
template <typename Traits>
void formatParamText (typename Traits::ParamId id, double normalized, char* text,
                      std::size_t size) noexcept
{
    if (text == nullptr || size == 0)
        return;
    text[0] = '\0';
    Traits::formatPlainValue (id, Traits::toPlain (id, normalized), text, size);
}

/** Parses user text for a contract parameter into a normalized value.
    Out-of-range numbers clamp to the parameter's range; NaN, infinity and
    text the product cannot parse are rejected (false). */
template <typename Traits>
bool parseParamText (typename Traits::ParamId id, const char* text, double& normalized) noexcept
{
    if (text == nullptr)
        return false;
    float plain = 0.0F;
    if (!Traits::parsePlainValue (id, text, plain))
        return false;
    const auto& meta = Traits::paramInfo (id);
    if (!(plain >= meta.minValue && plain <= meta.maxValue))
    {
        if (!std::isfinite (plain))
            return false;
        plain = plain < meta.minValue ? meta.minValue : meta.maxValue;
    }
    normalized = Traits::toNormalized (id, plain);
    return true;
}

/** The shared bypass switch's text: "On" or "Off". */
inline const char* bypassText (double normalized) noexcept
{
    return normalized >= 0.5 ? "On" : "Off";
}

/** Accepts On/on/1 and Off/off/0 for the bypass switch. */
inline bool parseBypassText (const char* text, double& normalized) noexcept
{
    if (text == nullptr)
        return false;
    if ((text[0] == 'O' || text[0] == 'o') && text[1] == 'n')
        normalized = 1.0;
    else if ((text[0] == 'O' || text[0] == 'o') && text[1] == 'f' && text[2] == 'f')
        normalized = 0.0;
    else if (text[0] == '1')
        normalized = 1.0;
    else if (text[0] == '0')
        normalized = 0.0;
    else
        return false;
    return true;
}

} // namespace kraftonic::formats
