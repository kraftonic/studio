// SPDX-License-Identifier: MIT
#include "kraftonic/fx/Chorus.h"
#include "kraftonic/fx/Delay.h"
#include "kraftonic/fx/Drive.h"
#include "kraftonic/fx/EQ.h"
#include "kraftonic/fx/Phaser.h"
#include "kraftonic/fx/Reverb.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace kraftonic::fx;

TEST_CASE ("All six classes satisfy StereoEffect", "[fx]")
{
    STATIC_CHECK (StereoEffect<Drive>);
    STATIC_CHECK (StereoEffect<Phaser>);
    STATIC_CHECK (StereoEffect<Chorus>);
    STATIC_CHECK (StereoEffect<Delay>);
    STATIC_CHECK (StereoEffect<Reverb>);
    STATIC_CHECK (StereoEffect<EQ>);
}

namespace
{
template <typename Effect>
void basicChecks()
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Effect effect;
        effect.prepare ({ rate, 512 });
        typename Effect::Parameters params;
        params.enabled = true;
        effect.setParameters (params);
        std::array<float, 512> l {}, r {};
        for (int pass = 0; pass < 3; ++pass)
        {
            for (int i = 0; i < 512; ++i)
            {
                l[static_cast<std::size_t> (i)] = pass == 0 ? 0.0f : (pass == 1 && i == 0 ? 1.0f :
                    static_cast<float> ((i * 811 + pass * 97) % 2001 - 1000) / 1000.0f);
                r[static_cast<std::size_t> (i)] = -l[static_cast<std::size_t> (i)];
            }
            effect.process (l.data(), r.data(), 512);
            for (int i = 0; i < 512; ++i)
            {
                REQUIRE (std::isfinite (l[static_cast<std::size_t> (i)]));
                REQUIRE (std::isfinite (r[static_cast<std::size_t> (i)]));
            }
        }
        params.enabled = false;
        effect.setParameters (params);
        std::array<float, 512> original {};
        for (int block = 0; block < 4; ++block)
        {
            for (int i = 0; i < 512; ++i)
                original[static_cast<std::size_t> (i)] = static_cast<float> (i - 256) / 512.0f;
            l = original; r = original;
            effect.process (l.data(), r.data(), 512);
        }
        REQUIRE (l == original);
        REQUIRE (r == original);
    }
}

template <typename Effect>
void blockChecks()
{
    std::array<float, 4096> original {}, a {}, b {}, ar {}, br {};
    for (int i = 0; i < 4096; ++i)
        original[static_cast<std::size_t> (i)] = 0.3f * std::sin (0.09f * static_cast<float> (i));
    for (int block : { 1, 7, 64, 512 })
    {
        Effect effect;
        effect.prepare ({ 48000.0, 512 });
        effect.setParameters (typename Effect::Parameters {});
        b = original; br = original;
        for (int at = 0; at < 4096; at += block)
            effect.process (b.data() + at, br.data() + at, std::min (block, 4096 - at));
        if (block == 1) { a = b; ar = br; }
        else
            for (int i = 0; i < 4096; ++i)
            {
                REQUIRE (b[static_cast<std::size_t> (i)] == Catch::Approx (a[static_cast<std::size_t> (i)]).margin (0.000001));
                REQUIRE (br[static_cast<std::size_t> (i)] == Catch::Approx (ar[static_cast<std::size_t> (i)]).margin (0.000001));
            }
    }
}

template <typename Effect>
void toggleCheck()
{
    Effect effect;
    effect.prepare ({ 48000.0, 512 });
    typename Effect::Parameters p;
    p.enabled = false;
    effect.setParameters (p);
    std::array<float, 512> l {}, r {};
    float previous = 0.1f;
    for (int block = 0; block < 6; ++block)
    {
        p.enabled = block >= 2 && block < 4;
        effect.setParameters (p);
        l.fill (0.1f); r.fill (0.1f);
        effect.process (l.data(), r.data(), 512);
        REQUIRE (std::abs (l[0] - previous) < 0.025f);
        previous = l.back();
    }
}

template <typename Effect>
void finiteExtreme (typename Effect::Parameters p)
{
    Effect effect;
    effect.prepare ({ 192000.0, 512 });
    effect.setParameters (p);
    std::array<float, 512> l {}, r {};
    for (int block = 0; block < 8; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            l[static_cast<std::size_t> (i)] = static_cast<float> ((i * 173 + block * 91) % 2001 - 1000) / 1000.0f;
            r[static_cast<std::size_t> (i)] = -l[static_cast<std::size_t> (i)];
        }
        effect.process (l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i)
        {
            REQUIRE (std::isfinite (l[static_cast<std::size_t> (i)]));
            REQUIRE (std::isfinite (r[static_cast<std::size_t> (i)]));
        }
    }
}
} // namespace

TEST_CASE ("All six effects stay finite and bypass exactly", "[fx]")
{
    basicChecks<Drive>(); basicChecks<Phaser>(); basicChecks<Chorus>();
    basicChecks<Delay>(); basicChecks<Reverb>(); basicChecks<EQ>();
}

TEST_CASE ("All six effects are independent of block size", "[fx]")
{
    blockChecks<Drive>(); blockChecks<Phaser>(); blockChecks<Chorus>();
    blockChecks<Delay>(); blockChecks<Reverb>(); blockChecks<EQ>();
}

TEST_CASE ("Enable transitions do not click on a held input", "[fx]")
{
    toggleCheck<Drive>(); toggleCheck<Phaser>(); toggleCheck<Chorus>();
    toggleCheck<Delay>(); toggleCheck<Reverb>(); toggleCheck<EQ>();
}

TEST_CASE ("Parameter endpoints stay finite at 192 kHz", "[fx]")
{
    Drive::Parameters drive; drive.drive = 1.0f; drive.tone = 1.0f; drive.outputDb = 24.0f;
    finiteExtreme<Drive> (drive);
    Phaser::Parameters phaser; phaser.stages = 8; phaser.rateHz = 12.0f;
    phaser.depth = 1.0f; phaser.feedback = 0.85f;
    finiteExtreme<Phaser> (phaser);
    Chorus::Parameters chorus; chorus.taps = 3; chorus.rateHz = 8.0f; chorus.depth = 1.0f;
    finiteExtreme<Chorus> (chorus);
    Delay::Parameters delay; delay.timeMs = 2000.0f; delay.feedback = 0.95f;
    delay.lowCutHz = 2000.0f; delay.highCutHz = 18000.0f;
    finiteExtreme<Delay> (delay);
    Reverb::Parameters reverb; reverb.preDelayMs = 200.0f; reverb.size = 1.0f;
    reverb.decaySeconds = 12.0f; reverb.damping = 1.0f;
    finiteExtreme<Reverb> (reverb);
    EQ::Parameters eq; eq.lowGainDb = 18.0f; eq.midGainDb = 18.0f;
    eq.highGainDb = 18.0f; eq.midQ = 10.0f; eq.outputDb = 24.0f;
    finiteExtreme<EQ> (eq);
}

TEST_CASE ("Delay timing and bounded feedback", "[fx]")
{
    Delay effect;
    effect.prepare ({ 48000.0, 512 });
    Delay::Parameters p;
    p.timeMs = 100.0f; p.mix = 1.0f; p.feedback = 0.0f;
    effect.setParameters (p);
    std::vector<float> l (12000), r (12000);
    l[0] = r[0] = 1.0f;
    effect.process (l.data(), r.data(), static_cast<int> (l.size()));
    REQUIRE (l[4800] == Catch::Approx (1.0f).margin (0.00001));
    REQUIRE (l[4799] == Catch::Approx (0.0f).margin (0.00001));
    REQUIRE (l[4801] == Catch::Approx (0.0f).margin (0.00001));
    p.feedback = 0.95f;
    effect.reset(); effect.setParameters (p);
    std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
    l[0] = 1.0f;
    effect.process (l.data(), r.data(), static_cast<int> (l.size()));
    for (float v : l) REQUIRE (std::abs (v) <= 1.01f);
}

TEST_CASE ("Delay echoes decay to exact zero and never through subnormals", "[fx]")
{
    // Subnormal floats cost many cycles on x86 and hosts flag them; a fading
    // feedback tail must reach 0 instead of lingering below FLT_MIN.
    Delay effect;
    effect.prepare ({ 48000.0, 512 });
    Delay::Parameters p;
    p.timeMs = 10.0f; p.mix = 0.5f; p.feedback = 0.5f;
    effect.setParameters (p);
    std::vector<float> l (512), r (512);
    l[0] = r[0] = 1.0f;
    int subnormals = 0;
    float last = 1.0f;
    for (int block = 0; block < 1000; ++block) // ~10.7 s, about 1000 halvings
    {
        effect.process (l.data(), r.data(), static_cast<int> (l.size()));
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            subnormals += std::fpclassify (l[i]) == FP_SUBNORMAL ? 1 : 0;
            subnormals += std::fpclassify (r[i]) == FP_SUBNORMAL ? 1 : 0;
            last = std::abs (l[i]) + std::abs (r[i]);
        }
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
    }
    CHECK (subnormals == 0);
    CHECK (last == 0.0f);
}

TEST_CASE ("Delay tempo division maps to the exact echo sample", "[fx]")
{
    Delay effect;
    effect.prepare ({ 48000.0, 512 });
    effect.setTempo (120.0);
    Delay::Parameters p;
    p.tempoSync = true; p.noteDivision = 0.5f; p.mix = 1.0f; p.feedback = 0.0f;
    effect.setParameters (p);
    std::vector<float> l (24000), r (24000);
    l[0] = r[0] = 1.0f;
    effect.process (l.data(), r.data(), static_cast<int> (l.size()));
    REQUIRE (l[12000] == Catch::Approx (1.0f).margin (0.00001));
    REQUIRE (l[11999] == Catch::Approx (0.0f).margin (0.00001));
}

TEST_CASE ("EQ bands reach their nominal gain", "[fx]")
{
    auto measure = [] (EQ::Parameters p, float frequency) {
        EQ eq; eq.prepare ({ 48000.0, 512 }); eq.setParameters (p);
        constexpr int count = 48000;
        std::vector<float> l (count), r (count);
        for (int i = 0; i < count; ++i)
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = 0.1f * std::sin (2.0f * detail::pi * frequency * static_cast<float> (i) / 48000.0f);
        eq.process (l.data(), r.data(), count);
        double wet = 0.0, dry = 0.0;
        for (int i = count / 2; i < count; ++i)
        {
            wet += static_cast<double> (l[static_cast<std::size_t> (i)]) * l[static_cast<std::size_t> (i)];
            const double x = 0.1 * std::sin (2.0 * detail::pi * frequency * static_cast<double> (i) / 48000.0);
            dry += x * x;
        }
        return 10.0 * std::log10 (wet / dry);
    };
    EQ::Parameters p;
    p.midGainDb = 6.0f;
    REQUIRE (measure (p, 1200.0f) == Catch::Approx (6.0).margin (0.5));
    p = {}; p.outputDb = -3.0f;
    REQUIRE (measure (p, 1000.0f) == Catch::Approx (-3.0).margin (0.5));
    p = {}; p.lowGainDb = 6.0f;
    REQUIRE (measure (p, 40.0f) == Catch::Approx (6.0).margin (0.5));
    p = {}; p.highGainDb = 6.0f;
    REQUIRE (measure (p, 16000.0f) == Catch::Approx (6.0).margin (0.5));
}

TEST_CASE ("Drive compensation controls level and ADAA reduces folded harmonics", "[fx]")
{
    constexpr int count = 48000;
    auto render = [] (float amount) {
        Drive effect; effect.prepare ({ 48000.0, 512 });
        Drive::Parameters p; p.drive = amount; p.tone = 1.0f; p.mix = 1.0f;
        effect.setParameters (p);
        std::vector<float> l (count), r (count);
        for (int i = 0; i < count; ++i)
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] =
                0.3f * std::sin (2.0f * detail::pi * 5000.0f * static_cast<float> (i) / 48000.0f);
        effect.process (l.data(), r.data(), count);
        return l;
    };
    const auto low = render (0.1f), high = render (0.9f);
    double lowEnergy = 0.0, highEnergy = 0.0;
    for (int i = count / 2; i < count; ++i)
    {
        lowEnergy += static_cast<double> (low[static_cast<std::size_t> (i)]) * low[static_cast<std::size_t> (i)];
        highEnergy += static_cast<double> (high[static_cast<std::size_t> (i)]) * high[static_cast<std::size_t> (i)];
    }
    REQUIRE (highEnergy / lowEnergy > 0.2);
    REQUIRE (highEnergy / lowEnergy < 2.0);
    auto bin = [] (const std::vector<float>& signal, int hz) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double phase = 2.0 * static_cast<double> (detail::pi) * hz * i / count;
            re += signal[static_cast<std::size_t> (i)] * std::cos (phase);
            im -= signal[static_cast<std::size_t> (i)] * std::sin (phase);
        }
        return std::hypot (re, im) / count;
    };
    // The 7th harmonic at 35 kHz folds to 13 kHz. Compare to a direct tanh clipper.
    std::vector<float> naive (count);
    const float gain = 1.0f + 14.0f * 0.9f * 0.9f;
    for (int i = 0; i < count; ++i)
    {
        const float x = 0.3f * std::sin (2.0f * detail::pi * 5000.0f * static_cast<float> (i) / 48000.0f);
        naive[static_cast<std::size_t> (i)] = std::tanh (gain * x);
    }
    REQUIRE (bin (high, 13000) < 0.6 * bin (naive, 13000));
}

TEST_CASE ("Reverb tail follows decay and decorrelates stereo", "[fx]")
{
    auto render = [] (float seconds) {
        Reverb effect; effect.prepare ({ 48000.0, 512 });
        Reverb::Parameters p; p.decaySeconds = seconds; p.mix = 1.0f; p.width = 1.0f;
        effect.setParameters (p);
        std::vector<float> l (48000 * 6), r (48000 * 6);
        l[0] = r[0] = 1.0f;
        effect.process (l.data(), r.data(), static_cast<int> (l.size()));
        return std::pair { l, r };
    };
    const auto shortTail = render (0.8f), longTail = render (3.0f);
    auto energy = [] (const std::vector<float>& x, int from, int to) {
        double value = 0.0;
        for (int i = from; i < to; ++i)
            value += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i)];
        return value;
    };
    const double shortLate = energy (shortTail.first, 96000, 120000);
    const double longLate = energy (longTail.first, 96000, 120000);
    REQUIRE (longLate > shortLate * 10.0);
    REQUIRE (energy (longTail.first, 240000, 264000) < longLate);
    double dot = 0.0, ll = 0.0, rr = 0.0;
    for (int i = 48000; i < 144000; ++i)
    {
        const double l = longTail.first[static_cast<std::size_t> (i)];
        const double r = longTail.second[static_cast<std::size_t> (i)];
        dot += l * r; ll += l * l; rr += r * r;
    }
    REQUIRE (std::abs (dot / std::sqrt (ll * rr)) < 0.95);
}

TEST_CASE ("Modulated effects preserve a mono sum", "[fx]")
{
    auto check = [] (auto effect) {
        effect.prepare ({ 48000.0, 512 });
        effect.setParameters (typename decltype (effect)::Parameters {});
        std::vector<float> l (48000), r (48000);
        for (int i = 0; i < 48000; ++i)
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = 0.2f * std::sin (2.0f * detail::pi * 440.0f * static_cast<float> (i) / 48000.0f);
        effect.process (l.data(), r.data(), 48000);
        double sum = 0.0;
        for (int i = 24000; i < 48000; ++i)
            sum += std::pow (0.5 * (l[static_cast<std::size_t> (i)] + r[static_cast<std::size_t> (i)]), 2.0);
        REQUIRE (sum > 1.0);
    };
    check (Phaser {}); check (Chorus {});
}
