// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace kraftonic::synth
{
enum class Waveform
{
    saw,
    pulse,
    triangle,
    sawTriangle,
    sine
};
class Oscillator
{
  public:
    Oscillator () noexcept : seed_ (defaultSeed ()), rng_ (seed_)
    {
    }
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) ? std::clamp (sampleRate, 22050.0, 384000.0) : 48000.0;
    }
    void reset (double phase = 0.0) noexcept
    {
        if (!std::isfinite (phase))
            phase = 0.0;
        phase_ = phase - std::floor (phase);
        triangle_ = 0.0;
        drift_ = 0.0;
        driftCount_ = 0;
        rng_ = seed_;
        syncPending_ = false;
        pendingSaw_ = 0.0;
        pendingPulse_ = 0.0;
    }
    bool wrapped () const noexcept
    {
        return wrapped_;
    }
    float wrapFraction () const noexcept
    {
        return wrapFraction_;
    }
    void setSeed (std::uint32_t seed) noexcept
    {
        seed_ = seed == 0 ? 1u : seed;
        rng_ = seed_;
    }
    void setFrequency (double hz) noexcept
    {
        frequency_ = std::isfinite (hz) ? std::clamp (hz, 0.0, sampleRate_ * 0.45) : 0.0;
    }
    void setDriftCents (float cents) noexcept
    {
        driftCents_ = std::isfinite (cents) ? std::clamp (cents, 0.0f, 100.0f) : 0.0f;
    }
    void randomStartPhase () noexcept
    {
        phase_ = randomUnit ();
    }

    /**
     * Hard sync: pass sync=true on the sample where the master reports wrapped(), with
     * syncFraction = the master's wrapFraction(), the point in [0, 1] of the interval
     * up to the next sample where the master wraps. The slave resets there. Every
     * edge in that interval (natural wraps, pulse edges and the reset jump itself) gets
     * a two-sided PolyBLEP: the pre-edge half on this sample, the post-edge half on the
     * next one. No latency is added, so synced and free-running oscillators stay aligned.
     */
    float process (Waveform wave, float pulseWidth = 0.5f, float phaseMod = 0.0f, bool sync = false,
                   float syncFraction = 0.0f) noexcept
    {
        wrapped_ = false;
        wrapFraction_ = 0.0f;
        if (!std::isfinite (pulseWidth))
            pulseWidth = 0.5f;
        if (!std::isfinite (phaseMod))
            phaseMod = 0.0f;
        const double dt = frequency_ * std::exp2 (drift_ * driftCents_ / 1200.0) / sampleRate_;
        if (++driftCount_ >= static_cast<unsigned> (sampleRate_ * 0.13))
        {
            driftCount_ = 0;
            drift_ += (randomUnit () * 2.0 - 1.0 - drift_) * 0.08;
        }
        const double phaseOffset = std::clamp (static_cast<double> (phaseMod), -0.49, 0.49);
        const double phase = phase_ + phaseOffset;
        const double u = phase - std::floor (phase);
        const double blepWidth = std::max (dt, 1.0e-9);
        const double width = std::clamp (static_cast<double> (pulseWidth), 0.02, 0.98);
        float saw = 0.0f;
        float pulse = 0.0f;
        float triangleWave = 0.0f;
        const bool additive = !sync && !hasSynced_ && frequency_ >= 1800.0;
        if (additive)
        {
            const double effectiveFrequency = std::max (frequency_ * std::exp2 (drift_ * driftCents_ / 1200.0), 1.0);
            const int harmonics = static_cast<int> (sampleRate_ * 0.5 / effectiveFrequency);
            double sawSum = 0.0;
            double pulseSum = 2.0 * width - 1.0;
            double triangleSum = 0.0;
            for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
            {
                sawSum -= 2.0 / (3.14159265358979323846 * harmonic) * std::sin (6.283185307179586 * harmonic * u);
                pulseSum += 4.0 / (3.14159265358979323846 * harmonic) *
                            std::sin (3.14159265358979323846 * harmonic * width) *
                            std::cos (6.283185307179586 * harmonic * (u - width * 0.5));
                if ((harmonic & 1) != 0)
                {
                    triangleSum -= 8.0 / (3.14159265358979323846 * 3.14159265358979323846 * harmonic * harmonic) *
                                   std::cos (6.283185307179586 * harmonic * u);
                }
            }
            saw = static_cast<float> (sawSum);
            pulse = static_cast<float> (pulseSum);
            triangleWave = static_cast<float> (triangleSum);
        }
        else
        {
            const double fallPhase = std::fmod (u + 1.0 - width, 1.0);
            saw = static_cast<float> (2.0 * u - 1.0) - polyBlep (u, blepWidth);
            pulse = (u < width ? 1.0f : -1.0f) + polyBlep (u, blepWidth) - polyBlep (fallPhase, blepWidth);
            const float trianglePulse =
                (u < 0.5 ? 1.0f : -1.0f) + polyBlep (u, blepWidth) - polyBlep (std::fmod (u + 0.5, 1.0), blepWidth);
            triangle_ += static_cast<float> (4.0 * dt * trianglePulse);
            triangle_ = std::clamp (triangle_, -1.2f, 1.2f);
            triangleWave = triangle_;
            double sawCorrection = 0.0;
            double pulseCorrection = 0.0;
            if (syncPending_)
            {
                // The previous interval held a sync reset: replace the post-edge half of the
                // normal BLEP terms with the edges that interval really had.
                sawCorrection += blepAfter (u, blepWidth) + pendingSaw_;
                pulseCorrection += blepAfter (fallPhase, blepWidth) - blepAfter (u, blepWidth) + pendingPulse_;
                // Phase modulation can move an edge across this sample after the prediction:
                // give such an edge its post-edge half here.
                const double phaseShift = u - predictedPhase_;
                if (phaseShift < -0.5)
                    sawCorrection += 1.0;
                else if (phaseShift > 0.5)
                    sawCorrection -= 1.0;
                const double predictedPulse = predictedPhase_ < width ? 1.0 : -1.0;
                pulseCorrection -= 0.5 * ((u < width ? 1.0 : -1.0) - predictedPulse);
                syncPending_ = false;
                pendingSaw_ = 0.0;
                pendingPulse_ = 0.0;
            }
            if (sync)
            {
                // The master wraps inside the interval to the next sample. Drop the pre-edge
                // half of the normal BLEP terms (those edges may never happen) and apply a
                // two-sided PolyBLEP to every edge the slave really makes in this interval.
                sawCorrection += blepBefore (u, blepWidth);
                pulseCorrection += blepBefore (fallPhase, blepWidth) - blepBefore (u, blepWidth);
                const double fraction = std::clamp (static_cast<double> (syncFraction), 0.0, 1.0);
                const double resetFrom = u + dt * fraction;
                const double phaseBefore = resetFrom - std::floor (resetFrom);
                const double phaseAfter = phaseOffset - std::floor (phaseOffset);
                addSyncEdges (u, dt * fraction, 0.0, dt, width);
                addSyncEdge (fraction, 2.0 * (phaseAfter - phaseBefore),
                             (phaseAfter < width ? 1.0 : -1.0) - (phaseBefore < width ? 1.0 : -1.0));
                addSyncEdges (phaseAfter, dt * (1.0 - fraction), fraction, dt, width);
                const double predictedEnd = phaseAfter + dt * (1.0 - fraction);
                predictedPhase_ = predictedEnd - std::floor (predictedEnd);
                sawCorrection += edgeSaw_;
                pulseCorrection += edgePulse_;
                edgeSaw_ = 0.0;
                edgePulse_ = 0.0;
                syncPending_ = true;
            }
            saw += static_cast<float> (sawCorrection);
            pulse += static_cast<float> (pulseCorrection);
        }
        const float sine = static_cast<float> (std::sin (6.283185307179586 * u));
        float output = saw;
        if (wave == Waveform::pulse)
            output = pulse;
        else if (wave == Waveform::sine)
            output = sine;
        else if (wave == Waveform::triangle)
            output = triangleWave;
        else if (wave == Waveform::sawTriangle)
            output = 0.5f * (saw + triangleWave);

        if (sync)
        {
            hasSynced_ = true;
            const double fraction = std::clamp (static_cast<double> (syncFraction), 0.0, 1.0);
            phase_ = dt * (1.0 - fraction);
        }
        else
        {
            const double nextPhase = phase_ + dt;
            if (nextPhase >= 1.0 - 1.0e-12)
            {
                wrapped_ = true;
                wrapFraction_ = dt > 0.0 ? static_cast<float> (std::clamp ((1.0 - phase_) / dt, 0.0, 1.0)) : 0.0f;
                phase_ = nextPhase - std::floor (nextPhase);
            }
            else
            {
                phase_ = nextPhase;
            }
        }
        return std::isfinite (output) ? output : 0.0f;
    }

  private:
    static std::uint32_t defaultSeed () noexcept
    {
        static std::atomic<std::uint32_t> next{0x9e3779b9u};
        const std::uint32_t value = next.fetch_add (0x6d2b79f5u, std::memory_order_relaxed);
        return value == 0 ? 1u : value;
    }
    static float polyBlep (double phase, double width) noexcept
    {
        if (phase < width)
        {
            phase /= width;
            return static_cast<float> (phase + phase - phase * phase - 1.0);
        }
        if (phase > 1.0 - width)
        {
            phase = (phase - 1.0) / width;
            return static_cast<float> (phase * phase + phase + phase + 1.0);
        }
        return 0.0f;
    }
    // Pre-edge (phase just below 1) and post-edge (phase just above 0) halves of polyBlep.
    static double blepBefore (double phase, double width) noexcept
    {
        return phase > 1.0 - width ? static_cast<double> (polyBlep (phase, width)) : 0.0;
    }
    static double blepAfter (double phase, double width) noexcept
    {
        return phase < width ? static_cast<double> (polyBlep (phase, width)) : 0.0;
    }
    // A step at position `at` in [0, 1] of the interval to the next sample: the pre-edge
    // residual goes on this sample, the post-edge residual waits for the next one.
    void addSyncEdge (double at, double sawStep, double pulseStep) noexcept
    {
        const double before = 0.5 * (1.0 - at) * (1.0 - at);
        const double after = -0.5 * at * at;
        edgeSaw_ += sawStep * before;
        edgePulse_ += pulseStep * before;
        pendingSaw_ += sawStep * after;
        pendingPulse_ += pulseStep * after;
    }
    // Natural edges while the phase runs from `start` for `span` cycles, starting at `at`.
    void addSyncEdges (double start, double span, double at, double dt, double width) noexcept
    {
        if (dt <= 1.0e-9 || span <= 0.0)
            return;
        const double end = start + span;
        const double wrapAt = std::floor (start) + 1.0;
        if (at == 0.0 ? end >= wrapAt : end > wrapAt)
            addSyncEdge (at + (wrapAt - start) / dt, -2.0, 2.0);
        const double fallAt = std::floor (start - width) + 1.0 + width;
        if (at == 0.0 ? end >= fallAt : end > fallAt)
            addSyncEdge (at + (fallAt - start) / dt, 0.0, -2.0);
    }
    double randomUnit () noexcept
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<double> (rng_) / 4294967295.0;
    }
    double sampleRate_ = 48000.0, frequency_ = 440.0, phase_ = 0.0, drift_ = 0.0;
    float driftCents_ = 0.0f, triangle_ = 0.0f, wrapFraction_ = 0.0f;
    bool wrapped_ = false, hasSynced_ = false, syncPending_ = false;
    double predictedPhase_ = 0.0, edgeSaw_ = 0.0, edgePulse_ = 0.0, pendingSaw_ = 0.0, pendingPulse_ = 0.0;
    unsigned driftCount_ = 0;
    std::uint32_t seed_ = 1, rng_ = 1;
};
} // namespace kraftonic::synth
