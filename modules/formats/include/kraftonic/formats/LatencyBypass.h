// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Latency-compensated, click-free effect bypass.
//
//  SDK-free. The dry signal always runs through a delay equal to the
//  engine's reported latency, so the bypassed output lines up in time with
//  the processed output and toggling bypass never shifts the audio. Switching
//  crossfades linearly between wet and delayed dry over kFadeSeconds.
//
//  Real-time: prepare() allocates; reset/setBypassed/process never do.
// ============================================================================

#include <cstddef>
#include <vector>

namespace kraftonic::formats
{

class LatencyBypass
{
public:
    static constexpr double kFadeSeconds = 0.010;

    /** Not real-time. `latency` >= 0 samples. */
    void prepare (double sampleRate, int latency)
    {
        latency_ = latency > 0 ? latency : 0;
        // Ring sized to a power of two above the latency (read == write - latency).
        std::size_t size = 1;
        while (size < static_cast<std::size_t> (latency_) + 1)
            size <<= 1;
        ringLeft_.assign (size, 0.0F);
        ringRight_.assign (size, 0.0F);
        mask_ = size - 1;
        writePos_ = 0;
        const double fade = sampleRate * kFadeSeconds;
        step_ = fade >= 1.0 ? static_cast<float> (1.0 / fade) : 1.0F;
    }

    void reset() noexcept
    {
        for (auto& s : ringLeft_)
            s = 0.0F;
        for (auto& s : ringRight_)
            s = 0.0F;
        writePos_ = 0;
        mix_ = target_;
        hold_ = 0;
    }

    /** Sets the bypass target; the crossfade runs inside process(). */
    void setBypassed (bool bypassed) noexcept { target_ = bypassed ? 1.0F : 0.0F; }

    /** Jumps straight to the target (used when processing (re)starts). */
    void snapToTarget() noexcept
    {
        mix_ = target_;
        hold_ = 0;
    }

    /**
     * Freezes the mix for `samples` before any fade moves. Call when the
     * engine restarts from reset (e.g. leaving full bypass): its latency
     * line is empty until then, so fading in earlier would let the wet
     * signal pop in mid-fade.
     */
    void holdMix (int samples) noexcept { hold_ = samples > 0 ? samples : 0; }

    bool isBypassTarget() const noexcept { return target_ >= 0.5F; }
    /** True when output is 100 % delayed dry: the engine need not run. */
    bool fullyBypassed() const noexcept { return mix_ >= 1.0F && target_ >= 1.0F; }
    /** True when output is 100 % wet (no fade in progress). */
    bool fullyActive() const noexcept { return mix_ <= 0.0F && target_ <= 0.0F; }
    int latency() const noexcept { return latency_; }

    /**
     * dry*: the undelayed input for this run of samples. out*: the engine's
     * (wet) output on entry, the final output on exit. Any pointer pair may be
     * the same buffer for mono; dry and out must not alias.
     */
    void process (const float* dryLeft, const float* dryRight, float* outLeft, float* outRight,
                  int numSamples) noexcept
    {
        if (ringLeft_.empty())
            return; // not prepared: leave the wet signal untouched
        for (int i = 0; i < numSamples; ++i)
        {
            ringLeft_[writePos_] = dryLeft[i];
            ringRight_[writePos_] = dryRight[i];
            const std::size_t readPos
                = (writePos_ + ringLeft_.size() - static_cast<std::size_t> (latency_)) & mask_;
            const float delayedLeft = ringLeft_[readPos];
            const float delayedRight = ringRight_[readPos];
            writePos_ = (writePos_ + 1) & mask_;

            if (hold_ > 0)
                --hold_;
            else if (mix_ < target_)
                mix_ = mix_ + step_ < target_ ? mix_ + step_ : target_;
            else if (mix_ > target_)
                mix_ = mix_ - step_ > target_ ? mix_ - step_ : target_;

            if (mix_ >= 1.0F)
            {
                outLeft[i] = delayedLeft;
                outRight[i] = delayedRight;
            }
            else if (mix_ > 0.0F)
            {
                const float wet = 1.0F - mix_;
                outLeft[i] = outLeft[i] * wet + delayedLeft * mix_;
                outRight[i] = outRight[i] * wet + delayedRight * mix_;
            }
        }
    }

private:
    std::vector<float> ringLeft_;
    std::vector<float> ringRight_;
    std::size_t mask_ = 0;
    std::size_t writePos_ = 0;
    int latency_ = 0;
    float mix_ = 0.0F;    // 0 = wet, 1 = delayed dry
    float target_ = 0.0F;
    float step_ = 1.0F;
    int hold_ = 0;
};

} // namespace kraftonic::formats
