// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace kraftonic::synth
{
enum class LfoWave
{
    triangle,
    sine,
    sawUp,
    sawDown,
    square,
    sampleHold
};
class Lfo
{
  public:
    void prepare (double sr) noexcept
    {
        sr_ = std::isfinite (sr) ? std::clamp (sr, 1.0, 384000.0) : 48000.0;
        reset ();
    }
    void reset () noexcept
    {
        phase_ = 0;
        elapsed_ = 0;
        fade_ = 0;
        held_ = 0;
    }
    void setRateHz (float h) noexcept
    {
        hz_ = std::isfinite (h) ? std::clamp (h, 0.0f, static_cast<float> (sr_ * 0.45)) : 0.0f;
        synced_ = false;
    }
    void setTempoSync (float beats, float bpm) noexcept
    {
        if (!std::isfinite (beats) || !std::isfinite (bpm))
        {
            hz_ = 0.0f;
        }
        else
        {
            hz_ = std::clamp (std::max (0.0f, bpm) / (60.0f * std::max (0.001f, beats)), 0.0f,
                              static_cast<float> (sr_ * 0.45));
        }
        synced_ = true;
    }
    void setDelay (float seconds) noexcept
    {
        delay_ = std::isfinite (seconds) ? std::max (0.0f, seconds) : 0.0f;
    }
    void setFade (float seconds) noexcept
    {
        fadeTime_ = std::isfinite (seconds) ? std::max (0.0f, seconds) : 0.0f;
    }
    void setKeySync (bool b) noexcept
    {
        keySync_ = b;
    }
    bool keySync () const noexcept
    {
        return keySync_;
    }
    void noteOn () noexcept
    {
        if (keySync_)
            reset ();
    }
    float process (LfoWave w) noexcept
    {
        elapsed_ += 1.0 / sr_;
        if (elapsed_ <= delay_)
            return 0;
        fade_ = fadeTime_ <= 0 ? 1.0f : std::min (1.0f, fade_ + static_cast<float> (1.0 / (sr_ * fadeTime_)));
        const float old = static_cast<float> (phase_);
        phase_ += hz_ / sr_;
        bool wrap = phase_ >= 1.0 - 1.0e-12;
        if (wrap)
            phase_ -= std::floor (phase_);
        if (wrap && w == LfoWave::sampleHold)
            held_ = random ();
        float y = 0;
        switch (w)
        {
        case LfoWave::triangle:
            y = 1.0f - 4.0f * std::abs (static_cast<float> (phase_) - 0.5f);
            break;
        case LfoWave::sine:
            y = std::sin (6.2831853f * static_cast<float> (phase_));
            break;
        case LfoWave::sawUp:
            y = 2 * static_cast<float> (phase_) - 1;
            break;
        case LfoWave::sawDown:
            y = 1 - 2 * static_cast<float> (phase_);
            break;
        case LfoWave::square:
            y = phase_ < 0.5 ? 1.0f : -1.0f;
            break;
        case LfoWave::sampleHold:
            y = held_;
            break;
        }
        (void)old;
        return y * fade_;
    }

  private:
    float random () noexcept
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float> (rng_) * (2.0f / 4294967295.0f) - 1;
    }
    double sr_ = 48000, phase_ = 0, elapsed_ = 0;
    float hz_ = 1, delay_ = 0, fadeTime_ = 0, fade_ = 0, held_ = 0;
    bool synced_ = false, keySync_ = true;
    std::uint32_t rng_ = 0x51f15e;
};
enum class GlideMode
{
    constantTime,
    constantRate
};
class Glide
{
  public:
    void prepare (double sr) noexcept
    {
        sr_ = std::isfinite (sr) ? std::clamp (sr, 1.0, 384000.0) : 48000.0;
    }
    void reset (float value = 0) noexcept
    {
        value_ = target_ = (std::isfinite (value) && std::abs (value) >= 1.0e-30f) ? value : 0.0f;
    }
    void setMode (GlideMode m) noexcept
    {
        mode_ = m;
    }
    void setTime (float seconds) noexcept
    {
        time_ = std::isfinite (seconds) ? std::max (0.0f, seconds) : 0.0f;
    }
    void setRate (float unitsPerSecond) noexcept
    {
        rate_ = std::isfinite (unitsPerSecond) ? std::max (0.0f, unitsPerSecond) : 0.0f;
    }
    void setTarget (float v) noexcept
    {
        target_ = (std::isfinite (v) && std::abs (v) >= 1.0e-30f) ? v : value_;
        step_ = mode_ == GlideMode::constantTime
                    ? std::abs (target_ - value_) / std::max (1.0f, static_cast<float> (time_ * sr_))
                    : rate_ / static_cast<float> (sr_);
    }
    float process () noexcept
    {
        const float step = mode_ == GlideMode::constantTime ? step_ : rate_ / static_cast<float> (sr_);
        if (step <= 0 || std::abs (target_ - value_) <= step)
            value_ = target_;
        else
            value_ += std::copysign (step, target_ - value_);
        if (std::abs (value_) < 1.0e-30f)
            value_ = 0.0f;
        return value_;
    }
    float value () const noexcept
    {
        return value_;
    }

  private:
    double sr_ = 48000;
    float value_ = 0, target_ = 0, time_ = 0, rate_ = 0, step_ = 0;
    GlideMode mode_ = GlideMode::constantTime;
};
} // namespace kraftonic::synth
