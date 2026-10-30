// SPDX-License-Identifier: MIT
#include <kraftonic/core/Adsr.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using kraftonic::core::Adsr;
using Catch::Matchers::WithinAbs;

namespace
{
Adsr makeEnvelope (double sr, Adsr::Parameters p)
{
    Adsr env;
    env.prepare (sr);
    env.setParameters (p);
    return env;
}

int samples (double seconds, double sr) { return static_cast<int> (seconds * sr); }
} // namespace

TEST_CASE ("ADSR is silent until triggered", "[adsr]")
{
    auto env = makeEnvelope (48000.0, {});
    for (int i = 0; i < 1000; ++i)
        REQUIRE (env.process() == 0.0f);
    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("ADSR runs attack, decay, sustain and release in time", "[adsr]")
{
    const double sr = GENERATE (44100.0, 48000.0, 96000.0);
    auto env = makeEnvelope (sr, { 0.01f, 0.05f, 0.5f, 0.1f });

    env.noteOn();
    float peak = 0.0f;
    for (int i = 0; i < samples (0.011, sr); ++i)
        peak = std::max (peak, env.process());
    REQUIRE_THAT (peak, WithinAbs (1.0, 1e-6));

    for (int i = 0; i < samples (0.06, sr); ++i)
        env.process();
    REQUIRE (env.getStage() == Adsr::Stage::sustain);
    REQUIRE_THAT (env.getLevel(), WithinAbs (0.5, 1e-3));

    env.noteOff();
    for (int i = 0; i < samples (0.11, sr); ++i)
        env.process();
    REQUIRE_FALSE (env.isActive());
    REQUIRE (env.getLevel() == 0.0f);
}

TEST_CASE ("ADSR retrigger continues from the current level (no click)", "[adsr]")
{
    auto env = makeEnvelope (48000.0, { 0.01f, 0.1f, 0.6f, 0.5f });
    env.noteOn();
    for (int i = 0; i < 4800; ++i)
        env.process();
    env.noteOff();
    for (int i = 0; i < 2400; ++i)
        env.process();

    const float before = env.getLevel();
    REQUIRE (before > 0.05f);
    env.noteOn();
    const float after = env.process();
    REQUIRE (std::abs (after - before) < 0.01f);
}

TEST_CASE ("ADSR per-sample steps stay small even with the shortest times", "[adsr]")
{
    // Minimum times are clamped so a full-scale jump never happens in one sample.
    auto env = makeEnvelope (48000.0, { 0.0f, 0.0f, 0.0f, 0.0f });
    env.noteOn();
    float prev = 0.0f;
    float maxStep = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        if (i == 2400)
            env.noteOff();
        const float v = env.process();
        maxStep = std::max (maxStep, std::abs (v - prev));
        prev = v;
    }
    REQUIRE (maxStep < 0.1f);
}
