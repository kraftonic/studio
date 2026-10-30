// SPDX-License-Identifier: MIT
#pragma once

#include "Random.h"
#include "Wavetables.h"

#include <array>
#include <cmath>

namespace kraftonic::wavetable::detail
{
inline constexpr int kMaxUnison = 8;

/** How a unison stack is laid out right now. Computed once per sample for
    the whole engine (every voice shares it), so voices only read it. */
struct UnisonLayout
{
    int copies = 1;         // copies to render, including ones fading out
    double maxRatio = 1.0;  // highest detune ratio (picks the mip level)
    std::array<double, kMaxUnison> ratio {};
    std::array<float, kMaxUnison> gainLeft {};
    std::array<float, kMaxUnison> gainRight {};
};

/** What to read: table (and the one it is crossfading from) and position. */
struct TableSelection
{
    int table = 0;
    int previousTable = 0;
    float mix = 1.0f; // weight of `table`; 1 - mix of `previousTable`
};

/** Interpolated read across two adjacent frames (position) and within the
    cycle (Catmull-Rom). */
inline float readMorph (const float* a, const float* b, float frac, int size, double phase) noexcept
{
    const double x = phase * static_cast<double> (size);
    const int i = std::min (static_cast<int> (x), size - 1);
    const float t = static_cast<float> (x - static_cast<double> (i));
    auto hermite = [t] (const float* s) noexcept
    {
        const float c1 = 0.5f * (s[1] - s[-1]);
        const float c2 = s[-1] - 2.5f * s[0] + 2.0f * s[1] - 0.5f * s[2];
        const float c3 = 0.5f * (s[2] - s[-1]) + 1.5f * (s[0] - s[1]);
        return ((c3 * t + c2) * t + c1) * t + s[0];
    };
    const float va = hermite (a + i);
    const float vb = hermite (b + i);
    return va + frac * (vb - va);
}

/** A band-limited wavetable oscillator with up to eight unison copies. */
class WaveOscillator
{
public:
    /** Unison 1 starts at phase 0 (consistent attack); stacked copies start
        at random (but deterministic, seeded) phases so they never comb. */
    void start (Random& random, bool randomise) noexcept
    {
        for (auto& p : phases)
            p = randomise ? static_cast<double> (random.nextUnit()) : 0.0;
        if (! randomise)
            for (std::size_t i = 1; i < phases.size(); ++i)
                phases[i] = static_cast<double> (random.nextUnit());
    }

    /** Adds the stack's output to left/right. `increment` = frequency / sample rate. */
    void process (const WavetableBank& bank, const UnisonLayout& layout, const TableSelection& selection,
                  float position, double increment, float& left, float& right) noexcept
    {
        const double maxIncrement = increment * layout.maxRatio;
        const float fade = nyquistFade (maxIncrement);
        if (fade <= 0.0f)
        {
            advance (layout, increment);
            return;
        }
        const int level = mipLevelFor (maxIncrement);
        const int size = levelSize (level);
        const float framePosition = std::clamp (position, 0.0f, 1.0f) * static_cast<float> (kFrames - 1);
        const int f0 = std::min (static_cast<int> (framePosition), kFrames - 2);
        const float frac = framePosition - static_cast<float> (f0);
        const float* a = bank.frame (selection.table, level, f0);
        const float* b = bank.frame (selection.table, level, f0 + 1);
        const bool crossfade = selection.mix < 1.0f;
        const float* c = crossfade ? bank.frame (selection.previousTable, level, f0) : a;
        const float* d = crossfade ? bank.frame (selection.previousTable, level, f0 + 1) : b;

        float sumLeft = 0.0f, sumRight = 0.0f;
        for (int i = 0; i < layout.copies; ++i)
        {
            const auto u = static_cast<std::size_t> (i);
            double& phase = phases[u];
            float v = readMorph (a, b, frac, size, phase);
            if (crossfade)
                v = selection.mix * v + (1.0f - selection.mix) * readMorph (c, d, frac, size, phase);
            sumLeft += v * layout.gainLeft[u];
            sumRight += v * layout.gainRight[u];
            phase += increment * layout.ratio[u];
            if (phase >= 1.0)
                phase -= std::floor (phase);
        }
        left += sumLeft * fade;
        right += sumRight * fade;
    }

    void advance (const UnisonLayout& layout, double increment) noexcept
    {
        for (int i = 0; i < layout.copies; ++i)
        {
            double& phase = phases[static_cast<std::size_t> (i)];
            phase += increment * layout.ratio[static_cast<std::size_t> (i)];
            phase -= std::floor (phase);
        }
    }

    double phase (int copy) const noexcept { return phases[static_cast<std::size_t> (copy)]; }

private:
    std::array<double, kMaxUnison> phases {};
};

/** Computes per-copy detune ratios, equal-power pans and level compensation.
    `weights` are the (smoothed) on/off amounts of each copy, so changing the
    unison count fades copies in and out instead of clicking. */
inline void layoutUnison (UnisonLayout& layout, int count, float detuneCents, float spread,
                          const std::array<float, kMaxUnison>& weights) noexcept
{
    float weightSum = 0.0f;
    int copies = 0;
    for (int i = 0; i < kMaxUnison; ++i)
        if (weights[static_cast<std::size_t> (i)] > 0.0f)
        {
            weightSum += weights[static_cast<std::size_t> (i)];
            copies = i + 1;
        }
    layout.copies = std::max (copies, 1);
    const float compensation = weightSum > 0.0f ? 1.0f / std::sqrt (weightSum) : 1.0f;
    layout.maxRatio = 1.0;
    for (int i = 0; i < layout.copies; ++i)
    {
        const auto u = static_cast<std::size_t> (i);
        // Copies beyond the new count keep the slot of the largest stack while they fade out.
        const int n = i < count ? count : kMaxUnison;
        const float spreadPosition = n > 1 ? 2.0f * static_cast<float> (i) / static_cast<float> (n - 1) - 1.0f : 0.0f;
        layout.ratio[u] = std::exp2 (static_cast<double> (0.5f * detuneCents * spreadPosition) / 1200.0);
        layout.maxRatio = std::max (layout.maxRatio, layout.ratio[u]);
        const float pan = std::clamp (spread * spreadPosition, -1.0f, 1.0f);
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
        const float gain = weights[u] * compensation * 1.41421356f;
        layout.gainLeft[u] = gain * std::cos (angle);
        layout.gainRight[u] = gain * std::sin (angle);
    }
}
} // namespace kraftonic::wavetable::detail
