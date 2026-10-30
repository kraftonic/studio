// SPDX-License-Identifier: MIT
#pragma once
#include "kraftonic/fx/Detail.h"
#include "kraftonic/fx/Effect.h"

namespace kraftonic::fx
{
class Chorus
{
public:
    struct Parameters
    {
        bool enabled = true;
        int taps = 2;
        float rateHz = 0.8f;
        float depth = 0.6f;
        float mix = 0.35f;
    };
    void prepare (const ProcessSetup& setup);
    void reset() noexcept;
    void setParameters (const Parameters& value) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    int latencySamples() const noexcept { return 0; }

private:
    detail::Ring lines[2];
    detail::Mix mix;
    core::LinearSmoother rateHz, depth, tapCount;
    float phase = 0.0f, rate = 48000.0f;
    bool initialized = false;
};
} // namespace kraftonic::fx
