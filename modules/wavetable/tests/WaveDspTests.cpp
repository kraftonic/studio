// SPDX-License-Identifier: MIT
#include "WaveTestUtils.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <limits>
#include <random>

using namespace wavetest;

namespace
{
constexpr int kRate = 48000;

struct Fixture
{
    WaveEngine engine;
    WaveParameters params;

    explicit Fixture (WaveParameters p = testParams(), double rate = kRate)
        : params (p)
    {
        engine.setParameters (params); // so prepare() starts settled on these values
        engine.prepare ({ rate, 512 });
        engine.setParameters (params);
    }

    Stereo play (int samples, std::vector<Event> events = {}, int block = 256)
    {
        return render (engine, samples, block, std::move (events));
    }
};

/** Steady tone of one note (after the attack) for spectral checks. */
std::vector<float> steady (WaveParameters p, int note, int samples = 1 << 14, double rate = kRate)
{
    Fixture f (p, rate);
    f.play (static_cast<int> (rate * 0.1), { noteOn (0, note) });
    return f.play (samples).left;
}

double toneLevel (const std::vector<float>& x, double hz, double rate = kRate)
{
    // Goertzel-style single-bin magnitude with a Hann window.
    double re = 0.0, im = 0.0;
    const double n = static_cast<double> (x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kraftonic::core::kTwoPi * static_cast<double> (i) / n);
        const double ph = kraftonic::core::kTwoPi * hz * static_cast<double> (i) / rate;
        re += x[i] * w * std::cos (ph);
        im -= x[i] * w * std::sin (ph);
    }
    return 4.0 * std::sqrt (re * re + im * im) / n;
}

double maxJump (const std::vector<float>& x, int from = 1, int to = -1)
{
    double worst = 0.0;
    const int end = to < 0 ? static_cast<int> (x.size()) : to;
    for (int i = std::max (from, 1); i < end; ++i)
        worst = std::max (worst, static_cast<double> (std::abs (x[static_cast<std::size_t> (i)] - x[static_cast<std::size_t> (i - 1)])));
    return worst;
}
} // namespace

TEST_CASE ("Defaults produce sound and silence returns to exact zero", "[wave][engine]")
{
    WaveEngine engine;
    engine.prepare ({ kRate, 256 });
    engine.setParameters (WaveParameters::defaults());
    const auto out = render (engine, kRate, 256, { noteOn (0, 60, 0.8f), noteOff (kRate / 2, 60) });
    CHECK (rms (slice (out.left, 2400, 12000)) > 0.02);
    CHECK (peak (out.left) < 0.9f);
    const auto tail = render (engine, kRate / 2);
    CHECK (engine.activeVoiceCount() == 0);
    CHECK (peak (slice (tail.left, kRate / 4, kRate / 4)) == 0.0f);
}

TEST_CASE ("Pitch is accurate in cents across the keyboard and tuning controls", "[wave][engine][pitch]")
{
    auto p = testParams();
    for (int note : { 33, 45, 57, 69, 81, 93, 105 })
    {
        const auto x = steady (p, note, kRate);
        const double expected = kraftonic::core::midiNoteToHz (note);
        INFO ("note " << note);
        CHECK (std::abs (cents (zeroCrossingHz (x, kRate), expected)) < 0.5);
    }
    p.set (ParamId::oscAOctave, -1.0f);
    p.set (ParamId::oscASemitone, 7.0f);
    p.set (ParamId::oscAFine, 25.0f);
    const auto x = steady (p, 69, kRate);
    CHECK (std::abs (cents (zeroCrossingHz (x, kRate), kraftonic::core::midiNoteToHz (69.0 - 12.0 + 7.0 + 0.25))) < 0.5);
}

TEST_CASE ("Osc B, sub and noise mix in; sub sits one or two octaves down", "[wave][engine]")
{
    auto p = testParams();
    p.set (ParamId::oscALevel, 0.0f);
    p.set (ParamId::subLevel, 1.0f);
    const auto sub1 = steady (p, 69, kRate);
    CHECK (std::abs (cents (zeroCrossingHz (sub1, kRate), 220.0)) < 1.0);
    p.set (ParamId::subOctave, 1.0f);
    const auto sub2 = steady (p, 69, kRate);
    CHECK (std::abs (cents (zeroCrossingHz (sub2, kRate), 110.0)) < 1.0);

    p.set (ParamId::subLevel, 0.0f);
    p.set (ParamId::oscBLevel, 1.0f);
    p.set (ParamId::oscBSemitone, 12.0f);
    const auto b = steady (p, 57, kRate);
    CHECK (std::abs (cents (zeroCrossingHz (b, kRate), 440.0)) < 1.0);

    p.set (ParamId::oscBLevel, 0.0f);
    p.set (ParamId::noiseLevel, 1.0f);
    const auto noise = steady (p, 60, 1 << 14);
    CHECK (rms (noise) > 0.01);
    CHECK (inharmonicDb (noise, kraftonic::core::midiNoteToHz (60), kRate) > -3.0); // broadband
}

TEST_CASE ("Unison detune and stereo spread place copies as set", "[wave][engine][unison]")
{
    auto p = testParams();
    p.set (ParamId::oscAUnison, 2.0f);
    p.set (ParamId::oscADetune, 40.0f); // copies at -20 and +20 cents
    p.set (ParamId::oscASpread, 1.0f);  // hard left / hard right
    Fixture f (p);
    f.play (4800, { noteOn (0, 69) });
    const auto out = f.play (kRate);
    const double low = 440.0 * std::exp2 (-20.0 / 1200.0);
    const double high = 440.0 * std::exp2 (20.0 / 1200.0);
    CHECK (std::abs (cents (zeroCrossingHz (out.left, kRate), low)) < 1.0);
    CHECK (std::abs (cents (zeroCrossingHz (out.right, kRate), high)) < 1.0);
    CHECK (toneLevel (out.left, high) < 0.01 * toneLevel (out.left, low));

    // No spread: both channels identical.
    p.set (ParamId::oscASpread, 0.0f);
    Fixture mono (p);
    mono.play (4800, { noteOn (0, 69) });
    const auto centred = mono.play (4800);
    CHECK (centred.left == centred.right);

    // Level compensation: an 8-voice stack is about as loud as one copy.
    auto one = testParams();
    one.set (ParamId::oscATable, 2.0f);
    const double single = rms (steady (one, 57, kRate));
    one.set (ParamId::oscAUnison, 8.0f);
    one.set (ParamId::oscADetune, 50.0f);
    one.set (ParamId::oscASpread, 0.0f);
    const double stack = rms (steady (one, 57, kRate));
    CHECK (std::abs (db (stack / single)) < 3.0);
}

TEST_CASE ("Every table sounds and stays band-limited through the engine at 44.1 kHz", "[wave][engine][alias]")
{
    constexpr double rate = 44100.0;
    for (int table = 0; table < 8; ++table)
    {
        auto p = testParams();
        p.set (ParamId::oscATable, static_cast<float> (table));
        p.set (ParamId::oscAPosition, 0.6f);
        for (int note : { 96, 108, 120 })
        {
            const auto x = steady (p, note, 1 << 15, rate);
            INFO ("table " << table << " note " << note);
            CHECK (rms (x) > 0.005);
            CHECK (inharmonicDb (x, kraftonic::core::midiNoteToHz (note), rate) < -60.0);
        }
    }
}

TEST_CASE ("Filter modes have the expected responses", "[wave][engine][filter]")
{
    // Sine (Basic, position 0) through a 1 kHz filter, no key tracking.
    auto measure = [] (int mode, double hz)
    {
        auto p = testParams();
        p.set (ParamId::filterMode, static_cast<float> (mode));
        p.set (ParamId::filterCutoff, 1000.0f);
        p.set (ParamId::filterResonance, 0.0f);
        const double note = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const int whole = static_cast<int> (std::floor (note));
        p.set (ParamId::oscAFine, static_cast<float> ((note - whole) * 100.0));
        const auto x = steady (p, whole, 1 << 14);
        return db (toneLevel (x, hz) / 0.2); // 0.2 = voice gain, unfiltered sine
    };
    const double lp12Low = measure (0, 125.0), lp12High = measure (0, 8000.0);
    const double lp24Low = measure (1, 125.0), lp24High = measure (1, 8000.0);
    const double bpLow = measure (2, 125.0), bpCentre = measure (2, 1000.0), bpHigh = measure (2, 8000.0);
    const double hpLow = measure (3, 125.0), hpHigh = measure (3, 8000.0);
    std::printf ("  filter dB @125/1k/8k: LP12 %.1f/%.1f  LP24 %.1f/%.1f  BP %.1f/%.1f/%.1f  HP %.1f/%.1f\n",
                 lp12Low, lp12High, lp24Low, lp24High, bpLow, bpCentre, bpHigh, hpLow, hpHigh);
    CHECK (std::abs (lp12Low) < 1.0);
    CHECK (lp12High < -30.0); // 12 dB/oct, 3 octaves
    CHECK (lp12High > -42.0);
    CHECK (std::abs (lp24Low) < 1.0);
    CHECK (lp24High < -60.0); // 24 dB/oct
    CHECK (std::abs (bpCentre) < 1.0);
    CHECK (bpLow < -12.0);
    CHECK (bpHigh < -12.0);
    CHECK (hpLow < -30.0);
    CHECK (std::abs (hpHigh) < 1.0);
}

TEST_CASE ("Resonance peaks at the cutoff and stays stable at maximum", "[wave][engine][filter]")
{
    auto p = testParams();
    p.set (ParamId::filterCutoff, 1000.0f);
    p.set (ParamId::filterResonance, 1.0f);
    p.set (ParamId::oscAFine, 0.0f);
    // 1 kHz is note 83.21; play via fine tuning.
    const double note = 69.0 + 12.0 * std::log2 (1000.0 / 440.0);
    p.set (ParamId::oscAFine, static_cast<float> ((note - 83.0) * 100.0));
    const auto x = steady (p, 83, 1 << 14);
    CHECK (db (toneLevel (x, 1000.0) / 0.2) > 15.0);
    // Saw with full resonance and a cutoff sweep: finite and bounded.
    p.set (ParamId::oscATable, 0.0f);
    p.set (ParamId::oscAPosition, 0.67f);
    Fixture f (p);
    f.play (100, { noteOn (0, 36) });
    for (int i = 0; i < 40; ++i)
    {
        p.set (ParamId::filterCutoff, 20.0f * std::pow (1000.0f, static_cast<float> (i) / 39.0f));
        f.engine.setParameters (p);
        const auto out = f.play (1200);
        for (float s : out.left)
            REQUIRE (std::isfinite (s));
        REQUIRE (peak (out.left) < 1.0f);
    }
}

TEST_CASE ("Key tracking, filter envelope and velocity move the cutoff", "[wave][engine][filter]")
{
    auto bright = [] (WaveParameters p, int note, float velocity)
    {
        p.set (ParamId::oscAPosition, 0.67f); // saw
        Fixture f (p);
        f.play (2400, { noteOn (0, note, velocity) });
        const auto x = f.play (1 << 14).left;
        return toneLevel (x, kraftonic::core::midiNoteToHz (note) * 8.0) / toneLevel (x, kraftonic::core::midiNoteToHz (note));
    };
    auto p = testParams();
    p.set (ParamId::filterCutoff, 500.0f);
    p.set (ParamId::filterKeyTrack, 1.0f);
    // With full key tracking, the 8th harmonic's relative level is the same an octave up.
    const double low = bright (p, 48, 1.0f), high = bright (p, 60, 1.0f);
    CHECK (std::abs (db (high / low)) < 1.5);
    p.set (ParamId::filterKeyTrack, 0.0f);
    CHECK (db (bright (p, 60, 1.0f) / bright (p, 48, 1.0f)) < -6.0);

    // LP24 at 500 Hz: velocity 0.1 adds 0.3 octave, velocity 1 adds 3 (8th harmonic ~19 dB apart).
    p.set (ParamId::filterMode, 1.0f);
    p.set (ParamId::velocityToCutoff, 3.0f);
    CHECK (db (bright (p, 48, 1.0f) / bright (p, 48, 0.1f)) > 15.0);

    // Mod envelope opens the filter during the attack/decay, not at sustain.
    auto e = testParams();
    e.set (ParamId::oscAPosition, 0.67f);
    e.set (ParamId::filterCutoff, 300.0f);
    e.set (ParamId::filterKeyTrack, 0.0f);
    e.set (ParamId::filterEnvAmount, 5.0f);
    e.set (ParamId::modDecay, 3.0f); // exponential: ~1/9 of this is one time constant
    e.set (ParamId::modSustain, 0.0f);
    Fixture f (e);
    const auto out = f.play (kRate, { noteOn (0, 48) }).left;
    const double early = toneLevel (slice (out, 480, 2048), 130.81 * 8.0);
    const double late = toneLevel (slice (out, kRate - 2048, 2048), 130.81 * 8.0);
    CHECK (db (early / late) > 15.0);
}

TEST_CASE ("Drive saturates without folding much back below 5 kHz", "[wave][engine][drive]")
{
    constexpr double rate = 44100.0;
    auto p = testParams();
    p.set (ParamId::filterDrive, 24.0f);
    const auto driven = steady (p, 96, 1 << 15, rate); // ~2.1 kHz sine, heavily driven
    const double hz = kraftonic::core::midiNoteToHz (96);
    CHECK (toneLevel (driven, 3.0 * hz, rate) > 0.05 * toneLevel (driven, hz, rate)); // it distorts
    const auto power = powerSpectrum (driven);
    const double binHz = rate / static_cast<double> (driven.size());
    double total = 0.0, lowAlias = 0.0;
    for (std::size_t b = 1; b < power.size(); ++b)
    {
        const double f = static_cast<double> (b) * binHz;
        total += power[b];
        const double h = std::round (f / hz);
        if (f < 5000.0 && (h < 1.0 || std::abs (f - h * hz) > 6.0 * binHz))
            lowAlias += power[b];
    }
    const double aliasDb = 10.0 * std::log10 (lowAlias / total);
    std::printf ("  engine drive +24 dB on a 2.1 kHz tone: alias below 5 kHz %.1f dB re total\n", aliasDb);
    CHECK (aliasDb < -45.0);
    // Zero drive is clean.
    p.set (ParamId::filterDrive, 0.0f);
    const auto clean = steady (p, 96, 1 << 15, rate);
    CHECK (toneLevel (clean, 3.0 * hz, rate) < 1.0e-4 * toneLevel (clean, hz, rate));
}

TEST_CASE ("Amp envelope follows attack, sustain and release", "[wave][engine][envelope]")
{
    auto p = testParams();
    p.set (ParamId::ampAttack, 0.1f);
    p.set (ParamId::ampDecay, 0.1f);
    p.set (ParamId::ampSustain, 0.5f);
    p.set (ParamId::ampRelease, 0.2f);
    Fixture f (p);
    const auto x = f.play (kRate, { noteOn (0, 69), noteOff (kRate / 2, 69) }).left;
    auto env = [&] (double t) { return peak (slice (x, static_cast<int> (t * kRate), 480)); };
    CHECK (env (0.0) < 0.05f);
    CHECK (env (0.05) < env (0.095));
    CHECK (env (0.095) > 0.18f);
    CHECK (std::abs (env (0.4) - 0.1f) < 0.01f); // sustain 0.5 * voice gain 0.2
    CHECK (env (0.6) < 0.06f);
    CHECK (env (0.75) < 0.01f);
    const auto tail = f.play (kRate / 2);
    CHECK (f.engine.activeVoiceCount() == 0);
}

TEST_CASE ("Mod envelope sweeps the wave position", "[wave][engine][envelope]")
{
    auto p = testParams();
    p.set (ParamId::modEnvToPosition, 1.0f);
    p.set (ParamId::modDecay, 0.5f);
    p.set (ParamId::modSustain, 0.0f);
    Fixture f (p);
    const auto x = f.play (kRate, { noteOn (0, 57) }).left;
    // Basic table: position 1 (square) early, back to a sine later.
    const double third = 3.0 * 220.0;
    const double early = toneLevel (slice (x, 240, 1024), third) / toneLevel (slice (x, 240, 1024), 220.0);
    const double late = toneLevel (slice (x, kRate - 4096, 4096), third) / toneLevel (slice (x, kRate - 4096, 4096), 220.0);
    CHECK (early > 0.2);
    CHECK (late < 0.001);
}

TEST_CASE ("LFOs reach every destination", "[wave][engine][lfo]")
{
    auto modulated = [] (int lfo, int destination)
    {
        auto p = testParams();
        p.set (lfo == 1 ? ParamId::lfo1Depth : ParamId::lfo2Depth, 0.5f);
        p.set (lfo == 1 ? ParamId::lfo1Destination : ParamId::lfo2Destination, static_cast<float> (destination));
        p.set (lfo == 1 ? ParamId::lfo1Rate : ParamId::lfo2Rate, 4.0f);
        p.set (ParamId::oscAPosition, 0.67f);
        p.set (ParamId::filterCutoff, 1500.0f);
        Fixture f (p);
        return f.play (kRate, { noteOn (0, 57) });
    };
    auto windows = [] (const std::vector<float>& x, auto metric)
    {
        double lo = 1.0e9, hi = 0.0;
        for (int at = 2400; at + 2400 <= static_cast<int> (x.size()); at += 1200)
        {
            const double m = metric (slice (x, at, 2400));
            lo = std::min (lo, m);
            hi = std::max (hi, m);
        }
        return hi / std::max (lo, 1.0e-12);
    };
    for (int lfo : { 1, 2 })
    {
        INFO ("LFO " << lfo);
        const auto off = modulated (lfo, 0);
        CHECK (windows (off.left, rms) < 1.02);
        // pitch: zero-crossing rate swings
        const auto pitch = modulated (lfo, 1);
        CHECK (windows (pitch.left, [] (const std::vector<float>& w) { return zeroCrossingHz (w, kRate); }) > 1.5);
        // cutoff: brightness swings
        const auto cutoff = modulated (lfo, 2);
        CHECK (windows (cutoff.left, [] (const std::vector<float>& w) { return toneLevel (w, 220.0 * 9.0) + 1.0e-9; }) > 4.0);
        // position: the Basic table morphs (3rd harmonic moves)
        const auto position = modulated (lfo, 3);
        CHECK (windows (position.left, [] (const std::vector<float>& w) { return toneLevel (w, 440.0) + 1.0e-9; }) > 2.0);
        // level: tremolo
        const auto level = modulated (lfo, 4);
        CHECK (windows (level.left, rms) > 1.5);
        // pan: left/right balance swings
        const auto pan = modulated (lfo, 5);
        CHECK (windows (pan.left, rms) > 1.5);
        CHECK (windows (pan.right, rms) > 1.5);
    }
}

TEST_CASE ("LFO 1 restarts with each note; LFO 2 runs free", "[wave][engine][lfo]")
{
    auto p = testParams();
    p.set (ParamId::lfo1Depth, 1.0f);
    p.set (ParamId::lfo1Destination, 4.0f); // level
    p.set (ParamId::lfo1Rate, 3.0f);
    Fixture a (p);
    const auto first = a.play (kRate, { noteOn (0, 60), noteOff (12000, 60), noteOn (24000, 60) }).left;
    // The second note's tremolo starts in the same phase as the first's.
    double diff = 0.0;
    for (int i = 0; i < 4800; ++i)
        diff = std::max (diff, static_cast<double> (std::abs (std::abs (first[static_cast<std::size_t> (24000 + i)])
                                                             - std::abs (first[static_cast<std::size_t> (i)]))));
    CHECK (diff < 0.02);

    p.set (ParamId::lfo1Depth, 0.0f);
    p.set (ParamId::lfo2Depth, 1.0f);
    p.set (ParamId::lfo2Destination, 4.0f);
    p.set (ParamId::lfo2Rate, 3.0f);
    Fixture b (p);
    const auto second = b.play (kRate, { noteOn (0, 60), noteOff (12000, 60), noteOn (24000, 60) }).left;
    CHECK (rms (slice (second, 2400, 2400)) > 1.3 * rms (slice (second, 26400, 2400))); // different phase now
}

TEST_CASE ("Mod wheel scales LFO 1 depth by modWheelToLfo1", "[wave][engine][lfo]")
{
    auto swing = [] (float amount, float wheel)
    {
        auto p = testParams();
        p.set (ParamId::lfo1Depth, 0.5f);
        p.set (ParamId::lfo1Destination, 4.0f);
        p.set (ParamId::lfo1Rate, 5.0f);
        p.set (ParamId::modWheelToLfo1, amount);
        Fixture f (p);
        const auto x = f.play (kRate, { control (Event::Type::modWheel, 0, wheel), noteOn (0, 60) }).left;
        double lo = 1.0, hi = 0.0;
        for (int at = 4800; at + 960 <= kRate; at += 480)
        {
            const double r = rms (slice (x, at, 960));
            lo = std::min (lo, r);
            hi = std::max (hi, r);
        }
        return hi / lo;
    };
    CHECK (swing (0.0f, 0.0f) > 1.5);               // wheel unused: LFO at full depth
    CHECK (std::abs (swing (0.0f, 1.0f) - swing (0.0f, 0.0f)) < 0.05);
    CHECK (swing (1.0f, 0.0f) < 1.06);              // wheel fully controls: down = no LFO
    CHECK (std::abs (swing (1.0f, 1.0f) - swing (0.0f, 0.0f)) < 0.05);
    const double half = swing (1.0f, 0.5f);
    CHECK (half > 1.1);
    CHECK (half < swing (1.0f, 1.0f));
}

TEST_CASE ("Pitch bend follows pitchBendRange", "[wave][engine][pitch]")
{
    for (float range : { 2.0f, 12.0f })
    {
        auto p = testParams();
        p.set (ParamId::pitchBendRange, range);
        Fixture f (p);
        f.play (4800, { control (Event::Type::pitchBend, 0, 1.0f), noteOn (0, 57) });
        const auto up = f.play (kRate).left;
        CHECK (std::abs (cents (zeroCrossingHz (up, kRate), 220.0 * std::exp2 (range / 12.0))) < 1.0);
        f.play (4800, { control (Event::Type::pitchBend, 0, -0.5f) });
        const auto down = f.play (kRate).left;
        CHECK (std::abs (cents (zeroCrossingHz (down, kRate), 220.0 * std::exp2 (-0.5 * range / 12.0))) < 1.0);
    }
}

TEST_CASE ("Glide, mono and legato voice modes", "[wave][engine][voices]")
{
    auto p = testParams();
    p.set (ParamId::voiceMode, 1.0f); // mono
    p.set (ParamId::glideTime, 0.2f);
    Fixture f (p);
    const auto x = f.play (kRate, { noteOn (0, 57), noteOn (kRate / 2, 69) }).left;
    CHECK (f.engine.activeVoiceCount() == 1);
    // Halfway through the glide the pitch is halfway (in semitones).
    const double mid = zeroCrossingHz (slice (x, kRate / 2 + 4320, 1440), kRate);
    CHECK (std::abs (cents (mid, kraftonic::core::midiNoteToHz (63.0))) < 60.0);
    const double end = zeroCrossingHz (slice (x, kRate / 2 + 12000, 9600), kRate);
    CHECK (std::abs (cents (end, 440.0)) < 1.0);

    // Mono returns to the still-held note on release.
    const auto back = f.play (kRate, { noteOff (0, 69) }).left;
    CHECK (std::abs (cents (zeroCrossingHz (slice (back, 24000, 24000), kRate), 220.0)) < 1.0);

    // Legato: overlapping notes don't retrigger the (decayed) envelope; mono does.
    auto envelopeJump = [] (float mode)
    {
        auto q = testParams();
        q.set (ParamId::voiceMode, mode);
        q.set (ParamId::ampDecay, 0.05f);
        q.set (ParamId::ampSustain, 0.3f);
        Fixture g (q);
        const auto y = g.play (kRate, { noteOn (0, 57), noteOn (kRate / 2, 60) }).left;
        return peak (slice (y, kRate / 2 + 96, 384)) / peak (slice (y, kRate / 2 - 1920, 960));
    };
    CHECK (envelopeJump (2.0f) < 1.1f);
    CHECK (envelopeJump (1.0f) > 2.0f);

    // Legato only glides when notes overlap.
    auto q = testParams();
    q.set (ParamId::voiceMode, 2.0f);
    q.set (ParamId::glideTime, 0.3f);
    Fixture g (q);
    const auto y = g.play (kRate, { noteOn (0, 57), noteOff (12000, 57), noteOn (24000, 69) }).left;
    CHECK (std::abs (cents (zeroCrossingHz (slice (y, 24480, 2400), kRate), 440.0)) < 1.0);

    // Poly glide starts from the previous note.
    auto poly = testParams();
    poly.set (ParamId::glideTime, 0.2f);
    Fixture h (poly);
    const auto z = h.play (kRate, { noteOn (0, 57), noteOn (kRate / 2, 69) }).left;
    CHECK (h.engine.activeVoiceCount() == 2);
    (void) z;
}

TEST_CASE ("Voice stealing and retriggers are click-free", "[wave][engine][voices]")
{
    auto p = testParams();
    p.set (ParamId::ampRelease, 2.0f);
    Fixture f (p);
    std::vector<Event> events;
    for (int i = 0; i < kMaxVoices; ++i)
        events.push_back (noteOn (i * 100, 36 + i, 0.6f));
    const auto warm = f.play (4800, events);
    CHECK (f.engine.activeVoiceCount() == kMaxVoices);
    const double normal = maxJump (warm.left, 3000);
    // Steal held voices with new notes, and retrigger a sounding one.
    const auto stolen = f.play (9600, { noteOn (1000, 52, 0.6f), noteOn (3000, 53, 0.6f), noteOn (5000, 40, 0.6f) });
    CHECK (f.engine.activeVoiceCount() == kMaxVoices);
    std::printf ("  steal: max step %.4f vs %.4f before\n", maxJump (stolen.left), normal);
    CHECK (maxJump (stolen.left) < 1.6 * normal + 0.01);
    CHECK (maxJump (stolen.right) < 1.6 * normal + 0.01);
    for (float s : stolen.left)
        REQUIRE (std::isfinite (s));
}

TEST_CASE ("Sustain pedal, all notes off and all sound off", "[wave][engine][voices]")
{
    for (float mode : { 0.0f, 1.0f })
    {
        auto p = testParams();
        p.set (ParamId::voiceMode, mode);
        Fixture f (p);
        const auto held = f.play (kRate / 2, { control (Event::Type::sustainPedal, 0, 1.0f), noteOn (0, 60),
                                               noteOff (2400, 60) });
        CHECK (rms (slice (held.left, 20000, 4000)) > 0.05);
        f.play (4800, { control (Event::Type::sustainPedal, 0, 0.0f) });
        CHECK (f.engine.activeVoiceCount() == 0);

        f.play (4800, { noteOn (0, 60), noteOn (0, 64) });
        f.play (4800, { { Event::Type::allNotesOff, 0, 0, 0.0f } });
        CHECK (f.engine.activeVoiceCount() == 0);

        f.play (480, { noteOn (0, 60) });
        const auto cut = f.play (480, { { Event::Type::allSoundOff, 10, 0, 0.0f } });
        CHECK (f.engine.activeVoiceCount() == 0);
        CHECK (peak (slice (cut.left, 10, 470)) == 0.0f);
    }
}

TEST_CASE ("Events are sample-accurate", "[wave][engine]")
{
    Fixture f;
    const auto x = f.play (1024, { noteOn (300, 69) }, 1024).left;
    CHECK (peak (slice (x, 0, 300)) == 0.0f);
    CHECK (peak (slice (x, 300, 100)) > 0.0f);
}

TEST_CASE ("Output does not depend on block size", "[wave][engine]")
{
    auto p = WaveParameters::defaults();
    p.set (ParamId::oscAUnison, 4.0f);
    p.set (ParamId::oscBLevel, 0.5f);
    p.set (ParamId::oscBTable, 3.0f);
    p.set (ParamId::noiseLevel, 0.1f);
    p.set (ParamId::subLevel, 0.3f);
    p.set (ParamId::filterDrive, 9.0f);
    p.set (ParamId::lfo1Depth, 0.4f);
    p.set (ParamId::lfo2Depth, 0.3f);
    p.set (ParamId::lfo1Shape, 4.0f);
    p.set (ParamId::glideTime, 0.05f);
    const std::vector<Event> events { noteOn (0, 60), noteOn (777, 64), control (Event::Type::modWheel, 1000, 0.7f),
                                      control (Event::Type::pitchBend, 1500, 0.3f), noteOff (3000, 60),
                                      noteOn (3001, 67), control (Event::Type::sustainPedal, 4000, 1.0f),
                                      noteOff (4500, 64), control (Event::Type::sustainPedal, 6000, 0.0f) };
    Stereo reference;
    for (int block : { 512, 1, 7, 64, 333 })
    {
        WaveEngine engine;
        engine.prepare ({ kRate, 512 });
        engine.setParameters (p);
        const auto out = render (engine, 9000, block, events);
        if (block == 512)
            reference = out;
        else
        {
            INFO ("block " << block);
            CHECK (out.left == reference.left);
            CHECK (out.right == reference.right);
        }
    }
}

TEST_CASE ("Extreme settings stay finite and under full scale", "[wave][engine]")
{
    std::mt19937 rng (1234);
    for (int run = 0; run < 24; ++run)
    {
        WaveParameters p;
        for (const auto& info : kParamInfo)
        {
            const int pick = static_cast<int> (rng() % 3);
            p.set (info.id, pick == 0 ? info.minValue : (pick == 1 ? info.maxValue : info.defaultValue));
        }
        p.set (ParamId::outputGain, 6.0f);
        for (double rate : { 22050.0, 192000.0 })
        {
            WaveEngine engine;
            engine.prepare ({ rate, 256 });
            engine.setParameters (p);
            std::vector<Event> events;
            for (int i = 0; i < 20; ++i)
                events.push_back (noteOn (i * 50, (i * 37) % 128, 1.0f));
            events.push_back (control (Event::Type::pitchBend, 1200, 1.0f));
            events.push_back (control (Event::Type::modWheel, 1300, 1.0f));
            const auto out = render (engine, 6000, 256, events);
            for (std::size_t i = 0; i < out.left.size(); ++i)
            {
                REQUIRE (std::isfinite (out.left[i]));
                REQUIRE (std::isfinite (out.right[i]));
                REQUIRE (std::abs (out.left[i]) < 1.0f);
                REQUIRE (std::abs (out.right[i]) < 1.0f);
            }
        }
    }
    // Non-finite parameter values fall back to defaults.
    auto bad = WaveParameters::defaults();
    bad.values[static_cast<std::size_t> (ParamId::filterCutoff)] = std::numeric_limits<float>::quiet_NaN();
    bad.values[static_cast<std::size_t> (ParamId::oscAPosition)] = std::numeric_limits<float>::infinity();
    WaveEngine engine;
    engine.prepare ({ kRate, 256 });
    engine.setParameters (bad);
    const auto out = render (engine, 4800, 256, { noteOn (0, 60), noteOn (0, 200), { Event::Type::noteOn, 10, 61,
                                                                                    std::numeric_limits<float>::quiet_NaN() } });
    for (float s : out.left)
        REQUIRE (std::isfinite (s));
}

TEST_CASE ("Parameter changes are smoothed", "[wave][engine]")
{
    auto p = testParams();
    p.set (ParamId::oscAPosition, 0.67f);
    Fixture f (p);
    f.play (4800, { noteOn (0, 45) });
    const double normal = maxJump (f.play (4800).left);
    // Jump several parameters at once, including table and filter mode.
    p.set (ParamId::oscATable, 6.0f);
    p.set (ParamId::filterMode, 3.0f);
    p.set (ParamId::filterCutoff, 200.0f);
    p.set (ParamId::oscALevel, 0.2f);
    p.set (ParamId::oscAUnison, 5.0f);
    p.set (ParamId::outputGain, -12.0f);
    f.engine.setParameters (p);
    const double changed = maxJump (f.play (4800).left);
    CHECK (changed < 1.5 * normal);
}

TEST_CASE ("Fast table automation never jumps", "[wave][engine]")
{
    auto p = testParams();
    p.set (ParamId::oscAPosition, 0.5f);
    Fixture f (p);
    f.play (4800, { noteOn (0, 45) });
    double normal = 0.0;
    for (int table = 0; table < 8; ++table)
    {
        p.set (ParamId::oscATable, static_cast<float> (table));
        f.engine.setParameters (p);
        normal = std::max (normal, maxJump (f.play (4800).left));
    }
    // Now switch table every 64-sample block (faster than the 10 ms crossfade).
    double automated = 0.0;
    for (int block = 0; block < 200; ++block)
    {
        p.set (ParamId::oscATable, static_cast<float> ((block * 3) % 8));
        f.engine.setParameters (p);
        automated = std::max (automated, maxJump (f.play (64, {}, 64).left));
    }
    CHECK (automated < 1.3 * normal);
}

TEST_CASE ("Re-attacking a sounding note at a new velocity is click-free", "[wave][engine][voices]")
{
    for (float mode : { 0.0f, 1.0f })
    {
        auto p = testParams();
        p.set (ParamId::voiceMode, mode);
        p.set (ParamId::velocityToLevel, 1.0f);
        p.set (ParamId::lfo1Depth, 1.0f);
        p.set (ParamId::lfo1Destination, 5.0f); // pan: the key-synced restart moves it
        p.set (ParamId::lfo1Shape, 3.0f);       // square: worst case
        p.set (ParamId::lfo1Rate, 3.0f);
        Fixture f (p);
        const auto steadyPart = f.play (12000, { noteOn (0, 45, 1.0f) });
        const double normal = std::max (maxJump (steadyPart.left, 2400), maxJump (steadyPart.right, 2400));
        const auto again = f.play (4800, { noteOn (100, 45, 0.1f) }); // same note, LFO mid-cycle
        INFO ("mode " << mode);
        CHECK (maxJump (again.left) < 1.2 * normal);
        CHECK (maxJump (again.right) < 1.2 * normal);
    }
}
