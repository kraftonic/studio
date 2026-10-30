// SPDX-License-Identifier: MIT
// Measured DSP tests: level, pitch, filter response, envelope timing,
// smoothing, glide, pedal, sample rates and block-size independence.
#include "StarterSynthEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace startersynth;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

struct Timed
{
    double seconds;
    Event event;
};

Timed on (double s, int note, float velocity = 1.0F) { return {s, {Event::Type::noteOn, 0, note, velocity}}; }
Timed off (double s, int note) { return {s, {Event::Type::noteOff, 0, note, 0.0F}}; }

/** Renders `seconds` of the left channel, placing each event on its exact sample. */
std::vector<float> render (const StarterSynthParameters& params, std::vector<Timed> events, double seconds,
                           int blockSize = 256, double rate = kRate)
{
    StarterSynthEngine engine;
    engine.prepare ({rate, blockSize});
    engine.setParameters (params);
    const int total = static_cast<int> (seconds * rate);
    std::vector<float> left (static_cast<std::size_t> (total)), right (left.size());
    std::vector<Event> blockEvents;
    std::size_t next = 0;
    for (int start = 0; start < total; start += blockSize)
    {
        const int n = std::min (blockSize, total - start);
        blockEvents.clear();
        for (; next < events.size() && static_cast<int> (events[next].seconds * rate) < start + n; ++next)
        {
            Event e = events[next].event;
            e.sampleOffset = static_cast<int> (events[next].seconds * rate) - start;
            blockEvents.push_back (e);
        }
        engine.setParameters (params);
        engine.process ({left.data() + start, right.data() + start, n}, blockEvents.data(),
                        static_cast<int> (blockEvents.size()));
    }
    return left;
}

float peak (const std::vector<float>& x, double from = 0.0, double to = 1e9, double rate = kRate)
{
    float result = 0.0F;
    const auto end = std::min (x.size(), static_cast<std::size_t> (to * rate));
    for (auto i = static_cast<std::size_t> (from * rate); i < end; ++i)
        result = std::max (result, std::abs (x[i]));
    return result;
}

/** Amplitude of the `hz` component (Goertzel), over a whole number of seconds' worth of samples. */
double magnitudeAt (const std::vector<float>& x, double hz, double from, double to)
{
    const auto a = static_cast<std::size_t> (from * kRate), b = static_cast<std::size_t> (to * kRate);
    const double w = 2.0 * kPi * hz / kRate;
    double re = 0.0, im = 0.0;
    for (auto i = a; i < b; ++i)
    {
        re += x[i] * std::cos (w * static_cast<double> (i));
        im += x[i] * std::sin (w * static_cast<double> (i));
    }
    return 2.0 * std::hypot (re, im) / static_cast<double> (b - a);
}

/** Frequency from rising zero crossings (with sub-sample interpolation). */
double frequency (const std::vector<float>& x, double from, double to)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (auto i = static_cast<std::size_t> (from * kRate) + 1; i < static_cast<std::size_t> (to * kRate); ++i)
        if (x[i - 1] < 0.0F && x[i] >= 0.0F)
        {
            const double t = static_cast<double> (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0)
                first = t;
            else
                ++crossings;
            last = t;
        }
    return crossings > 0 ? crossings * kRate / (last - first) : 0.0;
}

/** A clean sine voice: osc 1 sine only, filter wide open, no filter envelope. */
StarterSynthParameters sineVoice()
{
    auto p = StarterSynthParameters::defaults();
    p.set (ParamId::osc1Wave, static_cast<float> (Wave::sine));
    p.set (ParamId::oscMix, 0.0F);
    p.set (ParamId::cutoff, 20000.0F);
    p.set (ParamId::resonance, 0.0F);
    p.set (ParamId::envAmount, 0.0F);
    p.set (ParamId::outputGain, 0.0F);
    p.set (ParamId::sustain, 1.0F);
    return p;
}
} // namespace

TEST_CASE ("silent without notes, finite and under 0 dBFS with a full chord")
{
    const auto p = StarterSynthParameters::defaults();
    CHECK (peak (render (p, {}, 0.5)) == 0.0F);

    std::vector<Timed> chord;
    for (int note : {36, 48, 55, 60, 64, 67, 72, 76})
        chord.push_back (on (0.0, note));
    auto loud = p;
    loud.set (ParamId::outputGain, 0.0F); // worst case: output at maximum
    const auto out = render (loud, chord, 1.0);
    for (float s : out)
        REQUIRE (std::isfinite (s));
    CHECK (peak (out) < 1.0F);
    CHECK (peak (out) > 0.1F);
}

TEST_CASE ("one note at default settings plays at a sensible level")
{
    const auto out = render (StarterSynthParameters::defaults(), {on (0.0, 60)}, 0.5);
    const double db = 20.0 * std::log10 (peak (out, 0.1));
    CHECK (db > -24.0);
    CHECK (db < -6.0);
}

TEST_CASE ("note 69 plays 440 Hz and osc 2 detune shifts osc 2 by cents")
{
    CHECK (frequency (render (sineVoice(), {on (0.0, 69)}, 0.6), 0.1, 0.6) == Catch::Approx (440.0).epsilon (0.002));

    auto p = sineVoice();
    p.set (ParamId::osc2Wave, static_cast<float> (Wave::sine));
    p.set (ParamId::oscMix, 1.0F); // osc 2 only
    p.set (ParamId::osc2Detune, 50.0F);
    const double expected = 440.0 * std::exp2 (50.0 / 1200.0);
    CHECK (frequency (render (p, {on (0.0, 69)}, 0.6), 0.1, 0.6) == Catch::Approx (expected).epsilon (0.002));
}

TEST_CASE ("the low-pass cutoff removes harmonics above it")
{
    auto p = StarterSynthParameters::defaults();
    p.set (ParamId::oscMix, 0.0F); // one saw at 110 Hz
    p.set (ParamId::envAmount, 0.0F);
    p.set (ParamId::resonance, 0.0F);
    p.set (ParamId::sustain, 1.0F);
    p.set (ParamId::cutoff, 8000.0F);
    const auto open = render (p, {on (0.0, 45)}, 0.6);
    p.set (ParamId::cutoff, 400.0F);
    const auto closed = render (p, {on (0.0, 45)}, 0.6);

    // The fundamental passes both; harmonic 30 (3.3 kHz) is ~3 octaves above
    // 400 Hz, so a 12 dB/octave filter takes it down by well over 20 dB more.
    const double fundamentalDiff = 20.0 * std::log10 (magnitudeAt (closed, 110.0, 0.1, 0.6) / magnitudeAt (open, 110.0, 0.1, 0.6));
    const double harmonicDiff = 20.0 * std::log10 (magnitudeAt (closed, 3300.0, 0.1, 0.6) / magnitudeAt (open, 3300.0, 0.1, 0.6));
    CHECK (std::abs (fundamentalDiff) < 3.0);
    CHECK (harmonicDiff < -30.0);
}

TEST_CASE ("envelope amount opens the filter while the envelope is high")
{
    auto p = StarterSynthParameters::defaults();
    p.set (ParamId::oscMix, 0.0F);
    p.set (ParamId::cutoff, 300.0F);
    p.set (ParamId::resonance, 0.0F);
    p.set (ParamId::sustain, 1.0F);
    p.set (ParamId::envAmount, 0.0F);
    const auto flat = render (p, {on (0.0, 45)}, 0.4);
    p.set (ParamId::envAmount, 4.0F); // +4 octaves at full level: 4.8 kHz
    const auto opened = render (p, {on (0.0, 45)}, 0.4);
    CHECK (magnitudeAt (opened, 2200.0, 0.1, 0.4) > 10.0 * magnitudeAt (flat, 2200.0, 0.1, 0.4));
}

TEST_CASE ("attack and release times are measurable")
{
    auto p = sineVoice();
    p.set (ParamId::attack, 0.1F);
    p.set (ParamId::release, 0.1F);
    const auto out = render (p, {on (0.0, 69), off (0.5, 69)}, 1.2);

    // Attack: still rising at 20 ms, close to full level by 150 ms.
    const float full = peak (out, 0.3, 0.5);
    CHECK (peak (out, 0.015, 0.025) < 0.5F * full);
    CHECK (peak (out, 0.14, 0.16) > 0.9F * full);
    // Release: below -20 dB of full 0.25 s after note-off, silent (-80 dB) by 0.7 s.
    CHECK (peak (out, 0.75, 0.8) < 0.1F * full);
    CHECK (peak (out, 1.15, 1.2) < 1.0e-4F * full);
}

TEST_CASE ("voices free themselves after the release and the pedal holds notes")
{
    StarterSynthEngine engine;
    engine.prepare ({kRate, 256});
    auto p = sineVoice();
    p.set (ParamId::release, 0.05F);
    engine.setParameters (p);
    std::vector<float> l (256), r (256);
    auto run = [&] (std::vector<Event> events, int blocks = 1)
    {
        for (int b = 0; b < blocks; ++b, events.clear())
            engine.process ({l.data(), r.data(), 256}, events.data(), static_cast<int> (events.size()));
    };
    run ({{Event::Type::sustainPedal, 0, 0, 1.0F}, {Event::Type::noteOn, 0, 60, 1.0F}, {Event::Type::noteOff, 10, 60, 0.0F}});
    run ({}, 100); // half a second later the pedal still holds the note
    CHECK (engine.activeVoiceCount() == 1);
    run ({{Event::Type::sustainPedal, 0, 0, 0.0F}}, 100);
    CHECK (engine.activeVoiceCount() == 0);

    run ({{Event::Type::noteOn, 0, 60, 1.0F}, {Event::Type::noteOn, 0, 64, 1.0F}});
    CHECK (engine.activeVoiceCount() == 2);
    run ({{Event::Type::allSoundOff, 0, 0, 0.0F}});
    CHECK (engine.activeVoiceCount() == 0);
    CHECK (peak (l) == 0.0F);
}

TEST_CASE ("glide slides from the previous note")
{
    auto p = sineVoice();
    p.set (ParamId::glide, 0.1F);
    const auto out = render (p, {on (0.0, 57), off (0.3, 57), on (0.3, 69)}, 1.0);
    CHECK (frequency (out, 0.31, 0.36) < 400.0);                              // still on the way up
    CHECK (frequency (out, 0.8, 1.0) == Catch::Approx (440.0).epsilon (0.005)); // arrived
}

TEST_CASE ("a jump in the output level is smoothed instead of clicking")
{
    auto quiet = sineVoice();
    quiet.set (ParamId::outputGain, -40.0F);
    auto loud = sineVoice();
    loud.set (ParamId::outputGain, 0.0F);

    StarterSynthEngine engine;
    engine.prepare ({kRate, 256});
    std::vector<float> l (256 * 40), r (l.size());
    Event note {Event::Type::noteOn, 0, 69, 1.0F};
    for (int b = 0; b < 40; ++b)
    {
        engine.setParameters (b < 20 ? quiet : loud);
        engine.process ({l.data() + b * 256, r.data() + b * 256, 256}, &note, b == 0 ? 1 : 0);
    }
    // The steepest step a full-scale 440 Hz sine can take is 2 pi 440 / 48000.
    float maxStep = 0.0F;
    for (std::size_t i = 1; i < l.size(); ++i)
        maxStep = std::max (maxStep, std::abs (l[i] - l[i - 1]));
    CHECK (maxStep < 1.1F * static_cast<float> (2.0 * kPi * 440.0 / kRate) * peak (l));
}

TEST_CASE ("output does not depend on the host block size")
{
    auto p = StarterSynthParameters::defaults();
    p.set (ParamId::glide, 0.05F);
    const std::vector<Timed> events {on (0.0, 60), on (0.11, 64), off (0.2, 60), on (0.3, 67), off (0.5, 64)};
    const auto a = render (p, events, 0.8, 64);
    const auto b = render (p, events, 0.8, 333);
    CHECK (a == b);
}

TEST_CASE ("every supported sample rate plays finite at a sensible level")
{
    for (double rate : {22050.0, 44100.0, 96000.0, 384000.0})
    {
        const auto out = render (StarterSynthParameters::defaults(), {on (0.0, 60)}, 0.3, 512, rate);
        for (float s : out)
            REQUIRE (std::isfinite (s));
        const double db = 20.0 * std::log10 (peak (out, 0.1, 0.3, rate));
        CHECK (db > -24.0);
        CHECK (db < -6.0);
    }
}
