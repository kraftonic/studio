// SPDX-License-Identifier: MIT
#pragma once
#include "kraftonic/fx/Detail.h"
#include "kraftonic/fx/Effect.h"

namespace kraftonic::fx
{
class Phaser
{
public:
    struct Parameters
    {
        bool enabled = true;
        int stages = 6;
        float rateHz = 0.35f;
        float depth = 0.75f;
        float feedback = 0.3f;
        float stereoPhase = 0.5f;
        float mix = 0.5f;
    };
    void prepare (const ProcessSetup& setup) noexcept;
    void reset() noexcept;
    void setParameters (const Parameters& value) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    int latencySamples() const noexcept { return 0; }

private:
    detail::Mix mix;
    core::LinearSmoother rateHz, depth, feedback, stereoPhase, stageCount;
    float z[2][8] {}, last[2] {}, phase = 0.0f, rate = 48000.0f;
    bool initialized = false;
};
} // namespace kraftonic::fx
