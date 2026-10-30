// SPDX-License-Identifier: MIT
#pragma once

#include "kraftonic/core/Smoother.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kraftonic::fx::detail
{
inline constexpr float pi = 3.14159265358979323846f;

inline float bounded (float value, float low, float high) noexcept
{
    return std::isfinite (value) ? std::clamp (value, low, high) : low;
}

/** Finite, within +-16, and never subnormal: anything below 1e-30 (-600 dB)
    becomes exactly 0, so fading feedback and filter states stop there. */
inline float safe (float value) noexcept
{
    if (!std::isfinite (value) || std::abs (value) < 1.0e-30f)
        return 0.0f;
    return std::clamp (value, -16.0f, 16.0f);
}

inline float dbGain (float db) noexcept
{
    return std::pow (10.0f, bounded (db, -60.0f, 24.0f) / 20.0f);
}

class Mix
{
public:
    void prepare (double rate) noexcept
    {
        amount.prepare (rate, 0.005);
        enable.prepare (rate, 0.005);
        amount.setImmediate (0.0f);
        enable.setImmediate (0.0f);
        initialized = false;
    }

    void set (bool enabled, float mix) noexcept
    {
        const float target = bounded (mix, 0.0f, 1.0f);
        if (! initialized)
        {
            amount.setImmediate (target);
            enable.setImmediate (enabled ? 1.0f : 0.0f);
            initialized = true;
        }
        else
        {
            amount.setTarget (target);
            enable.setTarget (enabled ? 1.0f : 0.0f);
        }
    }

    float next() noexcept { return amount.next() * enable.next(); }
    static float apply (float dry, float wet, float mix) noexcept
    {
        if (mix <= 0.0f)
            return dry;
        return safe (dry + mix * (wet - dry));
    }

private:
    core::LinearSmoother amount, enable;
    bool initialized = false;
};

class Ring
{
public:
    void prepare (int samples)
    {
        data.assign (static_cast<std::size_t> (std::max (4, samples)), 0.0f);
        pos = 0;
    }

    void reset() noexcept
    {
        std::fill (data.begin(), data.end(), 0.0f);
        pos = 0;
    }

    float read (float samples) const noexcept
    {
        const auto size = static_cast<int> (data.size());
        float index = static_cast<float> (pos) - std::clamp (samples, 1.0f, static_cast<float> (size - 2));
        if (index < 0.0f)
            index += static_cast<float> (size);
        // Float rounding can land exactly on size; wrap so data[size] is never read.
        if (index >= static_cast<float> (size))
            index -= static_cast<float> (size);
        const int i = static_cast<int> (index);
        const float t = index - static_cast<float> (i);
        return data[static_cast<std::size_t> (i)] * (1.0f - t)
             + data[static_cast<std::size_t> ((i + 1) % size)] * t;
    }

    void push (float sample) noexcept
    {
        data[static_cast<std::size_t> (pos)] = safe (sample);
        if (++pos == static_cast<int> (data.size()))
            pos = 0;
    }

private:
    std::vector<float> data;
    int pos = 0;
};
} // namespace kraftonic::fx::detail
