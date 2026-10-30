// SPDX-License-Identifier: MIT
#include "kraftonic/fx/Delay.h"

namespace kraftonic::fx
{
void Delay::prepare (const ProcessSetup& setup)
{
    rate = static_cast<float> (std::max (22050.0, setup.sampleRate));
    for (auto& line : lines) line.prepare (static_cast<int> (rate * 2.0f) + 4);
    mix.prepare (rate);
    time.prepare (rate, 0.05);
    feedback.prepare (rate, 0.02);
    lowCut.prepare (rate, 0.02);
    highCut.prepare (rate, 0.02);
    initialized = false;
    reset();
}
void Delay::reset() noexcept
{
    for (auto& line : lines) line.reset();
    low[0] = low[1] = highState[0] = highState[1] = 0.0f;
}
void Delay::updateTime() noexcept
{
    const float ms = params.tempoSync ? 60000.0f * detail::bounded (params.noteDivision, 0.0625f, 4.0f)
                                           / static_cast<float> (tempo)
                                      : detail::bounded (params.timeMs, 1.0f, 2000.0f);
    const float samples = std::clamp (ms * 0.001f * rate, 1.0f, rate * 2.0f);
    if (! initialized) time.setImmediate (samples);
    else time.setTarget (samples);
}
void Delay::setTempo (double bpm) noexcept
{
    tempo = std::isfinite (bpm) ? std::clamp (bpm, 20.0, 300.0) : 120.0;
    updateTime();
}
void Delay::setParameters (const Parameters& value) noexcept
{
    params = value;
    mix.set (value.enabled, value.mix);
    updateTime();
    const float f = detail::bounded (value.feedback, 0.0f, 0.95f);
    const float lo = detail::bounded (value.lowCutHz, 10.0f, 2000.0f);
    const float hi = detail::bounded (value.highCutHz, 500.0f, 18000.0f);
    if (! initialized)
    {
        feedback.setImmediate (f); lowCut.setImmediate (lo); highCut.setImmediate (hi);
        initialized = true;
    }
    else
    {
        feedback.setTarget (f); lowCut.setTarget (lo); highCut.setTarget (hi);
    }
}
void Delay::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        const float blend = mix.next();
        const float length = time.next();
        const float fb = feedback.next();
        const float lo = lowCut.next();
        const float hi = highCut.next();
        const float lowA = 1.0f - std::exp (-2.0f * detail::pi * lo / rate);
        const float highA = 1.0f - std::exp (-2.0f * detail::pi * hi / rate);
        const float dry[2] { left[i], right[i] };
        float wet[2] { lines[0].read (length), lines[1].read (length) };
        float filtered[2] {};
        for (int c = 0; c < 2; ++c)
        {
            highState[c] = detail::safe (highState[c] + highA * (wet[c] - highState[c]));
            low[c] = detail::safe (low[c] + lowA * (highState[c] - low[c]));
            filtered[c] = highState[c] - low[c];
        }
        for (int c = 0; c < 2; ++c)
            lines[c].push (detail::safe (dry[c]) + fb * filtered[params.pingPong ? 1 - c : c]);
        left[i] = detail::Mix::apply (dry[0], wet[0], blend);
        if (right != left)
            right[i] = detail::Mix::apply (dry[1], wet[1], blend);
    }
}
} // namespace kraftonic::fx
