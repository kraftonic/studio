// SPDX-License-Identifier: MIT
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <random>

using namespace kraftonic::sampler;
using namespace kraftonic::sampler::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
std::unique_ptr<Sampler> makeSampler (std::shared_ptr<const SampleBank> bank, double rate = 48000.0)
{
    auto sampler = std::make_unique<Sampler>();
    sampler->prepare ({ rate, 512 });
    sampler->setBank (std::move (bank));
    sampler->setParameters ({});
    return sampler;
}

/** DC samples whose level identifies the zone that plays. */
std::shared_ptr<SampleBank> dcBank (std::initializer_list<float> levels, int frames = 48000)
{
    auto bank = std::make_shared<SampleBank>();
    plainSettings (*bank);
    for (float level : levels)
        bank->addSample (Sample::fromFloat (constant (level, frames), 48000));
    return bank;
}

float levelAt (const Render& r, int from, int to)
{
    return rms (r.left, static_cast<std::size_t> (from), static_cast<std::size_t> (to));
}

/** Residual of the best-fitting sine at `hz` relative to the signal, in dB. */
double sineResidualDb (const std::vector<float>& x, std::size_t from, std::size_t count, double hz, double rate)
{
    double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
    for (std::size_t i = from; i < from + count; ++i)
    {
        const double ph = 2.0 * kPi * hz * static_cast<double> (i) / rate;
        const double s = std::sin (ph), c = std::cos (ph);
        ss += s * s;
        cc += c * c;
        sc += s * c;
        xs += x[i] * s;
        xc += x[i] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (xs * cc - xc * sc) / det;
    const double b = (xc * ss - xs * sc) / det;
    double residual = 0, signal = 0;
    for (std::size_t i = from; i < from + count; ++i)
    {
        const double ph = 2.0 * kPi * hz * static_cast<double> (i) / rate;
        const double fit = a * std::sin (ph) + b * std::cos (ph);
        residual += (x[i] - fit) * (x[i] - fit);
        signal += fit * fit;
    }
    return 10.0 * std::log10 (residual / signal);
}
} // namespace

TEST_CASE ("zone selection by key and velocity", "[zones]")
{
    auto bank = dcBank ({ 0.1f, 0.2f, 0.3f });
    bank->addZone (zone (0, 0, 59, 60));          // low keys
    bank->addZone (zone (1, 60, 127, 60, 1, 64)); // high keys, soft
    bank->addZone (zone (2, 60, 127, 60, 65, 127)); // high keys, hard
    auto sampler = makeSampler (finish (bank));

    struct Case
    {
        int note;
        float velocity;
        float expected;
    };
    for (const Case c : { Case { 40, 1.0f, 0.1f }, Case { 59, 0.2f, 0.1f }, Case { 60, 0.3f, 0.2f },
                          Case { 72, 64.0f / 127.0f, 0.2f }, Case { 72, 65.0f / 127.0f, 0.3f },
                          Case { 100, 1.0f, 0.3f } })
    {
        sampler->reset();
        // Every zone is rooted at 60 and a DC sample has no pitch, so the level names the zone.
        const auto out = render (*sampler, { noteOn (0, c.note, c.velocity) }, 2000);
        CHECK_THAT (levelAt (out, 500, 2000), WithinAbs (c.expected, 1e-3));
    }
}

TEST_CASE ("events are sample accurate and the output ignores block size", "[timing]")
{
    auto bank = dcBank ({ 0.25f });
    bank->addZone (zone (0, 0, 127, 60));
    bank->settings().attackMs = 0.0f;
    auto shared = finish (bank);

    auto sampler = makeSampler (shared);
    const auto out = render (*sampler, { noteOn (300, 60, 1.0f) }, 1024, 256);
    CHECK (out.left[299] == 0.0f);
    CHECK (out.left[300] != 0.0f); // starts on the exact sample, mid-block
    CHECK (out.right[300] == out.left[300]);

    // A busy performance rendered with 512-sample blocks and with random blocks.
    auto pianoish = std::make_shared<SampleBank>();
    pianoish->settings().velocityCrossfade = 12.0f;
    pianoish->settings().pedalResonance = 0.4f;
    pianoish->addSample (Sample::fromFloat (sine (220.0, 44100.0, 30000), 44100));
    pianoish->addSample (Sample::fromFloat (sine (330.0, 44100.0, 30000, 0.7f), 44100));
    pianoish->addSample (Sample::fromFloat (constant (0.05f, 2000), 44100));
    pianoish->addZone (zone (0, 0, 127, 57, 1, 70));
    pianoish->addZone (zone (1, 0, 127, 64, 71, 127));
    Zone release = zone (2, 0, 127, 60);
    release.trigger = Trigger::release;
    pianoish->addZone (release);
    auto shared2 = finish (pianoish);

    std::vector<Event> events;
    std::mt19937 random (7);
    for (int i = 0; i < 60; ++i)
    {
        const int at = i * 700 + static_cast<int> (random() % 300);
        events.push_back (noteOn (at, 40 + static_cast<int> (random() % 40), 0.2f + 0.8f * static_cast<float> (random() % 100) / 100.0f));
        events.push_back (noteOff (at + 200 + static_cast<int> (random() % 2000), 40 + static_cast<int> (random() % 40)));
        if (i % 9 == 0)
            events.push_back (pedal (at + 13, i % 18 == 0));
        if (i % 7 == 0)
            events.push_back ({ Event::Type::pitchBend, at + 5, 0, static_cast<float> (random() % 200) / 100.0f - 1.0f });
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sampleOffset < b.sampleOffset; });

    auto a = makeSampler (shared2);
    const auto big = render (*a, events, 48000, 512);
    auto b = makeSampler (shared2);
    Render small;
    {
        small.left.assign (48000, 0.0f);
        small.right.assign (48000, 0.0f);
        std::size_t next = 0;
        int at = 0;
        std::vector<Event> local;
        while (at < 48000)
        {
            const int n = std::min (48000 - at, 1 + static_cast<int> (random() % 333));
            local.clear();
            while (next < events.size() && events[next].sampleOffset < at + n)
            {
                Event e = events[next++];
                e.sampleOffset -= at;
                local.push_back (e);
            }
            b->process ({ small.left.data() + at, small.right.data() + at, n }, local.data(), static_cast<int> (local.size()));
            at += n;
        }
    }
    float worst = 0.0f;
    for (std::size_t i = 0; i < big.left.size(); ++i)
        worst = std::max ({ worst, std::abs (big.left[i] - small.left[i]), std::abs (big.right[i] - small.right[i]) });
    CHECK (worst < 1.0e-6f);
    CHECK (rms (big.left, 0, 48000) > 0.01f);
}

TEST_CASE ("interpolation: a transposed sine stays a clean sine", "[interpolation]")
{
    const double sourceRate = 44100.0;
    const auto [shift, hz, hostRate] = GENERATE (std::tuple { 3, 1000.0, 48000.0 }, std::tuple { -5, 1000.0, 48000.0 },
                                                 std::tuple { 2, 5000.0, 48000.0 }, std::tuple { 0, 440.0, 96000.0 },
                                                 std::tuple { 7, 300.0, 44100.0 }, std::tuple { -12, 2000.0, 48000.0 });
    auto bank = std::make_shared<SampleBank>();
    plainSettings (*bank);
    bank->addSample (Sample::fromFloat (sine (hz, sourceRate, 90000), static_cast<int> (sourceRate)));
    bank->addZone (zone (0, 0, 127, 69));
    auto sampler = makeSampler (finish (bank), hostRate);
    const auto out = render (*sampler, { noteOn (0, 69 + shift, 1.0f) }, 32768 + 4000);
    const double expectedHz = hz * std::exp2 (shift / 12.0);
    const double residual = sineResidualDb (out.left, 2000, 32768, expectedHz, hostRate);
    INFO ("shift " << shift << " st, " << hz << " Hz at " << hostRate << " Hz: residual " << residual << " dB");
    CHECK (residual < -85.0); // the 16-bit source's own noise floor is about -92 dB
    // Level is preserved (passband is flat well below the cutoff).
    CHECK_THAT (rms (out.left, 2000, 34768), WithinRel (0.5f / std::sqrt (2.0f), 0.01f));
}

TEST_CASE ("interpolation: content above the output band is attenuated, not aliased", "[interpolation]")
{
    // A 19 kHz tone at 44.1 kHz transposed up 4 semitones would land at 23.9 kHz:
    // above the 22.05 kHz output Nyquist, so it must be filtered out, not folded back.
    auto bank = std::make_shared<SampleBank>();
    plainSettings (*bank);
    bank->addSample (Sample::fromFloat (sine (19000.0, 44100.0, 60000), 44100));
    bank->addZone (zone (0, 0, 127, 69));
    auto sampler = makeSampler (finish (bank), 44100.0);
    const auto out = render (*sampler, { noteOn (0, 73, 1.0f) }, 40000);
    const float level = rms (out.left, 4000, 36000);
    CHECK (20.0f * std::log10 (std::max (level, 1e-9f) / (0.5f / std::sqrt (2.0f))) < -55.0f);
}

TEST_CASE ("interpolation: the audible band stays flat", "[interpolation]")
{
    // 44.1 kHz recordings played untransposed at 48 kHz: the common case.
    for (const double hz : { 100.0, 4000.0, 12000.0, 16000.0 })
    {
        auto bank = std::make_shared<SampleBank>();
        plainSettings (*bank);
        bank->addSample (Sample::fromFloat (sine (hz, 44100.0, 30000), 44100));
        bank->addZone (zone (0, 0, 127, 69));
        auto sampler = makeSampler (finish (bank), 48000.0);
        const auto out = render (*sampler, { noteOn (0, 69, 1.0f) }, 24000);
        const float db = 20.0f * std::log10 (rms (out.left, 2000, 22000) / (0.5f / std::sqrt (2.0f)));
        INFO (hz << " Hz: " << db << " dB");
        CHECK (db > -1.0f);
        CHECK (db < 0.1f);
    }
}

TEST_CASE ("velocity layers crossfade monotonically", "[velocity]")
{
    auto bank = dcBank ({ 0.1f, 0.3f, 0.6f });
    bank->settings().velocityCrossfade = 20.0f;
    bank->settings().velocityRangeDb = 6.0f;
    bank->addZone (zone (0, 0, 127, 60, 1, 40));
    bank->addZone (zone (1, 0, 127, 60, 41, 90));
    bank->addZone (zone (2, 0, 127, 60, 91, 127));
    auto sampler = makeSampler (finish (bank));

    float previous = 0.0f;
    int maxVoices = 0;
    for (int v = 1; v <= 127; ++v)
    {
        sampler->reset();
        const auto out = render (*sampler, { noteOn (0, 60, static_cast<float> (v) / 127.0f) }, 600);
        maxVoices = std::max (maxVoices, sampler->activeVoiceCount());
        const float level = levelAt (out, 100, 600);
        INFO ("velocity " << v);
        CHECK (level >= previous - 1e-6f);
        previous = level;
    }
    CHECK (maxVoices == 2); // inside a crossfade both layers sound
    // Away from the crossfades exactly one layer plays.
    sampler->reset();
    auto out = render (*sampler, { noteOn (0, 60, 20.0f / 127.0f) }, 600);
    CHECK (sampler->activeVoiceCount() == 1);
    sampler->reset();
    out = render (*sampler, { noteOn (0, 60, 1.0f) }, 600);
    CHECK_THAT (levelAt (out, 100, 600), WithinAbs (0.6, 1e-3));
}

TEST_CASE ("equal-power crossfades keep uncorrelated layers' level constant", "[velocity]")
{
    // Two independent noise recordings of equal level: a linear crossfade dips
    // by 3 dB in the middle, an equal-power one doesn't.
    auto make = [] (bool power)
    {
        auto bank = std::make_shared<SampleBank>();
        plainSettings (*bank);
        std::mt19937 random (power ? 1 : 2);
        for (int layer = 0; layer < 2; ++layer)
        {
            std::vector<std::vector<float>> noise (1, std::vector<float> (48000));
            for (auto& v : noise[0])
                v = 0.3f * (static_cast<float> (random() % 20001) / 10000.0f - 1.0f);
            bank->addSample (Sample::fromFloat (noise, 48000));
        }
        bank->addZone (zone (0, 0, 127, 60, 1, 63));
        bank->addZone (zone (1, 0, 127, 60, 64, 127));
        bank->settings().velocityCrossfade = 40.0f;
        bank->settings().equalPowerCrossfade = power;
        return makeSampler (finish (bank));
    };
    for (const bool power : { false, true })
    {
        auto sampler = make (power);
        const float edge = levelAt (render (*sampler, { noteOn (0, 60, 20.0f / 127.0f) }, 24000), 1000, 24000);
        sampler->reset();
        const float middle = levelAt (render (*sampler, { noteOn (0, 60, 64.0f / 127.0f) }, 24000), 1000, 24000);
        const float dipDb = 20.0f * std::log10 (middle / edge);
        INFO ((power ? "power" : "linear") << " dip " << dipDb << " dB");
        if (power)
            CHECK (std::abs (dipDb) < 0.5f);
        else
            CHECK (dipDb < -2.0f);
    }
}

TEST_CASE ("release triggers fire when the damper falls, quieter after a long hold", "[release]")
{
    auto bank = dcBank ({ 0.2f, 0.1f }, 96000);
    bank->addZone (zone (0, 0, 127, 60));
    Zone release = zone (1, 0, 127, 60);
    release.trigger = Trigger::release;
    release.releaseDecayDbPerSecond = 6.0f;
    bank->addZone (release);
    bank->settings().releaseMs = 5.0f;
    auto sampler = makeSampler (finish (bank));

    auto out = render (*sampler, { noteOn (0, 60, 1.0f), noteOff (4800, 60) }, 20000);
    CHECK_THAT (levelAt (out, 1000, 4000), WithinAbs (0.2, 1e-3));
    const float shortHold = levelAt (out, 8000, 20000); // attack voice gone, release zone playing
    CHECK_THAT (shortHold, WithinRel (0.1f * std::pow (10.0f, -6.0f * 0.1f / 20.0f), 0.01f));

    sampler->reset();
    out = render (*sampler, { noteOn (0, 60, 1.0f), noteOff (48000, 60) }, 64000);
    const float longHold = levelAt (out, 52000, 64000);
    CHECK_THAT (longHold, WithinRel (0.1f * std::pow (10.0f, -6.0f / 20.0f), 0.01f));
    CHECK (longHold < shortHold);
}

TEST_CASE ("sustain pedal holds released notes until it lifts", "[pedal]")
{
    auto bank = dcBank ({ 0.2f, 0.05f, 0.03f }, 96000);
    bank->addZone (zone (0, 0, 127, 60));
    Zone release = zone (1, 0, 127, 60);
    release.trigger = Trigger::release;
    bank->addZone (release);
    Zone pedalNoise = zone (2, 0, 127, 60);
    pedalNoise.trigger = Trigger::pedalDown;
    bank->addZone (pedalNoise);
    bank->settings().releaseMs = 20.0f;
    auto sampler = makeSampler (finish (bank));

    const auto out = render (*sampler, { pedal (0, true), noteOn (100, 60, 1.0f), noteOff (2000, 60),
                                         pedal (20000, false) }, 40000);
    CHECK_THAT (levelAt (out, 50, 100), WithinAbs (0.03, 1e-3)); // pedal-down noise alone
    CHECK_THAT (levelAt (out, 5000, 19000), WithinAbs (0.23, 2e-3)); // note held by the pedal, no release noise yet
    CHECK_THAT (levelAt (out, 25000, 40000), WithinAbs (0.08, 2e-3)); // note gone; release + pedal noise remain

    // Re-pressing a key that the pedal holds keeps it held; lifting the key alone doesn't release it.
    sampler->reset();
    const auto again = render (*sampler, { pedal (0, true), noteOn (0, 60, 1.0f), noteOff (1000, 60),
                                           noteOn (3000, 60, 1.0f), noteOff (4000, 60) }, 12000);
    CHECK (sampler->activeVoiceCount() >= 1);
    CHECK (levelAt (again, 10000, 12000) > 0.15f);
}

TEST_CASE ("round robins alternate", "[zones]")
{
    auto bank = dcBank ({ 0.1f, 0.2f, 0.3f });
    for (int s = 0; s < 3; ++s)
    {
        Zone z = zone (s, 0, 127, 60);
        z.roundRobinSet = 1;
        bank->addZone (z);
    }
    auto sampler = makeSampler (finish (bank));
    std::vector<float> levels;
    for (int i = 0; i < 6; ++i)
    {
        const auto out = render (*sampler, { noteOn (0, 60, 1.0f), noteOff (1000, 60) }, 6000);
        levels.push_back (levelAt (out, 200, 900));
    }
    CHECK_THAT (levels[0], WithinAbs (0.1, 1e-3));
    CHECK_THAT (levels[1], WithinAbs (0.2, 1e-3));
    CHECK_THAT (levels[2], WithinAbs (0.3, 1e-3));
    CHECK_THAT (levels[3], WithinAbs (0.1, 1e-3));
}

TEST_CASE ("voice stealing and retriggering never click", "[voices]")
{
    auto bank = dcBank ({ 0.25f }, 96000);
    bank->addZone (zone (0, 0, 127, 60));
    bank->settings().attackMs = 5.0f;
    bank->settings().polyphony = 2;
    auto sampler = makeSampler (finish (bank));

    std::vector<Event> events;
    for (int i = 0; i < 12; ++i)
        events.push_back (noteOn (i * 2000, 50 + i, 1.0f));
    for (int i = 0; i < 6; ++i)
        events.push_back (noteOn (26000 + i * 1500, 70, 1.0f)); // the same key struck again and again
    const auto out = render (*sampler, events, 40000);
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < out.left.size(); ++i)
        maxStep = std::max (maxStep, std::abs (out.left[i] - out.left[i - 1]));
    CHECK (maxStep < 0.01f); // a hard cut of one voice would jump by 0.25
    CHECK (sampler->activeVoiceCount() <= 4);
    CHECK (levelAt (out, 20000, 24000) > 0.45f); // two voices still sound
}

TEST_CASE ("loops sustain while held; undamped zones ring through note-off", "[loop]")
{
    auto bank = std::make_shared<SampleBank>();
    plainSettings (*bank);
    bank->addSample (Sample::fromFloat (sine (480.0, 48000.0, 4800), 48000, 1000, 4800)); // 38 whole cycles: seamless
    bank->addSample (Sample::fromFloat (constant (0.2f, 48000), 48000));
    bank->addZone (zone (0, 0, 59, 60));
    Zone undamped = zone (1, 60, 127, 60);
    undamped.damped = false;
    bank->addZone (undamped);
    bank->settings().releaseMs = 30.0f;
    auto sampler = makeSampler (finish (bank));

    auto out = render (*sampler, { noteOn (0, 48, 1.0f), noteOff (40000, 48) }, 48000);
    CHECK (levelAt (out, 30000, 39000) > 0.3f); // far beyond the 0.1 s sample
    CHECK (levelAt (out, 44000, 48000) < 1e-4f);
    CHECK (sineResidualDb (out.left, 2000, 30000, 240.0, 48000.0) < -70.0); // loop seam is inaudible

    sampler->reset();
    out = render (*sampler, { noteOn (0, 72, 1.0f), noteOff (1000, 72) }, 20000);
    CHECK_THAT (levelAt (out, 10000, 20000), WithinAbs (0.2, 1e-3));
}

TEST_CASE ("pedal resonance adds sustain only while the pedal is down", "[pedal][resonance]")
{
    auto bank = std::make_shared<SampleBank>();
    plainSettings (*bank);
    std::vector<std::vector<float>> hit (1, std::vector<float> (24000));
    std::mt19937 random (3);
    for (std::size_t i = 0; i < hit[0].size(); ++i)
        hit[0][i] = 0.4f * std::exp (-static_cast<float> (i) / 3000.0f) * (static_cast<float> (random() % 2001) / 1000.0f - 1.0f);
    bank->addSample (Sample::fromFloat (hit, 48000));
    bank->addZone (zone (0, 0, 127, 60));
    bank->settings().pedalResonance = 1.0f;
    auto sampler = makeSampler (finish (bank));

    const auto withPedal = render (*sampler, { pedal (0, true), noteOn (0, 60, 1.0f) }, 72000);
    sampler->reset();
    const auto without = render (*sampler, { noteOn (0, 60, 1.0f) }, 72000);
    const float tailWith = levelAt (withPedal, 30000, 72000);
    const float tailWithout = levelAt (without, 30000, 72000);
    CHECK (tailWithout < 1e-6f);
    CHECK (tailWith > 1e-4f);
    float peak = 0.0f;
    for (float v : withPedal.left)
        peak = std::max (peak, std::abs (v));
    CHECK (peak < 1.0f);
}

TEST_CASE ("output stays finite with hostile parameters and event storms", "[robustness]")
{
    auto bank = dcBank ({ 0.3f, 0.2f }, 4000);
    bank->addZone (zone (0, 0, 127, 60));
    Zone release = zone (1, 0, 127, 60);
    release.trigger = Trigger::release;
    bank->addZone (release);
    bank->settings().pedalResonance = 1.0f;
    auto sampler = makeSampler (finish (bank), 22050.0);
    Sampler::Parameters p;
    p.gainDb = std::numeric_limits<float>::quiet_NaN();
    p.tuneCents = std::numeric_limits<float>::infinity();
    p.pitchBendSemitones = 1e30f;
    p.releaseScale = -5.0f;
    p.stereoWidth = std::numeric_limits<float>::quiet_NaN();
    p.polyphony = 100000;
    sampler->setParameters (p);

    std::vector<Event> events;
    std::mt19937 random (11);
    for (int i = 0; i < 512; ++i)
    {
        Event e;
        e.type = static_cast<Event::Type> (random() % 7);
        e.sampleOffset = static_cast<int> (random() % 4096);
        e.note = static_cast<int> (random() % 300) - 80;
        e.value = static_cast<float> (random() % 400) / 100.0f - 2.0f;
        events.push_back (e);
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sampleOffset < b.sampleOffset; });
    const auto out = render (*sampler, events, 8192, 4096);
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        REQUIRE (std::isfinite (out.left[i]));
        REQUIRE (std::isfinite (out.right[i]));
    }
    CHECK (sampler->activeVoiceCount() <= Sampler::kMaxVoices);
    // allSoundOff silences everything within a few milliseconds.
    const auto silent = render (*sampler, { { Event::Type::allSoundOff, 0, 0, 0.0f } }, 4096, 4096);
    CHECK (sampler->activeVoiceCount() == 0);
    CHECK (levelAt (silent, 2048, 4096) < 1e-6f);
}

TEST_CASE ("stereo width and mono output", "[mix]")
{
    auto bank = std::make_shared<SampleBank>();
    plainSettings (*bank);
    std::vector<std::vector<float>> stereo { std::vector<float> (8000, 0.4f), std::vector<float> (8000, -0.2f) };
    bank->addSample (Sample::fromFloat (stereo, 48000));
    bank->addZone (zone (0, 0, 127, 60));
    auto sampler = makeSampler (finish (bank));
    Sampler::Parameters p;
    p.stereoWidth = 0.0f;
    sampler->setParameters (p);
    sampler->reset(); // width snaps to the new value
    const auto out = render (*sampler, { noteOn (0, 60, 1.0f) }, 4000);
    CHECK_THAT (out.left[3000], WithinAbs (0.1, 1e-4));
    CHECK_THAT (out.right[3000], WithinAbs (0.1, 1e-4));

    // right == left: the adapter asked for mono.
    sampler->setParameters ({});
    sampler->reset();
    std::vector<float> mono (2000, 0.0f);
    const Event on = noteOn (0, 60, 1.0f);
    sampler->process ({ mono.data(), mono.data(), 2000 }, &on, 1);
    CHECK_THAT (mono[1500], WithinAbs (0.1, 1e-4));
}
