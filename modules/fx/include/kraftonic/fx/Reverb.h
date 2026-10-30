// SPDX-License-Identifier: MIT
#pragma once
#include "kraftonic/fx/Detail.h"
#include "kraftonic/fx/Effect.h"

namespace kraftonic::fx
{
class Reverb
{
public:
    struct Parameters
    {
        bool enabled = true;
        float preDelayMs = 18.0f;
        float size = 0.55f;
        float decaySeconds = 2.4f;
        float damping = 0.45f;
        float width = 0.8f;
        float mix = 0.22f;
    };
    void prepare (const ProcessSetup& setup);
    void reset() noexcept;
    void setParameters (const Parameters& value) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    int latencySamples() const noexcept { return 0; }

private:
    detail::Ring pre[2], lines[8];
    detail::Mix mix;
    core::LinearSmoother preDelay, size, decay, damping, width;
    float filter[8] {}, phases[8] {}, rate = 48000.0f;
    bool initialized = false;
};
} // namespace kraftonic::fx
