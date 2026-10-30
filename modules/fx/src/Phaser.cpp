// SPDX-License-Identifier: MIT
#include "kraftonic/fx/Phaser.h"

namespace kraftonic::fx
{
void Phaser::prepare (const ProcessSetup& setup) noexcept
{
    rate = static_cast<float> (std::max (22050.0, setup.sampleRate));
    mix.prepare (rate);
    rateHz.prepare (rate, 0.02);
    depth.prepare (rate, 0.02);
    feedback.prepare (rate, 0.02);
    stereoPhase.prepare (rate, 0.02);
    stageCount.prepare (rate, 0.01);
    initialized = false;
    reset();
}
void Phaser::reset() noexcept
{
    for (auto& channel : z)
        std::fill (std::begin (channel), std::end (channel), 0.0f);
    last[0] = last[1] = phase = 0.0f;
}
void Phaser::setParameters (const Parameters& p) noexcept
{
    mix.set (p.enabled, p.mix);
    const float count = static_cast<float> (p.stages <= 4 ? 4 : (p.stages <= 6 ? 6 : 8));
    const float r = detail::bounded (p.rateHz, 0.01f, 12.0f);
    const float d = detail::bounded (p.depth, 0.0f, 1.0f);
    const float f = detail::bounded (p.feedback, -0.85f, 0.85f);
    const float s = detail::bounded (p.stereoPhase, 0.0f, 1.0f);
    if (! initialized)
    {
        rateHz.setImmediate (r); depth.setImmediate (d);
        feedback.setImmediate (f); stereoPhase.setImmediate (s); stageCount.setImmediate (count);
        initialized = true;
    }
    else
    {
        rateHz.setTarget (r); depth.setTarget (d);
        feedback.setTarget (f); stereoPhase.setTarget (s); stageCount.setTarget (count);
    }
}
void Phaser::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        const float blend = mix.next();
        const float r = rateHz.next();
        const float d = depth.next();
        const float fb = feedback.next();
        const float offset = stereoPhase.next();
        const float stage = stageCount.next();
        const float dry[2] { left[i], right[i] };
        float wet[2] {};
        for (int c = 0; c < 2; ++c)
        {
            const float lfo = 0.5f + 0.5f * std::sin (2.0f * detail::pi * (phase + (c == 0 ? 0.0f : offset)));
            const float frequency = 160.0f * std::pow (22.0f, d * lfo);
            const float tangent = std::tan (detail::pi * frequency / rate);
            const float a = (1.0f - tangent) / (1.0f + tangent);
            float v = detail::safe (dry[c]) + fb * last[c];
            float tap4 = 0.0f, tap6 = 0.0f;
            for (int stageIndex = 0; stageIndex < 8; ++stageIndex)
            {
                const float y = a * v + z[c][stageIndex];
                z[c][stageIndex] = v - a * y;
                v = y;
                if (stageIndex == 3) tap4 = v;
                if (stageIndex == 5) tap6 = v;
            }
            const float chosen = stage <= 6.0f ? tap4 + 0.5f * (stage - 4.0f) * (tap6 - tap4)
                                                : tap6 + 0.5f * (stage - 6.0f) * (v - tap6);
            last[c] = detail::safe (chosen);
            wet[c] = last[c];
        }
        left[i] = detail::Mix::apply (dry[0], wet[0], blend);
        if (right != left)
            right[i] = detail::Mix::apply (dry[1], wet[1], blend);
        phase += r / rate;
        if (phase >= 1.0f) phase -= 1.0f;
    }
}
} // namespace kraftonic::fx
