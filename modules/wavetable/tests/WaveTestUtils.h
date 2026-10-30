// SPDX-License-Identifier: MIT
#pragma once

#include <kraftonic/core/Math.h>
#include <kraftonic/wavetable/WaveEngine.h>

#include "../src/dsp/Fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace wavetest
{
using namespace kraftonic::wavetable;

struct Stereo
{
    std::vector<float> left, right;
};

/** Renders `samples` frames, delivering events (absolute sampleOffset) in blocks. */
inline Stereo render (WaveEngine& engine, int samples, int blockSize = 256, std::vector<Event> events = {})
{
    Stereo out;
    out.left.resize (static_cast<std::size_t> (samples));
    out.right.resize (out.left.size());
    std::size_t next = 0;
    for (int pos = 0; pos < samples; pos += blockSize)
    {
        const int count = std::min (blockSize, samples - pos);
        std::array<Event, 64> local {};
        int used = 0;
        while (next < events.size() && events[next].sampleOffset < pos + count && used < 64)
        {
            local[static_cast<std::size_t> (used)] = events[next++];
            local[static_cast<std::size_t> (used++)].sampleOffset -= pos;
        }
        engine.process ({ out.left.data() + pos, out.right.data() + pos, count }, local.data(), used);
    }
    return out;
}

inline WaveParameters testParams()
{
    auto p = WaveParameters::defaults();
    p.set (ParamId::outputGain, 0.0f);
    p.set (ParamId::filterCutoff, 20000.0f);
    p.set (ParamId::filterKeyTrack, 0.0f);
    p.set (ParamId::filterResonance, 0.0f);
    p.set (ParamId::filterMode, 0.0f);
    p.set (ParamId::ampAttack, 0.001f);
    p.set (ParamId::ampSustain, 1.0f);
    p.set (ParamId::ampRelease, 0.02f);
    p.set (ParamId::velocityToLevel, 0.0f);
    return p;
}

inline Event noteOn (int at, int note, float velocity = 1.0f)
{
    return { Event::Type::noteOn, at, note, velocity };
}

inline Event noteOff (int at, int note)
{
    return { Event::Type::noteOff, at, note, 0.0f };
}

inline Event control (Event::Type type, int at, float value)
{
    return { type, at, 0, value };
}

inline std::vector<float> slice (const std::vector<float>& x, int from, int count)
{
    return { x.begin() + from, x.begin() + from + count };
}

inline double rms (const std::vector<float>& x)
{
    double sum = 0.0;
    for (float s : x)
        sum += static_cast<double> (s) * static_cast<double> (s);
    return x.empty() ? 0.0 : std::sqrt (sum / static_cast<double> (x.size()));
}

inline float peak (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float s : x)
        p = std::max (p, std::abs (s));
    return p;
}

inline double db (double ratio)
{
    return 20.0 * std::log10 (std::max (ratio, 1.0e-30));
}

/** Power spectrum (|X|^2) of x with a Blackman-Harris window, size = power of two. */
inline std::vector<double> powerSpectrum (const std::vector<float>& x, bool window = true)
{
    std::vector<std::complex<double>> data (x.size());
    const double n = static_cast<double> (x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        double w = 1.0;
        if (window)
        {
            const double t = kraftonic::core::kTwoPi * static_cast<double> (i) / n;
            w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        }
        data[i] = static_cast<double> (x[i]) * w;
    }
    kraftonic::wavetable::detail::fft (data, false);
    std::vector<double> power (x.size() / 2);
    for (std::size_t i = 0; i < power.size(); ++i)
        power[i] = std::norm (data[i]);
    return power;
}

/** Ratio (dB) of energy away from the harmonics of `hz` to the total. Bins
    within `guard` of a harmonic count as harmonic. */
inline double inharmonicDb (const std::vector<float>& x, double hz, double sampleRate, int guard = 6)
{
    const auto power = powerSpectrum (x);
    const double binHz = sampleRate / static_cast<double> (x.size());
    double total = 0.0, outside = 0.0;
    for (std::size_t b = 1; b < power.size(); ++b)
    {
        const double f = static_cast<double> (b) * binHz;
        const double h = std::round (f / hz);
        const bool harmonic = h >= 1.0 && std::abs (f - h * hz) <= guard * binHz;
        total += power[b];
        if (! harmonic)
            outside += power[b];
    }
    return 10.0 * std::log10 (std::max (outside, 1.0e-30) / std::max (total, 1.0e-30));
}

/** Frequency estimate from interpolated rising zero crossings. */
inline double zeroCrossingHz (const std::vector<float>& x, double sampleRate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1)
                             + static_cast<double> (x[i - 1]) / static_cast<double> (x[i - 1] - x[i]);
            if (first < 0.0)
                first = t;
            else
                ++crossings;
            last = t;
        }
    return crossings > 0 ? sampleRate * crossings / (last - first) : 0.0;
}

inline double cents (double measured, double expected)
{
    return 1200.0 * std::log2 (measured / expected);
}
} // namespace wavetest
