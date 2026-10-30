// SPDX-License-Identifier: MIT
#pragma once
#include "kraftonic/fx/Detail.h"
#include "kraftonic/fx/Effect.h"

namespace kraftonic::fx
{
class Drive
{
public:
    struct Parameters
    {
        bool enabled = true;
        float drive = 0.35f;
        float tone = 0.5f;
        float outputDb = 0.0f;
        float mix = 1.0f;
    };

    void prepare (const ProcessSetup& setup) noexcept;
    void reset() noexcept;
    void setParameters (const Parameters& value) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    int latencySamples() const noexcept { return 0; }

private:
    static float adaa (float input, float& previous, float gain) noexcept;
    detail::Mix mix;
    core::LinearSmoother drive, tone, output;
    float previous[2] {}, low[2] {};
    float rate = 48000.0f;
    bool initialized = false;
};
} // namespace kraftonic::fx
