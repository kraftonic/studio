// SPDX-License-Identifier: MIT
#include "kraftonic/fx/Reverb.h"

namespace kraftonic::fx
{
void Reverb::prepare (const ProcessSetup& setup)
{
    rate = static_cast<float> (std::max (22050.0, setup.sampleRate));
    for (auto& line : pre) line.prepare (static_cast<int> (0.21f * rate) + 4);
    for (auto& line : lines) line.prepare (static_cast<int> (0.19f * rate) + 4);
    mix.prepare (rate);
    preDelay.prepare (rate, 0.03); size.prepare (rate, 0.03);
    decay.prepare (rate, 0.03); damping.prepare (rate, 0.03);
    width.prepare (rate, 0.03);
    initialized = false;
    reset();
}
void Reverb::reset() noexcept
{
    for (auto& line : pre) line.reset();
    for (auto& line : lines) line.reset();
    std::fill (std::begin (filter), std::end (filter), 0.0f);
    std::fill (std::begin (phases), std::end (phases), 0.0f);
}
void Reverb::setParameters (const Parameters& p) noexcept
{
    mix.set (p.enabled, p.mix);
    const float a = detail::bounded (p.preDelayMs, 0.0f, 200.0f);
    const float b = detail::bounded (p.size, 0.0f, 1.0f);
    const float c = detail::bounded (p.decaySeconds, 0.25f, 12.0f);
    const float d = detail::bounded (p.damping, 0.0f, 1.0f);
    const float e = detail::bounded (p.width, 0.0f, 1.0f);
    if (! initialized)
    {
        preDelay.setImmediate (a); size.setImmediate (b);
        decay.setImmediate (c); damping.setImmediate (d);
        width.setImmediate (e); initialized = true;
    }
    else
    {
        preDelay.setTarget (a); size.setTarget (b);
        decay.setTarget (c); damping.setTarget (d);
        width.setTarget (e);
    }
}
void Reverb::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    constexpr float lengths[8] { 0.0371f, 0.0413f, 0.0479f, 0.0533f,
                                 0.0617f, 0.0719f, 0.0797f, 0.0893f };
    for (int i = 0; i < numSamples; ++i)
    {
        const float blend = mix.next();
        const float preMs = preDelay.next();
        const float scale = 0.55f + size.next() * 1.0f;
        const float rt60 = decay.next();
        const float damp = damping.next();
        const float spread = width.next();
        const float dry[2] { left[i], right[i] };
        float input[2] {};
        for (int c = 0; c < 2; ++c)
        {
            input[c] = preMs < 0.02f ? detail::safe (dry[c]) : pre[c].read (preMs * 0.001f * rate);
            pre[c].push (detail::safe (dry[c]));
        }
        const float mono = 0.5f * (input[0] + input[1]);
        const float side = 0.5f * (input[0] - input[1]);
        float delayed[8] {}, mixed[8] {};
        for (int line = 0; line < 8; ++line)
        {
            const float modulation = 0.00045f * rate * std::sin (phases[line]);
            const float length = lengths[line] * scale * rate;
            delayed[line] = lines[line].read (length + modulation);
            phases[line] += 2.0f * detail::pi * (0.11f + 0.023f * static_cast<float> (line)) / rate;
            if (phases[line] >= 2.0f * detail::pi) phases[line] -= 2.0f * detail::pi;
            const float cutoff = 14000.0f - 11500.0f * damp;
            const float a = 1.0f - std::exp (-2.0f * detail::pi * cutoff / rate);
            filter[line] += a * (delayed[line] - filter[line]);
            const float gain = std::pow (10.0f, -3.0f * lengths[line] * scale / rt60);
            mixed[line] = gain * filter[line];
        }
        // Orthogonal eight-point Hadamard feedback matrix, scaled by 1/sqrt(8).
        for (int step = 1; step < 8; step *= 2)
            for (int base = 0; base < 8; base += 2 * step)
                for (int j = 0; j < step; ++j)
                {
                    const float x = mixed[base + j];
                    const float y = mixed[base + j + step];
                    mixed[base + j] = x + y;
                    mixed[base + j + step] = x - y;
                }
        for (int line = 0; line < 8; ++line)
        {
            const float injection = mono * 0.14f + (line & 1 ? -side : side) * 0.1f;
            lines[line].push (mixed[line] * 0.35355339f + injection);
        }
        const float mid = (delayed[0] + delayed[2] + delayed[4] + delayed[6]) * 0.20f;
        const float leftSide = (delayed[1] - delayed[3] + delayed[5] - delayed[7]) * 0.20f;
        const float rightSide = (delayed[1] + delayed[3] - delayed[5] - delayed[7]) * 0.20f;
        left[i] = detail::Mix::apply (dry[0], mid + spread * leftSide, blend);
        if (right != left)
            right[i] = detail::Mix::apply (dry[1], mid + spread * rightSide, blend);
    }
}
} // namespace kraftonic::fx
