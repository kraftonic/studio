// SPDX-License-Identifier: MIT
//
// Table generation. Each frame is described as a harmonic spectrum C[1..512]
// with x(t) = sum_n Re (C[n] * exp (i 2 pi n t)). Each mip level keeps only the
// harmonics that fit and is rendered with an inverse FFT, so every level is
// band-limited exactly. Frames are peak-normalised (one gain per frame, shared
// by all its levels so the levels match in loudness).

#include "Wavetables.h"

#include "Fft.h"
#include "kraftonic/core/Math.h"

#include <complex>
#include <cstdint>

namespace kraftonic::wavetable::detail
{
namespace
{
using Complex = std::complex<double>;
using Spectrum = std::vector<Complex>; // index = harmonic number, [0] unused
constexpr double kPi = core::kPi;

/** Coefficient of a sin (2 pi n t + phase). */
Complex sineTerm (double amplitude, double phase)
{
    return amplitude * Complex (std::sin (phase), -std::cos (phase));
}

double smoothStep (double x)
{
    x = std::clamp (x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

/** Deterministic hash -> [0, 1), so tables are identical on every platform. */
double hashUnit (std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<double> (x) / 4294967296.0;
}

/** Spreads a partial at a fractional harmonic position over its two
    neighbours, so partials move continuously as position changes. */
void addPartial (Spectrum& c, double harmonic, double amplitude, double phase)
{
    if (harmonic < 1.0 || harmonic > static_cast<double> (kMaxHarmonics))
        return;
    const auto low = static_cast<int> (std::floor (harmonic));
    const double frac = harmonic - static_cast<double> (low);
    c[static_cast<std::size_t> (low)] += sineTerm (amplitude * (1.0 - frac), phase);
    if (frac > 0.0 && low + 1 <= kMaxHarmonics)
        c[static_cast<std::size_t> (low + 1)] += sineTerm (amplitude * frac, phase);
}

// ---- 0 Basic: sine -> triangle -> saw -> square ----------------------------
void basic (Spectrum& c, double t)
{
    auto shape = [] (int which, int n) -> double
    {
        const bool odd = (n % 2) == 1;
        switch (which)
        {
            case 0: return n == 1 ? 1.0 : 0.0;
            case 1: return odd ? (8.0 / (kPi * kPi)) * (((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (n * n) : 0.0;
            case 2: return (2.0 / kPi) * (n % 2 == 1 ? 1.0 : -1.0) / n;
            default: return odd ? (4.0 / kPi) / n : 0.0;
        }
    };
    const double segment = std::clamp (t * 3.0, 0.0, 3.0);
    const int from = std::min (static_cast<int> (segment), 2);
    const double mix = segment - from;
    for (int n = 1; n <= kMaxHarmonics; ++n)
    {
        const double a = (1.0 - mix) * shape (from, n) + mix * shape (from + 1, n);
        c[static_cast<std::size_t> (n)] = sineTerm (a, 0.0);
    }
}

// ---- 1 Harmonic: additive series sweeping from dark to bright ---------------
void harmonic (Spectrum& c, double t)
{
    const double brightness = 0.6 * std::pow (400.0 / 0.6, t); // e-folding harmonic count
    const double evenWeight = 1.0 - 0.7 * std::sin (kPi * t);  // hollow in the middle
    for (int n = 1; n <= kMaxHarmonics; ++n)
    {
        double a = std::pow (static_cast<double> (n), -1.1) * std::exp (-(n - 1) / brightness);
        if (n % 2 == 0)
            a *= evenWeight;
        const double phase = kPi * static_cast<double> (n * n) / 64.0; // low crest factor
        c[static_cast<std::size_t> (n)] = sineTerm (a, phase);
    }
}

// ---- 2 Pulse sweep: centred pulse, width 50% -> 3% ------------------------
void pulse (Spectrum& c, double t)
{
    const double width = 0.5 - 0.47 * t;
    for (int n = 1; n <= kMaxHarmonics; ++n)
        c[static_cast<std::size_t> (n)] = Complex (2.0 * std::sin (kPi * n * width) / (kPi * n), 0.0);
}

// ---- 3 Formant: vowel-like resonances over a buzzy source ------------------
void formant (Spectrum& c, double t)
{
    // Textbook average formant frequencies (Hz) for five vowels.
    constexpr std::array<std::array<double, 3>, 5> vowels {{
        { 730.0, 1090.0, 2440.0 }, // a
        { 530.0, 1840.0, 2480.0 }, // e
        { 270.0, 2290.0, 3010.0 }, // i
        { 570.0, 840.0, 2410.0 },  // o
        { 300.0, 870.0, 2240.0 },  // u
    }};
    constexpr std::array<double, 3> gains { 1.0, 0.6, 0.3 };
    constexpr std::array<double, 3> bandwidths { 90.0, 110.0, 160.0 };
    constexpr double referenceHz = 130.81; // the table "speaks" its vowels around C3

    const double segment = std::clamp (t * 4.0, 0.0, 4.0);
    const int from = std::min (static_cast<int> (segment), 3);
    const double mix = smoothStep (segment - from);
    std::array<double, 3> f {};
    for (std::size_t k = 0; k < 3; ++k)
        f[k] = std::exp ((1.0 - mix) * std::log (vowels[static_cast<std::size_t> (from)][k])
                         + mix * std::log (vowels[static_cast<std::size_t> (from + 1)][k]));
    for (int n = 1; n <= kMaxHarmonics; ++n)
    {
        const double hz = n * referenceHz;
        double envelope = 0.015;
        for (std::size_t k = 0; k < 3; ++k)
        {
            const double d = (hz - f[k]) / (0.5 * bandwidths[k]);
            envelope += gains[k] / std::sqrt (1.0 + d * d);
        }
        const double a = envelope / std::pow (static_cast<double> (n), 0.7);
        c[static_cast<std::size_t> (n)] = sineTerm (a, kPi * static_cast<double> (n * n) / 128.0);
    }
}

// ---- 4 Metallic: bar and plate partial ratios, stretched apart -------------
void metallic (Spectrum& c, double t)
{
    // Free-bar and circular-plate mode ratios (physics textbook values).
    constexpr std::array<double, 6> bar { 1.0, 2.756, 5.404, 8.933, 13.344, 18.638 };
    constexpr std::array<double, 7> plate { 1.0, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156 };
    const double stretch = 1.0 + 2.0 * t;
    addPartial (c, 1.0, 0.7, 0.0); // keeps the played pitch
    for (std::size_t j = 0; j < bar.size(); ++j)
        addPartial (c, 1.0 + (bar[j] - 1.0) * stretch, 1.0 / (1.0 + 0.45 * static_cast<double> (j)),
                    core::kTwoPi * hashUnit (static_cast<std::uint32_t> (j) + 11U));
    for (std::size_t j = 0; j < plate.size(); ++j)
        addPartial (c, plate[j] * (3.0 + 4.0 * t), 0.45 * t + 0.1,
                    core::kTwoPi * hashUnit (static_cast<std::uint32_t> (j) + 101U));
}

// ---- 5 Digital: stepped, bit-crushed shapes (rendered, then band-limited) ---
void digital (Spectrum& c, double t, const Fft& transform)
{
    constexpr int analysisSize = 8192;
    const double steps = std::round (64.0 * std::pow (2.0, -4.0 * t));            // 64 -> 4 steps
    const double levels = std::round (2.0 + 30.0 * (1.0 - t) * (1.0 - t));         // 32 -> 2 levels
    std::vector<Complex> x (analysisSize);
    for (int i = 0; i < analysisSize; ++i)
    {
        const double phase = static_cast<double> (i) / analysisSize;
        const double held = (std::floor (phase * steps) + 0.5) / steps;
        const double source = std::sin (core::kTwoPi * held) + 0.45 * std::sin (3.0 * core::kTwoPi * held + 0.6 * t);
        x[static_cast<std::size_t> (i)] = std::round (source * 0.5 * levels) / (0.5 * levels);
    }
    transform.transform (x, false);
    for (int n = 1; n <= kMaxHarmonics; ++n)
        c[static_cast<std::size_t> (n)] = 2.0 * x[static_cast<std::size_t> (n)] / static_cast<double> (analysisSize);
}

// ---- 6 Organ: drawbar-style registrations (8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1') --
void organ (Spectrum& c, double t)
{
    constexpr std::array<int, 7> harmonics { 1, 2, 3, 4, 5, 6, 8 };
    constexpr std::array<std::array<int, 7>, 6> registrations {{
        { 8, 0, 0, 0, 0, 0, 0 }, // soft flute
        { 8, 8, 0, 0, 0, 0, 0 },
        { 8, 8, 8, 0, 0, 0, 0 },
        { 8, 6, 8, 8, 0, 0, 4 },
        { 6, 0, 8, 0, 8, 6, 0 }, // hollow
        { 8, 8, 8, 8, 6, 6, 8 }, // full
    }};
    const double segment = std::clamp (t * 5.0, 0.0, 5.0);
    const int from = std::min (static_cast<int> (segment), 4);
    const double mix = segment - from;
    auto level = [] (int drawbar) { return drawbar == 0 ? 0.0 : std::pow (10.0, (drawbar - 8) * 3.0 / 20.0); };
    for (std::size_t k = 0; k < harmonics.size(); ++k)
    {
        const double a = (1.0 - mix) * level (registrations[static_cast<std::size_t> (from)][k])
                         + mix * level (registrations[static_cast<std::size_t> (from + 1)][k]);
        c[static_cast<std::size_t> (harmonics[k])] = sineTerm (a, 0.0);
        // A little harmonic content per "wheel" so it is not perfectly pure.
        const int third = harmonics[k] * 3;
        if (third <= kMaxHarmonics)
            c[static_cast<std::size_t> (third)] += sineTerm (0.02 * a, 0.0);
    }
}

// ---- 7 Noise: fixed random spectrum with a band moving dark -> bright -------
void noise (Spectrum& c, double t)
{
    const double centre = std::log2 (2.0) + 8.0 * t; // harmonic 2 -> 512 (log2)
    for (int n = 1; n <= kMaxHarmonics; ++n)
    {
        const double magnitude = 0.25 + 0.75 * hashUnit (static_cast<std::uint32_t> (n) * 2U + 7U);
        const double phase = core::kTwoPi * hashUnit (static_cast<std::uint32_t> (n) * 2U + 8U);
        const double d = (std::log2 (static_cast<double> (n)) - centre) / 1.3;
        const double envelope = std::exp (-0.5 * d * d) + (n == 1 ? 0.25 : 0.0);
        c[static_cast<std::size_t> (n)] = sineTerm (magnitude * envelope / std::sqrt (static_cast<double> (n)), phase);
    }
}

void spectrum (Table table, double t, Spectrum& c, const Fft& transform)
{
    std::fill (c.begin(), c.end(), Complex {});
    switch (table)
    {
        case Table::basic: basic (c, t); break;
        case Table::harmonic: harmonic (c, t); break;
        case Table::pulse: pulse (c, t); break;
        case Table::formant: formant (c, t); break;
        case Table::metallic: metallic (c, t); break;
        case Table::digital: digital (c, t, transform); break;
        case Table::organ: organ (c, t); break;
        case Table::noise: noise (c, t); break;
    }
}
} // namespace

WavetableBank::WavetableBank()
{
    std::size_t total = 0;
    for (int level = 0; level < kLevels; ++level)
    {
        offsets[static_cast<std::size_t> (level)] = total;
        total += static_cast<std::size_t> (kFrames) * static_cast<std::size_t> (levelSize (level) + 3);
    }

    // Two frames per inverse FFT: both spectra are Hermitian, so frame A comes
    // out in the real part and frame B in the imaginary part.
    static_assert (kFrames % 2 == 0);
    Spectrum a (kMaxHarmonics + 1), b (kMaxHarmonics + 1);
    const Fft transform (static_cast<std::size_t> (levelSize (0)));
    std::vector<Complex> buffer;
    for (int table = 0; table < kNumTables; ++table)
    {
        auto& data = tables[static_cast<std::size_t> (table)];
        data.assign (total, 0.0f);
        for (int f = 0; f < kFrames; f += 2)
        {
            spectrum (static_cast<Table> (table), static_cast<double> (f) / (kFrames - 1), a, transform);
            spectrum (static_cast<Table> (table), static_cast<double> (f + 1) / (kFrames - 1), b, transform);
            double gainA = 1.0, gainB = 1.0;
            for (int level = 0; level < kLevels; ++level)
            {
                const int size = levelSize (level);
                const int harmonics = levelHarmonics (level);
                buffer.assign (static_cast<std::size_t> (size), Complex {});
                const Complex i (0.0, 1.0);
                for (int n = 1; n <= harmonics; ++n)
                {
                    const auto& ca = a[static_cast<std::size_t> (n)];
                    const auto& cb = b[static_cast<std::size_t> (n)];
                    buffer[static_cast<std::size_t> (n)] = 0.5 * (ca + i * cb);
                    buffer[static_cast<std::size_t> (size - n)] = 0.5 * (std::conj (ca) + i * std::conj (cb));
                }
                transform.transform (buffer, true);
                if (level == 0)
                {
                    double peakA = 0.0, peakB = 0.0;
                    for (const auto& v : buffer)
                    {
                        peakA = std::max (peakA, std::abs (v.real()));
                        peakB = std::max (peakB, std::abs (v.imag()));
                    }
                    gainA = peakA > 1.0e-9 ? 1.0 / peakA : 0.0;
                    gainB = peakB > 1.0e-9 ? 1.0 / peakB : 0.0;
                }
                for (int k = 0; k < 2; ++k)
                {
                    float* out = data.data() + offsets[static_cast<std::size_t> (level)]
                                 + static_cast<std::size_t> (f + k) * static_cast<std::size_t> (size + 3) + 1;
                    for (int s = 0; s < size; ++s)
                    {
                        const auto& v = buffer[static_cast<std::size_t> (s)];
                        out[s] = static_cast<float> (k == 0 ? v.real() * gainA : v.imag() * gainB);
                    }
                    out[-1] = out[size - 1];
                    out[size] = out[0];
                    out[size + 1] = out[1];
                }
            }
        }
    }
}

const WavetableBank& WavetableBank::instance()
{
    static const WavetableBank bank;
    return bank;
}
} // namespace kraftonic::wavetable::detail
