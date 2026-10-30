// SPDX-License-Identifier: MIT
#pragma once

namespace kraftonic::core
{
/** Analog-style ADSR envelope with exponential segments.

    - Attack approaches an overshoot target, giving the familiar convex curve.
    - Decay and release are exponential.
    - Retriggering starts the attack from the current level, so re-used or
      stolen voices never jump in amplitude.
    - Sustain changes are smoothed while sustaining.

    Real-time safe: no allocation, no locks.
*/
class Adsr
{
public:
    struct Parameters
    {
        float attackSeconds = 0.005f;
        float decaySeconds = 0.2f;
        float sustainLevel = 0.8f; // 0..1
        float releaseSeconds = 0.3f;
    };

    enum class Stage
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    void prepare (double sampleRate) noexcept;
    void setParameters (const Parameters& p) noexcept;

    void noteOn() noexcept;
    void noteOff() noexcept;
    /** Hard reset to silence. Only use when the output is not audible. */
    void reset() noexcept;

    float process() noexcept;

    Stage getStage() const noexcept { return stage; }
    bool isActive() const noexcept { return stage != Stage::idle; }
    float getLevel() const noexcept { return level; }

    // Minimum segment times that keep transitions click-free.
    static constexpr float kMinAttackSeconds = 0.001f;
    static constexpr float kMinDecaySeconds = 0.005f;
    static constexpr float kMinReleaseSeconds = 0.005f;

private:
    void updateCoefficients() noexcept;

    double sampleRate = 44100.0;
    Parameters params;
    Stage stage = Stage::idle;
    float level = 0.0f;

    float attackCoef = 0.0f, attackBase = 0.0f;
    float decayCoef = 0.0f, decayBase = 0.0f;
    float releaseCoef = 0.0f, releaseBase = 0.0f;
    float sustainSmoothCoef = 0.0f;
};
} // namespace kraftonic::core
