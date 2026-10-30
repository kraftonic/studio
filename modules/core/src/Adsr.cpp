// SPDX-License-Identifier: MIT
#include "kraftonic/core/Adsr.h"

#include <algorithm>
#include <cmath>

namespace kraftonic::core
{
namespace
{
    // How far past the target each curve aims. Smaller ratio = more exponential.
    constexpr double kAttackTargetRatio = 0.3;
    constexpr double kDecayReleaseTargetRatio = 0.0001;

    // Coefficient so that a one-pole curve aimed `ratio` past its target
    // covers the full segment in `seconds`.
    double curveCoefficient (double seconds, double sampleRate, double ratio)
    {
        const double samples = std::max (1.0, seconds * sampleRate);
        return std::exp (-std::log ((1.0 + ratio) / ratio) / samples);
    }
} // namespace

void Adsr::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    // ~5 ms smoothing for sustain-level changes.
    sustainSmoothCoef = static_cast<float> (std::exp (-1.0 / (0.005 * sampleRate)));
    updateCoefficients();
    reset();
}

void Adsr::setParameters (const Parameters& p) noexcept
{
    params.attackSeconds = std::max (p.attackSeconds, kMinAttackSeconds);
    params.decaySeconds = std::max (p.decaySeconds, kMinDecaySeconds);
    params.sustainLevel = std::clamp (p.sustainLevel, 0.0f, 1.0f);
    params.releaseSeconds = std::max (p.releaseSeconds, kMinReleaseSeconds);
    updateCoefficients();
}

void Adsr::updateCoefficients() noexcept
{
    const double a = curveCoefficient (params.attackSeconds, sampleRate, kAttackTargetRatio);
    const double d = curveCoefficient (params.decaySeconds, sampleRate, kDecayReleaseTargetRatio);
    const double r = curveCoefficient (params.releaseSeconds, sampleRate, kDecayReleaseTargetRatio);
    const double s = params.sustainLevel;

    attackCoef = static_cast<float> (a);
    attackBase = static_cast<float> ((1.0 + kAttackTargetRatio) * (1.0 - a));
    decayCoef = static_cast<float> (d);
    decayBase = static_cast<float> ((s - kDecayReleaseTargetRatio) * (1.0 - d));
    releaseCoef = static_cast<float> (r);
    releaseBase = static_cast<float> (-kDecayReleaseTargetRatio * (1.0 - r));
}

void Adsr::noteOn() noexcept
{
    stage = Stage::attack;
}

void Adsr::noteOff() noexcept
{
    if (stage != Stage::idle)
        stage = Stage::release;
}

void Adsr::reset() noexcept
{
    stage = Stage::idle;
    level = 0.0f;
}

float Adsr::process() noexcept
{
    switch (stage)
    {
        case Stage::idle:
            break;

        case Stage::attack:
            level = attackBase + level * attackCoef;
            if (level >= 1.0f)
            {
                level = 1.0f;
                stage = Stage::decay;
            }
            break;

        case Stage::decay:
            level = decayBase + level * decayCoef;
            if (level <= params.sustainLevel)
            {
                level = params.sustainLevel;
                stage = Stage::sustain;
            }
            break;

        case Stage::sustain:
            level = params.sustainLevel + (level - params.sustainLevel) * sustainSmoothCoef;
            break;

        case Stage::release:
            level = releaseBase + level * releaseCoef;
            if (level <= 0.0f)
            {
                level = 0.0f;
                stage = Stage::idle;
            }
            break;
    }

    return level;
}
} // namespace kraftonic::core
