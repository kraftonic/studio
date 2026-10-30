// SPDX-License-Identifier: MIT
#include "kraftonic/fx/Drive.h"

namespace kraftonic::fx
{
void Drive::prepare (const ProcessSetup& setup) noexcept
{
    rate = static_cast<float> (std::max (22050.0, setup.sampleRate));
    mix.prepare (rate);
    drive.prepare (rate, 0.01);
    tone.prepare (rate, 0.01);
    output.prepare (rate, 0.01);
    initialized = false;
    reset();
}

void Drive::reset() noexcept
{
    previous[0] = previous[1] = low[0] = low[1] = 0.0f;
}

void Drive::setParameters (const Parameters& value) noexcept
{
    mix.set (value.enabled, value.mix);
    const float d = detail::bounded (value.drive, 0.0f, 1.0f);
    const float t = detail::bounded (value.tone, 0.0f, 1.0f);
    const float o = detail::dbGain (value.outputDb);
    if (! initialized)
    {
        drive.setImmediate (d);
        tone.setImmediate (t);
        output.setImmediate (o);
        initialized = true;
    }
    else
    {
        drive.setTarget (d);
        tone.setTarget (t);
        output.setTarget (o);
    }
}

float Drive::adaa (float input, float& previous, float gain) noexcept
{
    // First-order antiderivative anti-aliasing of an asymmetric tanh clipper.
    const auto f = [gain] (float x) noexcept {
        const float g = gain * (x >= 0.0f ? 1.12f : 0.88f);
        return std::tanh (g * x);
    };
    const auto F = [gain] (float x) noexcept {
        const float g = gain * (x >= 0.0f ? 1.12f : 0.88f);
        const float a = std::abs (g * x);
        return (a + std::log1p (std::exp (-2.0f * a)) - std::log (2.0f)) / g;
    };
    const float delta = input - previous;
    const float result = std::abs (delta) > 0.0001f ? (F (input) - F (previous)) / delta
                                                       : f (0.5f * (input + previous));
    previous = input;
    return result;
}

void Drive::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        const float blend = mix.next();
        const float d = drive.next();
        const float t = tone.next();
        const float level = output.next();
        const float gain = 1.0f + 14.0f * d * d;
        const float compensation = 1.0f / std::sqrt (gain);
        const float cutoff = 900.0f + 13000.0f * t * t;
        const float a = 1.0f - std::exp (-2.0f * detail::pi * cutoff / rate);
        const float dry[2] { left[i], right[i] };
        float wet[2] {};
        for (int c = 0; c < 2; ++c)
        {
            const float clipped = adaa (detail::safe (dry[c]), previous[c], gain);
            low[c] += a * (clipped - low[c]);
            wet[c] = (0.25f * clipped + 0.75f * low[c]) * compensation * level;
        }
        left[i] = detail::Mix::apply (dry[0], wet[0], blend);
        if (right != left)
            right[i] = detail::Mix::apply (dry[1], wet[1], blend);
    }
}
} // namespace kraftonic::fx
