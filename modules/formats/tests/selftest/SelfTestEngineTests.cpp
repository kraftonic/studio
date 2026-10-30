// SPDX-License-Identifier: MIT
//
// Self-test engine tests. Pins the real-time contract of
// the proof instrument behind the shared VST3 base: silence when idle,
// finite sub-0dBFS output, envelope release, sustain-pedal hold, and the
// immediate all-sound-off path.

#include "SelfTestEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace kraftonic::formats::selftest;

namespace
{

constexpr double kRate = 48000.0;

struct Rendered
{
    std::vector<float> left;
    std::vector<float> right;
};

Rendered render (SelfTestEngine& engine, const std::vector<Event>& events, int samples)
{
    Rendered out {};
    out.left.assign (static_cast<std::size_t> (samples), 0.0F);
    out.right.assign (static_cast<std::size_t> (samples), 0.0F);
    const AudioBlock block {out.left.data(), out.right.data(), samples};
    engine.process (block, events.empty() ? nullptr : events.data(),
                    static_cast<int> (events.size()));
    return out;
}

float peak (const Rendered& out)
{
    float p = 0.0F;
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        const float a = std::fabs (out.left[i]);
        const float b = std::fabs (out.right[i]);
        if (a > p)
            p = a;
        if (b > p)
            p = b;
    }
    return p;
}

} // namespace

TEST_CASE ("selftest engine is silent when idle", "[plugin-selftest]")
{
    SelfTestEngine engine {};
    ProcessSetup setup {};
    setup.sampleRate = kRate;
    setup.maxBlockSize = 512;
    engine.prepare (setup);
    engine.setParameters (SelfTestParameters::defaults());
    CHECK (engine.activeVoiceCount() == 0);
    const Rendered out = render (engine, {}, 512);
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        CHECK (out.left[i] == 0.0F);
        CHECK (out.right[i] == 0.0F);
    }
}

TEST_CASE ("selftest engine renders finite sub-0dBFS chords", "[plugin-selftest]")
{
    SelfTestEngine engine {};
    ProcessSetup setup {};
    setup.sampleRate = kRate;
    setup.maxBlockSize = 512;
    engine.prepare (setup);
    SelfTestParameters params = SelfTestParameters::defaults();
    params.set (ParamId::outputGain, 0.0F);
    engine.setParameters (params);

    std::vector<Event> chord {};
    for (int n = 60; n < 68; ++n)
        chord.push_back (Event {Event::Type::noteOn, 0, n, 1.0F});
    const Rendered out = render (engine, chord, 512);
    CHECK (engine.activeVoiceCount() == 8);
    float p = 0.0F;
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        REQUIRE (std::isfinite (out.left[i]));
        REQUIRE (std::isfinite (out.right[i]));
        const float a = std::fabs (out.left[i]);
        if (a > p)
            p = a;
    }
    CHECK (p > 0.01F); // audibly sounding
    CHECK (p < 1.0F); // under 0 dBFS
    CHECK (peak (out) < 1.0F);
}

TEST_CASE ("selftest engine releases and silences", "[plugin-selftest]")
{
    SelfTestEngine engine {};
    ProcessSetup setup {};
    setup.sampleRate = kRate;
    setup.maxBlockSize = 512;
    engine.prepare (setup);
    engine.setParameters (SelfTestParameters::defaults());

    render (engine, {{Event::Type::noteOn, 0, 64, 0.9F}}, 240);
    CHECK (engine.activeVoiceCount() == 1);
    render (engine, {{Event::Type::noteOff, 0, 64, 0.0F}}, 240);
    // Release is 0.15 s; after a full second the voice must be gone and
    // the engine must output exact silence again.
    render (engine, {}, static_cast<int> (kRate));
    CHECK (engine.activeVoiceCount() == 0);
    const Rendered tail = render (engine, {}, 64);
    CHECK (peak (tail) == 0.0F);
}

TEST_CASE ("selftest engine holds notes under the pedal", "[plugin-selftest]")
{
    SelfTestEngine engine {};
    ProcessSetup setup {};
    setup.sampleRate = kRate;
    setup.maxBlockSize = 512;
    engine.prepare (setup);
    engine.setParameters (SelfTestParameters::defaults());

    render (engine, {{Event::Type::sustainPedal, 0, 0, 1.0F}}, 8);
    render (engine, {{Event::Type::noteOn, 0, 64, 0.9F}}, 240);
    render (engine, {{Event::Type::noteOff, 0, 64, 0.0F}}, 2400);
    CHECK (engine.activeVoiceCount() == 1); // pedal holds it
    render (engine, {{Event::Type::sustainPedal, 0, 0, 0.0F}}, 8);
    render (engine, {}, static_cast<int> (kRate));
    CHECK (engine.activeVoiceCount() == 0);
}

TEST_CASE ("selftest engine all-sound-off is immediate", "[plugin-selftest]")
{
    SelfTestEngine engine {};
    ProcessSetup setup {};
    setup.sampleRate = kRate;
    setup.maxBlockSize = 512;
    engine.prepare (setup);
    engine.setParameters (SelfTestParameters::defaults());

    render (engine, {{Event::Type::noteOn, 0, 64, 0.9F}}, 2400);
    REQUIRE (engine.activeVoiceCount() == 1);
    const Rendered out = render (engine, {{Event::Type::allSoundOff, 0, 0, 0.0F}}, 64);
    CHECK (engine.activeVoiceCount() == 0);
    CHECK (peak (out) == 0.0F);
}

TEST_CASE ("selftest wave switch changes the sound", "[plugin-selftest]")
{
    SelfTestEngine engine {};
    ProcessSetup setup {};
    setup.sampleRate = kRate;
    setup.maxBlockSize = 512;
    engine.prepare (setup);

    SelfTestParameters sine = SelfTestParameters::defaults();
    sine.set (ParamId::wave, 0.0F);
    engine.setParameters (sine);
    const Rendered a = render (engine, {{Event::Type::noteOn, 0, 64, 0.9F}}, 240);
    engine.reset();

    SelfTestParameters square = SelfTestParameters::defaults();
    square.set (ParamId::wave, 1.0F);
    engine.setParameters (square);
    const Rendered b = render (engine, {{Event::Type::noteOn, 0, 64, 0.9F}}, 240);
    bool differ = false;
    for (std::size_t i = 100; i < a.left.size(); ++i)
    {
        if (std::fabs (a.left[i] - b.left[i]) > 1e-4F)
        {
            differ = true;
            break;
        }
    }
    CHECK (differ);
}
