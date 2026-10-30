// SPDX-License-Identifier: MIT
#include "kraftonic/fx/EQ.h"

namespace kraftonic::fx
{
EQ::Coeff EQ::design (int type, float frequency, float gainDb, float q, float rate) noexcept
{
    const float hz = detail::bounded (frequency, 20.0f, rate * 0.45f);
    const float gain = detail::bounded (gainDb, -18.0f, 18.0f);
    const float w = 2.0f * detail::pi * hz / rate;
    const float cs = std::cos (w), sn = std::sin (w);
    const float A = std::pow (10.0f, gain / 40.0f);
    float b0, b1, b2, a0, a1, a2;
    if (type == 1)
    {
        const float alpha = sn / (2.0f * detail::bounded (q, 0.2f, 10.0f));
        b0 = 1.0f + alpha * A; b1 = -2.0f * cs; b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A; a1 = -2.0f * cs; a2 = 1.0f - alpha / A;
    }
    else
    {
        const float root = std::sqrt (2.0f * A) * sn;
        const float plus = A + 1.0f, minus = A - 1.0f;
        if (type == 0)
        {
            b0 = A * (plus - minus * cs + root);
            b1 = 2.0f * A * (minus - plus * cs);
            b2 = A * (plus - minus * cs - root);
            a0 = plus + minus * cs + root;
            a1 = -2.0f * (minus + plus * cs);
            a2 = plus + minus * cs - root;
        }
        else
        {
            b0 = A * (plus + minus * cs + root);
            b1 = -2.0f * A * (minus + plus * cs);
            b2 = A * (plus + minus * cs - root);
            a0 = plus - minus * cs + root;
            a1 = 2.0f * (minus - plus * cs);
            a2 = plus - minus * cs - root;
        }
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}
float EQ::tick (float x, const Coeff& c, State& s) noexcept
{
    const float y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return detail::safe (y);
}
void EQ::prepare (const ProcessSetup& setup) noexcept
{
    rate = static_cast<float> (std::max (22050.0, setup.sampleRate));
    ramp = std::max (1, static_cast<int> (0.01f * rate));
    mix.prepare (rate);
    output.prepare (rate, 0.01);
    initialized = false;
    reset();
}
void EQ::reset() noexcept
{
    for (auto& channel : states)
        for (auto& state : channel) state = {};
}
void EQ::setParameters (const Parameters& p) noexcept
{
    mix.set (p.enabled, 1.0f);
    target[0] = design (0, p.lowFrequencyHz, p.lowGainDb, 1.0f, rate);
    target[1] = design (1, p.midFrequencyHz, p.midGainDb, p.midQ, rate);
    target[2] = design (2, p.highFrequencyHz, p.highGainDb, 1.0f, rate);
    const float gain = detail::dbGain (p.outputDb);
    if (! initialized)
    {
        for (int i = 0; i < 3; ++i) current[i] = target[i];
        output.setImmediate (gain);
        initialized = true;
    }
    else
    {
        for (int i = 0; i < 3; ++i)
        {
            step[i].b0 = (target[i].b0 - current[i].b0) / static_cast<float> (ramp);
            step[i].b1 = (target[i].b1 - current[i].b1) / static_cast<float> (ramp);
            step[i].b2 = (target[i].b2 - current[i].b2) / static_cast<float> (ramp);
            step[i].a1 = (target[i].a1 - current[i].a1) / static_cast<float> (ramp);
            step[i].a2 = (target[i].a2 - current[i].a2) / static_cast<float> (ramp);
        }
        remaining = ramp;
        output.setTarget (gain);
    }
}
void EQ::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        if (remaining > 0)
        {
            for (int band = 0; band < 3; ++band)
            {
                current[band].b0 += step[band].b0;
                current[band].b1 += step[band].b1;
                current[band].b2 += step[band].b2;
                current[band].a1 += step[band].a1;
                current[band].a2 += step[band].a2;
            }
            if (--remaining == 0)
                for (int band = 0; band < 3; ++band) current[band] = target[band];
        }
        const float blend = mix.next();
        const float gain = output.next();
        const float dry[2] { left[i], right[i] };
        float wet[2] {};
        for (int c = 0; c < 2; ++c)
        {
            float v = detail::safe (dry[c]);
            for (int band = 0; band < 3; ++band)
                v = tick (v, current[band], states[c][band]);
            wet[c] = v * gain;
        }
        left[i] = detail::Mix::apply (dry[0], wet[0], blend);
        if (right != left)
            right[i] = detail::Mix::apply (dry[1], wet[1], blend);
    }
}
} // namespace kraftonic::fx
