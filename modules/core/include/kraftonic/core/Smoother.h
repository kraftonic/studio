// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>

namespace kraftonic::core
{
/** Linear parameter ramp that reaches each new target in a fixed time.
    Use for gains and other values that must not jump between blocks. */
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampSamples = std::max (1, static_cast<int> (std::lround (sampleRate * rampSeconds)));
        snapToTarget();
    }

    void setTarget (float newTarget) noexcept
    {
        if (newTarget == target)
            return;
        target = newTarget;
        remaining = rampSamples;
        step = (target - current) / static_cast<float> (rampSamples);
    }

    /** Jump straight to `value`, e.g. when playback starts. */
    void setImmediate (float value) noexcept
    {
        target = value;
        snapToTarget();
    }

    float next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0)
                current = target;
        }
        return current;
    }

    bool isSmoothing() const noexcept { return remaining > 0; }
    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }

private:
    void snapToTarget() noexcept
    {
        current = target;
        remaining = 0;
        step = 0.0f;
    }

    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
    int remaining = 0;
    int rampSamples = 1;
};
} // namespace kraftonic::core
