// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace kraftonic::synth
{
class Noise
{
  public:
    void reset (std::uint32_t seed = 0x1234567u) noexcept
    {
        rng_ = seed ? seed : 1;
        b0_ = b1_ = b2_ = b3_ = b4_ = b5_ = b6_ = 0.0f;
    }
    float white () noexcept
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float> (rng_) * (2.0f / 4294967295.0f) - 1.0f;
    }
    float pink () noexcept
    {
        const float w = white ();
        b0_ = 0.99886f * b0_ + w * 0.0555179f;
        b1_ = 0.99332f * b1_ + w * 0.0750759f;
        b2_ = 0.96900f * b2_ + w * 0.1538520f;
        b3_ = 0.86650f * b3_ + w * 0.3104856f;
        b4_ = 0.55000f * b4_ + w * 0.5329522f;
        b5_ = -0.7616f * b5_ - w * 0.0168980f;
        const float y = (b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + w * 0.5362f) * 0.11f;
        b6_ = w * 0.115926f;
        return y;
    }

  private:
    std::uint32_t rng_ = 1;
    float b0_ = 0, b1_ = 0, b2_ = 0, b3_ = 0, b4_ = 0, b5_ = 0, b6_ = 0;
};
} // namespace kraftonic::synth
