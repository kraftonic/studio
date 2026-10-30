// SPDX-License-Identifier: MIT
#include "StarterEffectEngine.h"

#include <cmath>

namespace startereffect
{
void StarterEffectEngine::prepare (const ProcessSetup& setup)
{
    drive_.prepare (setup);
    delay_.prepare (setup); // allocates two seconds of delay line per channel
    gain_.prepare (setup.sampleRate, 0.02);
    reset();
}

void StarterEffectEngine::reset() noexcept
{
    drive_.reset();
    delay_.reset(); // clears the echoes
    snapGain_ = true;
}

void StarterEffectEngine::setParameters (const StarterEffectParameters& params) noexcept
{
    // Drive at zero switches the stage off: the shared effects crossfade
    // on/off over 5 ms, and a disabled effect is bit-exact pass-through.
    fx::Drive::Parameters drive;
    drive.enabled = params.get (ParamId::drive) > 0.0F;
    drive.drive = params.get (ParamId::drive);
    drive.tone = params.get (ParamId::tone);
    drive.mix = 1.0F;
    drive_.setParameters (drive);

    fx::Delay::Parameters delay;
    delay.timeMs = params.get (ParamId::delayTime);
    delay.feedback = params.get (ParamId::feedback);
    delay.highCutHz = params.get (ParamId::damping); // darker repeats as it goes down
    delay.lowCutHz = 40.0F;                          // keeps low rumble from building up
    delay.pingPong = params.get (ParamId::pingPong) >= 0.5F;
    delay.mix = params.get (ParamId::mix);
    delay_.setParameters (delay);

    // The shared effects smooth their own controls; the output level is ours.
    gain_.setTarget (std::pow (10.0F, params.get (ParamId::outputGain) / 20.0F));
    if (snapGain_)
    {
        gain_.setImmediate (gain_.getTarget());
        snapGain_ = false;
    }
}

void StarterEffectEngine::process (const AudioBlock& block) noexcept
{
    drive_.process (block.left, block.right, block.numSamples);
    delay_.process (block.left, block.right, block.numSamples);
    for (int i = 0; i < block.numSamples; ++i)
    {
        const float gain = gain_.next();
        block.left[i] *= gain;
        if (block.right != block.left)
            block.right[i] *= gain;
    }
}
} // namespace startereffect
