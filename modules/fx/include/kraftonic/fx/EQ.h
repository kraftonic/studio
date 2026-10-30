// SPDX-License-Identifier: MIT
#pragma once
#include "kraftonic/fx/Detail.h"
#include "kraftonic/fx/Effect.h"

namespace kraftonic::fx
{
class EQ
{
public:
    struct Parameters
    {
        bool enabled = true;
        float lowFrequencyHz = 180.0f;
        float lowGainDb = 0.0f;
        float midFrequencyHz = 1200.0f;
        float midQ = 0.8f;
        float midGainDb = 0.0f;
        float highFrequencyHz = 6000.0f;
        float highGainDb = 0.0f;
        float outputDb = 0.0f;
    };
    void prepare (const ProcessSetup& setup) noexcept;
    void reset() noexcept;
    void setParameters (const Parameters& value) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    int latencySamples() const noexcept { return 0; }

private:
    struct Coeff { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
    struct State { float z1 = 0.0f, z2 = 0.0f; };
    static Coeff design (int type, float frequency, float gainDb, float q, float rate) noexcept;
    static float tick (float x, const Coeff& c, State& s) noexcept;
    Coeff current[3], target[3], step[3];
    State states[2][3];
    detail::Mix mix;
    core::LinearSmoother output;
    float rate = 48000.0f;
    int remaining = 0, ramp = 1;
    bool initialized = false;
};
} // namespace kraftonic::fx
