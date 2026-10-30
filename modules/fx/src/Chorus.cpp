// SPDX-License-Identifier: MIT
#include "kraftonic/fx/Chorus.h"

namespace kraftonic::fx
{
void Chorus::prepare (const ProcessSetup& setup)
{
    rate = static_cast<float> (std::max (22050.0, setup.sampleRate));
    for (auto& line : lines) line.prepare (static_cast<int> (rate * 0.06f) + 4);
    mix.prepare (rate);
    rateHz.prepare (rate, 0.02);
    depth.prepare (rate, 0.02);
    tapCount.prepare (rate, 0.01);
    initialized = false;
    reset();
}
void Chorus::reset() noexcept
{
    for (auto& line : lines) line.reset();
    phase = 0.0f;
}
void Chorus::setParameters (const Parameters& p) noexcept
{
    mix.set (p.enabled, p.mix);
    const float taps = static_cast<float> (std::clamp (p.taps, 1, 3));
    const float r = detail::bounded (p.rateHz, 0.05f, 8.0f);
    const float d = detail::bounded (p.depth, 0.0f, 1.0f);
    if (! initialized)
    {
        rateHz.setImmediate (r); depth.setImmediate (d); tapCount.setImmediate (taps); initialized = true;
    }
    else
    {
        rateHz.setTarget (r); depth.setTarget (d); tapCount.setTarget (taps);
    }
}
void Chorus::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        const float blend = mix.next();
        const float r = rateHz.next();
        const float d = depth.next();
        const float taps = tapCount.next();
        const float dry[2] { left[i], right[i] };
        float wet[2] {};
        for (int c = 0; c < 2; ++c)
        {
            float outputs[3] {};
            for (int tap = 0; tap < 3; ++tap)
            {
                const float stagger = static_cast<float> (tap) / 3.0f;
                const float p = phase + stagger + (c == 0 ? 0.0f : 0.21f);
                const float modulation = std::sin (2.0f * detail::pi * p);
                const float milliseconds = 13.0f + 6.0f * stagger + 5.0f * d * modulation;
                outputs[tap] = lines[c].read (milliseconds * 0.001f * rate);
            }
            const float one = outputs[0];
            const float two = 0.5f * (outputs[0] + outputs[1]);
            const float three = (outputs[0] + outputs[1] + outputs[2]) / 3.0f;
            wet[c] = taps <= 2.0f ? one + (taps - 1.0f) * (two - one)
                                  : two + (taps - 2.0f) * (three - two);
            lines[c].push (detail::safe (dry[c]));
        }
        left[i] = detail::Mix::apply (dry[0], wet[0], blend);
        if (right != left)
            right[i] = detail::Mix::apply (dry[1], wet[1], blend);
        phase += r / rate;
        if (phase >= 1.0f) phase -= 1.0f;
    }
}
} // namespace kraftonic::fx
