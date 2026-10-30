// SPDX-License-Identifier: MIT
#include "WaveTestUtils.h"

#include "../src/dsp/Filter.h"
#include "../src/dsp/WaveOscillator.h"
#include "../src/dsp/Wavetables.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>

using namespace kraftonic::wavetable::detail;
using namespace wavetest;

namespace
{
constexpr std::array<const char*, kNumTables> kTableNames { "Basic", "Harmonic", "Pulse sweep", "Formant",
                                                            "Metallic", "Digital", "Organ", "Noise" };

std::vector<float> renderOsc (int table, float position, double hz, double sampleRate, int samples,
                              int unison = 1, float detune = 0.0f)
{
    const auto& bank = WavetableBank::instance();
    UnisonLayout layout;
    std::array<float, kMaxUnison> weights {};
    for (int i = 0; i < unison; ++i)
        weights[static_cast<std::size_t> (i)] = 1.0f;
    layoutUnison (layout, unison, detune, 0.0f, weights);
    WaveOscillator osc;
    Random random;
    osc.start (random, unison > 1);
    const TableSelection selection { table, table, 1.0f };
    std::vector<float> out (static_cast<std::size_t> (samples));
    for (auto& s : out)
    {
        float l = 0.0f, r = 0.0f;
        osc.process (bank, layout, selection, position, hz / sampleRate, l, r);
        s = l;
    }
    return out;
}

/** Energy outside the exact harmonic bins (dB re total) for a bin-exact tone,
    counting only the lowest `maxBinFraction` of the spectrum. */
double aliasDb (const std::vector<float>& x, int fundamentalBin, double maxBinFraction = 1.0)
{
    const auto power = powerSpectrum (x, false);
    double total = 0.0, outside = 0.0;
    const auto limit = static_cast<std::size_t> (maxBinFraction * static_cast<double> (power.size()));
    for (std::size_t b = 1; b < power.size(); ++b)
    {
        total += power[b];
        if (b % static_cast<std::size_t> (fundamentalBin) != 0 && b < limit)
            outside += power[b];
    }
    return 10.0 * std::log10 (std::max (outside, 1.0e-30) / std::max (total, 1.0e-30));
}
} // namespace

TEST_CASE ("Every mip level of every frame is band-limited by construction", "[wave][tables]")
{
    const auto& bank = WavetableBank::instance();
    for (int table = 0; table < kNumTables; ++table)
        for (int level = 0; level < kLevels; ++level)
            for (int frame : { 0, 21, 42, kFrames - 1 })
            {
                const int size = levelSize (level);
                const float* data = bank.frame (table, level, frame);
                std::vector<float> cycle (data, data + size);
                const auto power = powerSpectrum (cycle, false);
                double inBand = 0.0, outOfBand = 0.0;
                for (std::size_t b = 1; b < power.size(); ++b)
                    (static_cast<int> (b) <= levelHarmonics (level) ? inBand : outOfBand) += power[b];
                INFO (kTableNames[static_cast<std::size_t> (table)] << " level " << level << " frame " << frame);
                CHECK (inBand > 0.0);
                CHECK (10.0 * std::log10 (outOfBand / inBand + 1.0e-30) < -100.0);
                // Guard samples wrap the cycle for the interpolator.
                CHECK (data[-1] == data[size - 1]);
                CHECK (data[size] == data[0]);
                CHECK (data[size + 1] == data[1]);
                CHECK (peak (cycle) <= 1.3f);
            }
}

TEST_CASE ("Mip level selection keeps every harmonic under 0.45 fs", "[wave][tables]")
{
    for (double increment = 1.0e-4; increment < 0.45; increment *= 1.07)
    {
        const int level = mipLevelFor (increment);
        CHECK (levelHarmonics (level) * increment <= kAliasLimit + 1.0e-12);
        if (level > 0)
            CHECK (levelHarmonics (level - 1) * increment > kAliasLimit); // never darker than needed
    }
    CHECK (nyquistFade (0.40) == 1.0f);
    CHECK (nyquistFade (0.5) == 0.0f);
}

TEST_CASE ("Wavetables do not alias on high notes at 44.1 kHz", "[wave][tables][alias]")
{
    constexpr double rate = 44100.0;
    constexpr int size = 1 << 15;
    for (int table = 0; table < kNumTables; ++table)
    {
        double worst = -300.0;
        for (int note : { 60, 72, 84, 91, 96, 100, 103, 108, 112, 115, 120, 124, 127 })
        {
            const int bin = static_cast<int> (std::lround (kraftonic::core::midiNoteToHz (note) * size / rate));
            const double hz = bin * rate / size; // bin-exact so harmonics land on bins
            for (float position : { 0.0f, 0.37f, 0.71f, 1.0f })
            {
                const auto x = renderOsc (table, position, hz, rate, size);
                const double a = aliasDb (x, bin);
                worst = std::max (worst, a);
                INFO (kTableNames[static_cast<std::size_t> (table)] << " note " << note << " position " << position);
                // Harmonics never fold (mip levels); what remains is interpolation
                // images. Tables with energy piled at the top of a level (Pulse
                // narrow, Metallic, Noise) sit higher but still far below audibility.
                const bool topHeavy = table == static_cast<int> (Table::pulse) || table == static_cast<int> (Table::metallic)
                                      || table == static_cast<int> (Table::noise);
                CHECK (a < (topHeavy ? -65.0 : -85.0));
            }
        }
        std::printf ("  alias %-12s worst %.1f dB re total\n", kTableNames[static_cast<std::size_t> (table)], worst);
    }
}

TEST_CASE ("Position morph is continuous for every table", "[wave][tables]")
{
    constexpr double rate = 48000.0;
    const auto& bank = WavetableBank::instance();
    for (int table = 0; table < kNumTables; ++table)
    {
        // Adjacent frames are close: the largest step between neighbours is
        // a small fraction of the full range.
        double worstStep = 0.0;
        for (int f = 0; f + 1 < kFrames; ++f)
        {
            const float* a = bank.frame (table, 3, f);
            const float* b = bank.frame (table, 3, f + 1);
            double diff = 0.0, energy = 0.0;
            for (int i = 0; i < levelSize (3); ++i)
            {
                diff += (a[i] - b[i]) * (a[i] - b[i]);
                energy += a[i] * a[i];
            }
            worstStep = std::max (worstStep, std::sqrt (diff / std::max (energy, 1.0e-12)));
        }
        INFO (kTableNames[static_cast<std::size_t> (table)]);
        CHECK (worstStep < (table == static_cast<int> (Table::digital) ? 1.0 : 0.6));

        // A held note swept through position never jumps: the second difference
        // stays within what the static frames themselves produce.
        const double hz = 110.0;
        double staticWorst = 0.0;
        for (int f = 0; f < kFrames; f += 3)
        {
            const auto x = renderOsc (table, static_cast<float> (f) / (kFrames - 1), hz, rate, 2048);
            for (std::size_t i = 2; i < x.size(); ++i)
                staticWorst = std::max (staticWorst, static_cast<double> (std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2])));
        }
        const auto& b2 = WavetableBank::instance();
        UnisonLayout layout;
        std::array<float, kMaxUnison> weights { 1.0f };
        layoutUnison (layout, 1, 0.0f, 0.0f, weights);
        WaveOscillator osc;
        Random random;
        osc.start (random, false);
        std::vector<float> sweep (static_cast<std::size_t> (rate));
        for (std::size_t i = 0; i < sweep.size(); ++i)
        {
            float l = 0.0f, r = 0.0f;
            osc.process (b2, layout, { table, table, 1.0f }, static_cast<float> (i) / static_cast<float> (sweep.size()),
                         hz / rate, l, r);
            sweep[i] = l;
        }
        double sweepWorst = 0.0;
        for (std::size_t i = 2; i < sweep.size(); ++i)
            sweepWorst = std::max (sweepWorst, static_cast<double> (std::abs (sweep[i] - 2.0f * sweep[i - 1] + sweep[i - 2])));
        CHECK (sweepWorst <= 1.25 * staticWorst + 1.0e-4);
    }
}

TEST_CASE ("Oscillator pitch is accurate to a fraction of a cent", "[wave][tables]")
{
    constexpr double rate = 48000.0;
    for (double hz : { 27.5, 110.0, 440.0, 1760.0, 4186.0 })
    {
        const auto x = renderOsc (0, 0.0f, hz, rate, 48000);
        INFO (hz);
        CHECK (std::abs (cents (zeroCrossingHz (x, rate), hz)) < 0.1);
    }
}

TEST_CASE ("Unison keeps loudness constant and stays band-limited", "[wave][tables]")
{
    constexpr double rate = 48000.0;
    const double one = rms (renderOsc (2, 0.0f, 220.0, rate, 48000, 1));
    for (int n : { 2, 3, 5, 8 })
    {
        const double many = rms (renderOsc (2, 0.0f, 220.0, rate, 48000, n, 40.0f));
        INFO (n);
        CHECK (std::abs (db (many / one)) < 2.5);
    }
    // Detuned copies push the top harmonic up; the stack still picks a safe level.
    const auto high = renderOsc (0, 1.0f, 3000.0, rate, 1 << 15, 8, 100.0f);
    const auto power = powerSpectrum (high);
    double above = 0.0, total = 0.0;
    const double binHz = rate / static_cast<double> (high.size());
    for (std::size_t b = 1; b < power.size(); ++b)
    {
        total += power[b];
        // the stack's highest copy is +50 cents; anything far from its partials is alias
        const double f = static_cast<double> (b) * binHz;
        const double h = std::round (f / 3000.0);
        if (h < 1.0 || std::abs (f / (h * 3000.0) - 1.0) > 0.035)
            above += power[b];
    }
    CHECK (10.0 * std::log10 (above / total) < -70.0);
}

TEST_CASE ("ADAA drive aliases far less than a plain tanh", "[wave][drive]")
{
    constexpr double rate = 44100.0;
    constexpr int size = 1 << 15;
    const int bin = static_cast<int> (std::lround (2500.0 * size / rate));
    const double hz = bin * rate / size;
    std::vector<float> plain (size), adaa (size);
    AdaaTanh shaper;
    shaper.track (static_cast<float> (8.0 * std::sin (-kraftonic::core::kTwoPi * hz / rate)));
    for (int i = 0; i < size; ++i)
    {
        const auto x = static_cast<float> (8.0 * std::sin (kraftonic::core::kTwoPi * hz * i / rate));
        plain[static_cast<std::size_t> (i)] = std::tanh (x);
        adaa[static_cast<std::size_t> (i)] = shaper.process (x);
    }
    // Aliases that fold down below 5 kHz are the audible ones.
    const double band = 5000.0 / (rate / 2.0);
    const double plainAlias = aliasDb (plain, bin, band);
    const double adaaAlias = aliasDb (adaa, bin, band);
    std::printf ("  drive alias below 5 kHz (2.5 kHz sine, +18 dB): plain tanh %.1f dB, ADAA %.1f dB\n", plainAlias, adaaAlias);
    CHECK (adaaAlias < plainAlias - 20.0);
}
