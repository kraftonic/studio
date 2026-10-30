// SPDX-License-Identifier: MIT
// Measured DSP tests: pass-through, echo timing and level, ping-pong,
// damping, drive harmonics, tone, level, presets, sample rates, block sizes.
#include "StarterEffectPresets.h"

#include <kraftonic/formats/Preset.h>
#include "StarterEffectEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

using namespace startereffect;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

struct Stereo
{
    std::vector<float> left, right;
};

/** Runs `input` (both channels, or left only) through a fresh engine in blocks. */
Stereo run (const StarterEffectParameters& params, const std::vector<float>& input, bool leftOnly = false,
            int blockSize = 256, double rate = kRate)
{
    StarterEffectEngine engine;
    engine.prepare ({rate, blockSize});
    Stereo out {input, leftOnly ? std::vector<float> (input.size()) : input};
    for (std::size_t start = 0; start < input.size(); start += static_cast<std::size_t> (blockSize))
    {
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blockSize), input.size() - start));
        engine.setParameters (params);
        engine.process ({out.left.data() + start, out.right.data() + start, n});
    }
    return out;
}

std::vector<float> impulse (double seconds, double rate = kRate)
{
    std::vector<float> x (static_cast<std::size_t> (seconds * rate));
    x[0] = 1.0F;
    return x;
}

std::vector<float> sine (double hz, float amplitude, double seconds, double burst = 1e9)
{
    std::vector<float> x (static_cast<std::size_t> (seconds * kRate));
    for (std::size_t i = 0; i < x.size() && static_cast<double> (i) < burst * kRate; ++i)
        x[i] = amplitude * static_cast<float> (std::sin (2.0 * kPi * hz * static_cast<double> (i) / kRate));
    return x;
}

float peak (const std::vector<float>& x, double from = 0.0, double to = 1e9)
{
    float result = 0.0F;
    for (auto i = static_cast<std::size_t> (from * kRate); i < std::min (x.size(), static_cast<std::size_t> (to * kRate)); ++i)
        result = std::max (result, std::abs (x[i]));
    return result;
}

std::size_t loudestSample (const std::vector<float>& x, std::size_t from)
{
    return static_cast<std::size_t> (std::max_element (x.begin() + static_cast<long> (from), x.end(),
                                                       [] (float a, float b) { return std::abs (a) < std::abs (b); })
                                     - x.begin());
}

/** Amplitude of the `hz` component (Goertzel). */
double magnitudeAt (const std::vector<float>& x, double hz, double from, double to)
{
    const auto a = static_cast<std::size_t> (from * kRate), b = static_cast<std::size_t> (to * kRate);
    double re = 0.0, im = 0.0;
    for (auto i = a; i < b; ++i)
    {
        re += x[i] * std::cos (2.0 * kPi * hz * static_cast<double> (i) / kRate);
        im += x[i] * std::sin (2.0 * kPi * hz * static_cast<double> (i) / kRate);
    }
    return 2.0 * std::hypot (re, im) / static_cast<double> (b - a);
}

/** Wet echoes only, no drive: what the delay alone does to the input. */
StarterEffectParameters echoesOnly (float timeMs, float feedback)
{
    auto p = StarterEffectParameters::defaults();
    p.set (ParamId::drive, 0.0F);
    p.set (ParamId::mix, 1.0F);
    p.set (ParamId::delayTime, timeMs);
    p.set (ParamId::feedback, feedback);
    p.set (ParamId::damping, 18000.0F);
    return p;
}
} // namespace

TEST_CASE ("no drive and no mix is bit-exact pass-through")
{
    auto p = StarterEffectParameters::defaults();
    p.set (ParamId::drive, 0.0F);
    p.set (ParamId::mix, 0.0F);
    const auto input = sine (440.0, 0.5F, 0.5);
    const auto out = run (p, input);
    CHECK (out.left == input);
    CHECK (out.right == input);
}

TEST_CASE ("the first echo arrives after the delay time and each repeat is feedback times quieter")
{
    const auto out = run (echoesOnly (250.0F, 0.5F), impulse (1.0));
    const auto first = loudestSample (out.left, 1);
    CHECK (static_cast<double> (first) == Catch::Approx (0.25 * kRate).margin (1.0));
    const float firstLevel = std::abs (out.left[first]);
    const float secondLevel = peak (out.left, 0.49, 0.51);
    CHECK (firstLevel > 0.9F);
    CHECK (secondLevel / firstLevel == Catch::Approx (0.5).margin (0.08)); // damping takes a little
}

TEST_CASE ("ping-pong sends the repeats to the other side")
{
    auto p = echoesOnly (200.0F, 0.6F);
    p.set (ParamId::pingPong, 1.0F);
    const auto out = run (p, impulse (0.7), true); // impulse in the left channel only
    CHECK (peak (out.left, 0.19, 0.21) > 0.9F);    // first echo: left
    CHECK (peak (out.right, 0.19, 0.21) < 1e-6F);
    CHECK (peak (out.right, 0.39, 0.41) > 0.4F);   // second echo: right
    CHECK (peak (out.left, 0.39, 0.41) < 1e-3F);
}

TEST_CASE ("damping darkens the repeats")
{
    auto bright = echoesOnly (200.0F, 0.7F);
    auto dark = bright;
    dark.set (ParamId::damping, 1000.0F);
    const auto burst = sine (6000.0, 0.5F, 0.6, 0.05); // 50 ms of 6 kHz
    // The second repeat has passed the damping filter once: a 1 kHz one-pole
    // low-pass takes 6 kHz down by about 11 dB more than an open one.
    CHECK (peak (run (dark, burst).left, 0.4, 0.45) < 0.35F * peak (run (bright, burst).left, 0.4, 0.45));
}

TEST_CASE ("drive adds harmonics and tone sets how bright they are")
{
    auto p = StarterEffectParameters::defaults();
    p.set (ParamId::mix, 0.0F);
    p.set (ParamId::drive, 0.0F);
    const auto clean = run (p, sine (440.0, 0.5F, 0.5)).left;
    p.set (ParamId::drive, 0.8F);
    p.set (ParamId::tone, 1.0F);
    const auto driven = run (p, sine (440.0, 0.5F, 0.5)).left;
    p.set (ParamId::tone, 0.0F);
    const auto dull = run (p, sine (440.0, 0.5F, 0.5)).left;

    const double fundamental = magnitudeAt (driven, 440.0, 0.1, 0.5);
    CHECK (magnitudeAt (clean, 1320.0, 0.1, 0.5) < 1e-4);          // no 3rd harmonic when clean
    CHECK (magnitudeAt (driven, 1320.0, 0.1, 0.5) > 0.05 * fundamental); // above -26 dB when driven
    CHECK (magnitudeAt (dull, 3960.0, 0.1, 0.5) < 0.5 * magnitudeAt (driven, 3960.0, 0.1, 0.5));
}

TEST_CASE ("output level scales the signal")
{
    auto p = StarterEffectParameters::defaults();
    p.set (ParamId::drive, 0.0F);
    p.set (ParamId::mix, 0.0F);
    p.set (ParamId::outputGain, -6.0F);
    CHECK (peak (run (p, sine (440.0, 0.5F, 0.3)).left, 0.1) == Catch::Approx (0.5 * 0.501).epsilon (0.01));
}

TEST_CASE ("every preset keeps a -6 dBFS input under 0 dBFS")
{
    for (const auto& preset : plugin::kPresets)
    {
        auto p = StarterEffectParameters::defaults();
        for (std::size_t i = 0; i < preset.count; ++i)
            for (const auto& info : kParamInfo)
                if (std::string_view (info.key) == preset.values[i].key)
                    p.set (info.id, preset.values[i].plain);
        const auto out = run (p, sine (220.0, 0.5F, 2.0));
        INFO (preset.name);
        CHECK (peak (out.left) < 1.0F);
        CHECK (peak (out.right) < 1.0F);
    }
}

TEST_CASE ("extreme settings stay finite and reset clears the echoes")
{
    auto p = StarterEffectParameters::defaults();
    for (const auto& info : kParamInfo)
        p.set (info.id, info.maxValue);
    StarterEffectEngine engine;
    engine.prepare ({kRate, 512});
    auto x = sine (100.0, 1.0F, 0.5);
    auto y = x;
    for (std::size_t s = 0; s < x.size(); s += 512)
    {
        engine.setParameters (p);
        engine.process ({x.data() + s, y.data() + s, static_cast<int> (std::min<std::size_t> (512, x.size() - s))});
    }
    for (float v : x)
        REQUIRE (std::isfinite (v));
    for (float v : y)
        REQUIRE (std::isfinite (v));
    engine.reset();
    std::vector<float> silence (48000), silenceR (48000);
    for (std::size_t s = 0; s < silence.size(); s += 512)
        engine.process ({silence.data() + s, silenceR.data() + s, static_cast<int> (std::min<std::size_t> (512, silence.size() - s))});
    CHECK (peak (silence) == 0.0F);
    CHECK (peak (silenceR) == 0.0F);
}

TEST_CASE ("output does not depend on the host block size")
{
    const auto input = sine (330.0, 0.5F, 0.6, 0.2);
    auto p = StarterEffectParameters::defaults();
    p.set (ParamId::pingPong, 1.0F);
    const auto a = run (p, input, false, 64);
    const auto b = run (p, input, false, 333);
    CHECK (a.left == b.left);
    CHECK (a.right == b.right);
}

TEST_CASE ("echo timing holds at every supported sample rate")
{
    for (double rate : {22050.0, 44100.0, 96000.0, 384000.0})
    {
        const auto out = run (echoesOnly (100.0F, 0.0F), impulse (0.2, rate), false, 512, rate);
        INFO (rate);
        CHECK (static_cast<double> (loudestSample (out.left, 1)) == Catch::Approx (0.1 * rate).margin (1.0));
    }
}
