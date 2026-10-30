// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace kraftonic::wavetable::detail
{
/** The eight factory wavetables, in contract order (ParamId::oscATable). */
enum class Table : int
{
    basic = 0,    // sine -> triangle -> saw -> square
    harmonic = 1, // additive series, dark to bright
    pulse = 2,    // pulse width 50% -> 3%
    formant = 3,  // vowel-like formants A -> E -> I -> O -> U
    metallic = 4, // bar and plate partial clusters drifting apart
    digital = 5,  // sample-and-hold steps and bit quantisation
    organ = 6,    // drawbar-style registrations
    noise = 7     // random spectra with a moving band
};

inline constexpr int kNumTables = 8;
inline constexpr int kFrames = 64;
inline constexpr int kLevels = 10;          // one mip level per octave
inline constexpr int kMaxHarmonics = 512;   // level 0
inline constexpr double kAliasLimit = 0.45; // highest harmonic stays below 0.45 * sample rate

/** Harmonics kept in a mip level: 512, 256, ... 1. */
constexpr int levelHarmonics (int level) noexcept
{
    return kMaxHarmonics >> level;
}

/** Samples per cycle in a mip level: at least 16 per period of its top harmonic,
    which keeps cubic-interpolation images 65-120 dB down. */
constexpr int levelSize (int level) noexcept
{
    return std::max (1024, 8192 >> level);
}

/** Smallest mip level whose top harmonic stays under kAliasLimit for a phase
    increment (cycles per sample) of `maxIncrement`. */
inline int mipLevelFor (double maxIncrement) noexcept
{
    const double x = maxIncrement * (static_cast<double> (kMaxHarmonics) / kAliasLimit);
    if (! (x > 1.0))
        return 0;
    int exponent = 0;
    const double mantissa = std::frexp (x, &exponent);
    const int level = mantissa == 0.5 ? exponent - 1 : exponent;
    return std::min (level, kLevels - 1);
}

/** Gain that fades a (sine) top level out between 0.45 and 0.5 cycles/sample. */
inline float nyquistFade (double maxIncrement) noexcept
{
    return static_cast<float> (std::clamp ((0.5 - maxIncrement) / (0.5 - kAliasLimit), 0.0, 1.0));
}

/** Catmull-Rom read of one cycle. `frame` points at sample 0 and has one
    guard sample before and two after. `phase` is in [0, 1). */
inline float readCycle (const float* frame, int size, double phase) noexcept
{
    const double x = phase * static_cast<double> (size);
    const int i = std::min (static_cast<int> (x), size - 1);
    const float t = static_cast<float> (x - static_cast<double> (i));
    const float* s = frame + i;
    const float c1 = 0.5f * (s[1] - s[-1]);
    const float c2 = s[-1] - 2.5f * s[0] + 2.0f * s[1] - 0.5f * s[2];
    const float c3 = 0.5f * (s[2] - s[-1]) + 1.5f * (s[0] - s[1]);
    return ((c3 * t + c2) * t + c1) * t + s[0];
}

/** All eight tables, generated once per process from additive/FFT spectra
    (no sample files) and shared, read-only, by every engine instance.
    Every level of every frame is band-limited by construction. */
class WavetableBank
{
public:
    /** Builds on first use (not real-time; call from prepare()). */
    static const WavetableBank& instance();

    /** Pointer to sample 0 of a frame; readable from [-1] to [size + 1]. */
    const float* frame (int table, int level, int frameIndex) const noexcept
    {
        return tables[static_cast<std::size_t> (table)].data()
               + offsets[static_cast<std::size_t> (level)]
               + static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (levelSize (level) + 3) + 1;
    }

    WavetableBank (const WavetableBank&) = delete;
    WavetableBank& operator= (const WavetableBank&) = delete;

private:
    WavetableBank();
    std::array<std::vector<float>, kNumTables> tables;
    std::array<std::size_t, kLevels> offsets {};
};
} // namespace kraftonic::wavetable::detail
