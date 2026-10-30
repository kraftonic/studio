// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace kraftonic::wavetable::detail
{
/** Small deterministic xorshift generator (real-time safe). */
class Random
{
public:
    explicit Random (std::uint32_t seed = 0x9e3779b9U) noexcept { setSeed (seed); }

    void setSeed (std::uint32_t seed) noexcept { state = seed != 0 ? seed : 0x9e3779b9U; }

    std::uint32_t nextInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [0, 1). */
    float nextUnit() noexcept { return static_cast<float> (nextInt() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return 2.0f * nextUnit() - 1.0f; }

private:
    std::uint32_t state = 0x9e3779b9U;
};
} // namespace kraftonic::wavetable::detail
