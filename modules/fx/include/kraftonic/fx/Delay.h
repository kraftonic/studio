// SPDX-License-Identifier: MIT
#pragma once
#include "kraftonic/fx/Detail.h"
#include "kraftonic/fx/Effect.h"

namespace kraftonic::fx
{
class Delay
{
public:
    struct Parameters
    {
        bool enabled = true;
        bool pingPong = false;
        bool tempoSync = false;
        float timeMs = 375.0f;
        float noteDivision = 0.5f; // quarter note = 1, eighth note = 0.5
        float feedback = 0.35f;
        float lowCutHz = 30.0f;
        float highCutHz = 9000.0f;
        float mix = 0.25f;
    };
    void prepare (const ProcessSetup& setup);
    void reset() noexcept;
    void setParameters (const Parameters& value) noexcept;
    void setTempo (double bpm) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    int latencySamples() const noexcept { return 0; }

private:
    void updateTime() noexcept;
    detail::Ring lines[2];
    detail::Mix mix;
    core::LinearSmoother time, feedback, lowCut, highCut;
    Parameters params;
    float low[2] {}, highState[2] {}, rate = 48000.0f;
    double tempo = 120.0;
    bool initialized = false;
};
} // namespace kraftonic::fx
