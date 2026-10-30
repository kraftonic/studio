// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kraftonic::synth
{
namespace detail
{
inline float flush (float x) noexcept
{
    return std::isfinite (x) && std::abs (x) >= 1.0e-30f ? x : 0.0f;
}
inline float finiteOr (float x, float fallback) noexcept
{
    return std::isfinite (x) ? x : fallback;
}
} // namespace detail

class OnePoleLowPass
{
  public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) ? std::clamp (sampleRate, 22050.0, 384000.0) : 48000.0;
        update ();
    }
    void setCutoff (float hz) noexcept
    {
        cutoff_ = detail::finiteOr (hz, cutoff_);
        update ();
    }
    void reset () noexcept
    {
        state_ = 0.0f;
    }
    float process (float input) noexcept
    {
        if (!std::isfinite (input))
        {
            reset ();
            return 0.0f;
        }
        state_ = detail::flush (state_ + coefficient_ * (input - state_));
        return state_;
    }

  private:
    void update () noexcept
    {
        const double frequency = std::clamp (static_cast<double> (cutoff_), 1.0, sampleRate_ * 0.49);
        coefficient_ = static_cast<float> (1.0 - std::exp (-6.283185307179586 * frequency / sampleRate_));
    }
    double sampleRate_ = 48000.0;
    float cutoff_ = 1000.0f, coefficient_ = 0.1f, state_ = 0.0f;
};

class OnePoleHighPass
{
  public:
    void prepare (double sampleRate) noexcept
    {
        lowPass_.prepare (sampleRate);
    }
    void setCutoff (float hz) noexcept
    {
        lowPass_.setCutoff (hz);
    }
    void reset () noexcept
    {
        lowPass_.reset ();
    }
    float process (float input) noexcept
    {
        if (!std::isfinite (input))
        {
            reset ();
            return 0.0f;
        }
        return detail::flush (input - lowPass_.process (input));
    }

  private:
    OnePoleLowPass lowPass_;
};

enum class SVFMode
{
    lowPass,
    highPass,
    bandPass,
    notch
};
class StateVariableFilter
{
  public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) ? std::clamp (sampleRate, 22050.0, 384000.0) : 48000.0;
        updateSmoothingCoefficient ();
        update ();
    }
    void reset () noexcept
    {
        ic1_ = ic2_ = cascadeIc1_ = cascadeIc2_ = 0.0f;
    }
    void setCutoff (float hz) noexcept
    {
        if (std::isfinite (hz))
            targetCutoff_ = std::clamp (hz, 1.0f, static_cast<float> (sampleRate_ * 0.45));
    }
    void setCutoffImmediate (float hz) noexcept
    {
        if (std::isfinite (hz))
        {
            cutoff_ = targetCutoff_ = std::clamp (hz, 1.0f, static_cast<float> (sampleRate_ * 0.45));
            update ();
        }
    }
    float cutoffHz () const noexcept
    {
        return cutoff_;
    }
    void setSmoothingTime (float milliseconds) noexcept
    {
        if (std::isfinite (milliseconds))
        {
            smoothingTimeMs_ = std::max (0.0f, milliseconds);
            updateSmoothingCoefficient ();
        }
    }
    void setResonance (float resonance) noexcept
    {
        resonance_ = std::clamp (detail::finiteOr (resonance, resonance_), 0.0f, 1.0f);
        update ();
    }
    void process (float input, float &low, float &high, float &band, float &notch) noexcept
    {
        if (!std::isfinite (input))
        {
            reset ();
            low = high = band = notch = 0.0f;
            return;
        }
        cutoff_ += (targetCutoff_ - cutoff_) * cutoffSmoothing_;
        update ();
        const float v3 = input - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = detail::flush (2.0f * v1 - ic1_);
        ic2_ = detail::flush (2.0f * v2 - ic2_);
        high = detail::flush (input - k_ * v1 - v2);
        band = detail::flush (v1);
        low = detail::flush (v2);
        notch = detail::flush (high + low);
        if (!std::isfinite (low + high + band + notch))
        {
            reset ();
            low = high = band = notch = 0.0f;
        }
    }
    float process (float input, SVFMode mode, bool twelveDb = true) noexcept
    {
        float low, high, band, notch;
        process (input, low, high, band, notch);
        if (!twelveDb && mode == SVFMode::lowPass)
            low = processCascade (low);
        switch (mode)
        {
        case SVFMode::lowPass:
            return low;
        case SVFMode::highPass:
            return high;
        case SVFMode::bandPass:
            return band;
        case SVFMode::notch:
            return notch;
        }
        return 0.0f;
    }

  private:
    void updateSmoothingCoefficient () noexcept
    {
        cutoffSmoothing_ = smoothingTimeMs_ <= 0.0f
                               ? 1.0f
                               : static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate_ * 0.001 * smoothingTimeMs_)));
    }
    float processCascade (float input) noexcept
    {
        const float v3 = input - cascadeIc2_;
        const float v1 = a1_ * cascadeIc1_ + a2_ * v3;
        const float v2 = cascadeIc2_ + a2_ * cascadeIc1_ + a3_ * v3;
        cascadeIc1_ = detail::flush (2.0f * v1 - cascadeIc1_);
        cascadeIc2_ = detail::flush (2.0f * v2 - cascadeIc2_);
        return detail::flush (v2);
    }
    void update () noexcept
    {
        const float frequency = std::clamp (cutoff_, 1.0f, static_cast<float> (sampleRate_ * 0.45));
        const float g = std::tan (3.14159265359f * frequency / static_cast<float> (sampleRate_));
        k_ = 2.0f - 1.98f * resonance_;
        a1_ = 1.0f / (1.0f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }
    double sampleRate_ = 48000.0;
    float cutoff_ = 1000.0f, targetCutoff_ = 1000.0f, cutoffSmoothing_ = 0.0f, smoothingTimeMs_ = 1.0f;
    float resonance_ = 0.2f, k_ = 1.6f;
    float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1_ = 0.0f, ic2_ = 0.0f, cascadeIc1_ = 0.0f, cascadeIc2_ = 0.0f;
};

class LadderFilter
{
  public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) ? std::clamp (sampleRate, 22050.0, 384000.0) : 48000.0;
        updateSmoothingCoefficient ();
        update ();
    }
    void reset () noexcept
    {
        states_.fill (0.0f);
    }
    void setCutoff (float hz) noexcept
    {
        if (std::isfinite (hz))
            targetCutoff_ = std::clamp (hz, 1.0f, static_cast<float> (sampleRate_ * 0.45));
    }
    void setCutoffImmediate (float hz) noexcept
    {
        if (std::isfinite (hz))
        {
            cutoff_ = targetCutoff_ = std::clamp (hz, 1.0f, static_cast<float> (sampleRate_ * 0.45));
            update ();
        }
    }
    float cutoffHz () const noexcept
    {
        return cutoff_;
    }
    void setSmoothingTime (float milliseconds) noexcept
    {
        if (std::isfinite (milliseconds))
        {
            smoothingTimeMs_ = std::max (0.0f, milliseconds);
            updateSmoothingCoefficient ();
        }
    }
    void setResonance (float resonance) noexcept
    {
        if (!std::isfinite (resonance))
            return;
        resonance_ = std::clamp (resonance, 0.0f, 1.0f);
        // Up to 0.9 the feedback is 4 * resonance, as before. Above it the feedback passes the
        // self-oscillation threshold (4, near resonance 0.909) and reaches 8 at 1. Meanwhile the
        // stages fade to linear by 0.91, so the feedback tanh alone limits the oscillation: its
        // level (about 0.29 peak at 1) and pitch then do not depend on cutoff or sample rate.
        const float above = std::max (0.0f, resonance_ - 0.9f) * 10.0f;
        feedbackGain_ = resonance_ <= 0.9f ? resonance_ * 4.0f : 3.6f + 4.4f * above;
        stageLinearity_ = std::min (1.0f, above * 10.0f);
    }
    void setDrive (float drive) noexcept
    {
        if (std::isfinite (drive))
            drive_ = std::clamp (drive, 0.0f, 16.0f);
    }
    void setGainCompensation (bool enabled) noexcept
    {
        compensation_ = enabled;
    }
    float process (float input) noexcept
    {
        if (!std::isfinite (input))
        {
            reset ();
            return 0.0f;
        }
        cutoff_ += (targetCutoff_ - cutoff_) * cutoffSmoothing_;
        update ();

        float drivenInput = std::tanh (input * drive_);
        const float feedbackGain = feedbackGain_;
        // Past the threshold, start the oscillation from silence with a -100 dBFS nudge.
        if (feedbackGain > 4.0f && std::abs (states_[0]) + std::abs (states_[1]) + std::abs (states_[2]) +
                                           std::abs (states_[3]) <
                                       1.0e-9f)
            drivenInput += 1.0e-5f;
        float firstStageInput = drivenInput;
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            float stageOutput = firstStageInput;
            float stageDerivative = 1.0f;
            for (const float state : states_)
            {
                const float linear = a1_ * state + a2_ * stageOutput;
                stageOutput = std::tanh (linear);
                float slope = 1.0f - stageOutput * stageOutput;
                if (stageLinearity_ > 0.0f)
                {
                    stageOutput += stageLinearity_ * (linear - stageOutput);
                    slope += stageLinearity_ * (1.0f - slope);
                }
                stageDerivative *= slope * a2_;
            }
            const float feedbackInput = drivenInput - feedbackGain * stageOutput;
            const float feedback = std::tanh (feedbackInput);
            const float residual = firstStageInput - feedback;
            const float derivative = 1.0f + feedbackGain * (1.0f - feedback * feedback) * stageDerivative;
            firstStageInput -= residual / std::max (derivative, 0.05f);
            firstStageInput = std::clamp (firstStageInput, -4.0f, 4.0f);
        }

        float output = firstStageInput;
        for (auto &state : states_)
        {
            const float linear = a1_ * state + a2_ * output;
            output = std::tanh (linear);
            if (stageLinearity_ > 0.0f)
                output += stageLinearity_ * (linear - output);
            state = detail::flush (2.0f * output - state);
        }
        const float compensation = compensation_ ? 1.0f + 0.25f * resonance_ : 1.0f;
        return detail::flush (output * compensation);
    }

  private:
    void updateSmoothingCoefficient () noexcept
    {
        cutoffSmoothing_ = smoothingTimeMs_ <= 0.0f
                               ? 1.0f
                               : static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate_ * 0.001 * smoothingTimeMs_)));
    }
    void update () noexcept
    {
        const float frequency = std::clamp (cutoff_, 1.0f, static_cast<float> (sampleRate_ * 0.45));
        const float g = std::tan (3.14159265359f * frequency / static_cast<float> (sampleRate_));
        a1_ = 1.0f / (1.0f + g);
        a2_ = g * a1_;
    }
    double sampleRate_ = 48000.0;
    float targetCutoff_ = 1000.0f, cutoff_ = 1000.0f, cutoffSmoothing_ = 0.0f, smoothingTimeMs_ = 1.0f;
    float resonance_ = 0.0f, feedbackGain_ = 0.0f, stageLinearity_ = 0.0f, drive_ = 1.0f, a1_ = 0.9f, a2_ = 0.1f;
    bool compensation_ = false;
    std::array<float, 4> states_{};
};
} // namespace kraftonic::synth
