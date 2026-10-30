// SPDX-License-Identifier: MIT
#include "kraftonic/wavetable/WaveEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace kraftonic::wavetable;

TEST_CASE ("Wavetable parameter table is consistent", "[wave][contract]")
{
    std::set<std::string> keys;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kParamInfo[static_cast<std::size_t> (i)];
        CHECK (static_cast<int> (p.id) == i);
        CHECK (keys.insert (p.key).second);
        CHECK (p.minValue < p.maxValue);
        CHECK (p.defaultValue >= p.minValue);
        CHECK (p.defaultValue <= p.maxValue);
        if (p.scale == ParamScale::logarithmic)
        {
            CHECK (p.minValue > 0.0f);
            CHECK (p.stepCount == 0);
        }
        CHECK (toPlain (p.id, toNormalized (p.id, p.defaultValue)) == Catch::Approx (p.defaultValue).margin (1e-4));
    }
}

TEST_CASE ("Discrete Wavetable parameters land on whole values", "[wave][contract]")
{
    for (double n = 0.0; n <= 1.0; n += 0.05)
    {
        const float wave = toPlain (ParamId::oscATable, n);
        CHECK (wave == std::round (wave));
        const float semis = toPlain (ParamId::oscBSemitone, n);
        CHECK (semis == std::round (semis));
    }
    auto params = WaveParameters::defaults();
    params.set (ParamId::oscAOctave, 1.4f);
    CHECK (params.get (ParamId::oscAOctave) == 1.0f);
}

TEST_CASE ("wavetable engine overwrites output with finite audio", "[wave][contract]")
{
    WaveEngine engine;
    engine.prepare ({ 48000.0, 256 });
    engine.setParameters (WaveParameters::defaults());
    std::vector<float> left (256, 1.0f), right (256, 1.0f); // poison: engine must overwrite
    const Event on { Event::Type::noteOn, 0, 60, 0.8f };
    engine.process ({ left.data(), right.data(), 256 }, &on, 1);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        REQUIRE (std::isfinite (left[i]));
        REQUIRE (std::isfinite (right[i]));
        REQUIRE (std::fabs (left[i]) < 1.0f);
    }
    engine.reset();
    engine.process ({ left.data(), right.data(), 256 }, nullptr, 0);
    for (float s : left)
        REQUIRE (s == 0.0f);
}
