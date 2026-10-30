// SPDX-License-Identifier: MIT
//
// Self-test sine engine implementation.

#include "SelfTestEngine.h"

#include <cmath>

namespace kraftonic::formats::selftest
{

namespace
{

double noteFreq (int note) noexcept
{
    return 440.0 * std::pow (2.0, (static_cast<double> (note) - 69.0) / 12.0);
}

float dbToLin (float db) noexcept
{
    return std::pow (10.0F, db / 20.0F);
}

float oscSample (int wave, double phase) noexcept
{
    const double s = std::sin (2.0 * 3.141592653589793 * phase);
    if (wave == 1)
        return static_cast<float> (std::tanh (2.0 * s));
    if (wave == 2)
    {
        const double tri = 2.0 * std::fabs (2.0 * (phase - std::floor (phase)) - 1.0) - 1.0;
        return static_cast<float> (tri * 0.8);
    }
    return static_cast<float> (s);
}

} // namespace

void SelfTestEngine::prepare (const ProcessSetup& setup)
{
    double rate = setup.sampleRate;
    if (rate < 22050.0)
        rate = 22050.0;
    if (rate > 384000.0)
        rate = 384000.0;
    sampleRate_ = rate;
    noteVoice_.fill (-1);
    reset();
    initialised_ = true;
}

void SelfTestEngine::reset() noexcept
{
    for (auto& voice : voices_)
        voice = Voice {};
    noteVoice_.fill (-1);
    pedalDown_ = false;
    if (initialised_)
        gainLin_ = gainTarget_;
}

void SelfTestEngine::setParameters (const SelfTestParameters& params) noexcept
{
    gainTarget_ = dbToLin (params.get (ParamId::outputGain));
    attackSecs_ = params.get (ParamId::attack);
    if (attackSecs_ < 0.001F)
        attackSecs_ = 0.001F;
    int wave = static_cast<int> (params.get (ParamId::wave) + 0.5F);
    if (wave < 0)
        wave = 0;
    if (wave > 2)
        wave = 2;
    wave_ = wave;
}

void SelfTestEngine::noteOn (int note, float velocity) noexcept
{
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    // Retrigger the same note's voice when it is already held.
    int slot = noteVoice_[static_cast<std::size_t> (note)];
    if (slot < 0)
    {
        slot = 0;
        for (int i = 0; i < kVoices; ++i)
        {
            if (!voices_[static_cast<std::size_t> (i)].active)
            {
                slot = i;
                break;
            }
            slot = i; // all busy: steal the lowest index (bounded, simple)
        }
        // Free the stolen voice's old note mapping.
        for (std::size_t n = 0; n < noteVoice_.size(); ++n)
        {
            if (noteVoice_[n] == slot)
                noteVoice_[n] = -1;
        }
    }
    Voice& voice = voices_[static_cast<std::size_t> (slot)];
    voice.active = true;
    voice.sustained = false;
    voice.releasing = false;
    voice.note = note;
    voice.phase = 0.0;
    voice.freq = noteFreq (note);
    voice.velocity = velocity;
    voice.env = 0.0F;
    noteVoice_[static_cast<std::size_t> (note)] = slot;
}

void SelfTestEngine::noteOff (int note) noexcept
{
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    const int slot = noteVoice_[static_cast<std::size_t> (note)];
    if (slot < 0)
        return;
    Voice& voice = voices_[static_cast<std::size_t> (slot)];
    if (pedalDown_)
    {
        voice.sustained = true;
        return;
    }
    voice.releasing = true;
    voice.sustained = false;
    noteVoice_[static_cast<std::size_t> (note)] = -1;
}

void SelfTestEngine::process (
    const AudioBlock& block, const Event* events, int numEvents) noexcept
{
    if (block.left == nullptr || block.numSamples <= 0)
        return;
    float* left = block.left;
    float* right = block.right != nullptr ? block.right : block.left;
    const int n = block.numSamples;

    // Gain smoothing coefficient: ~5 ms towards the target.
    const float coeff = static_cast<float> (
        std::exp (-1.0 / (0.005 * sampleRate_)));
    const float attackStep = static_cast<float> (1.0 / (attackSecs_ * sampleRate_));
    const float releaseStep = static_cast<float> (1.0 / (kReleaseSeconds * sampleRate_));

    int nextEvent = 0;
    for (int s = 0; s < n; ++s)
    {
        while (events != nullptr && nextEvent < numEvents
               && events[nextEvent].sampleOffset <= s)
        {
            const Event& e = events[nextEvent++];
            switch (e.type)
            {
                case Event::Type::noteOn:
                    noteOn (e.note, e.value > 0.0F ? e.value : 0.8F);
                    break;
                case Event::Type::noteOff:
                    noteOff (e.note);
                    break;
                case Event::Type::sustainPedal:
                {
                    const bool down = e.value >= 0.5F;
                    if (pedalDown_ && !down)
                    {
                        for (auto& voice : voices_)
                        {
                            if (voice.active && voice.sustained)
                            {
                                voice.sustained = false;
                                voice.releasing = true;
                            }
                        }
                    }
                    pedalDown_ = down;
                    break;
                }
                case Event::Type::allNotesOff:
                    pedalDown_ = false;
                    for (std::size_t v = 0; v < voices_.size(); ++v)
                    {
                        if (voices_[v].active)
                        {
                            voices_[v].releasing = true;
                            voices_[v].sustained = false;
                        }
                    }
                    noteVoice_.fill (-1);
                    break;
                case Event::Type::modWheel:
                case Event::Type::pitchBend:
                case Event::Type::polyPressure:
                case Event::Type::channelPressure:
                    break; // not used by this engine yet
                case Event::Type::allSoundOff:
                    pedalDown_ = false;
                    for (auto& voice : voices_)
                        voice = Voice {};
                    noteVoice_.fill (-1);
                    break;
            }
        }

        gainLin_ += (gainTarget_ - gainLin_) * (1.0F - coeff);
        float mix = 0.0F;
        for (auto& voice : voices_)
        {
            if (!voice.active)
                continue;
            if (!voice.releasing && voice.env < 1.0F)
            {
                voice.env += attackStep;
                if (voice.env >= 1.0F)
                    voice.env = 1.0F;
            }
            else if (voice.releasing)
            {
                voice.env -= releaseStep;
                if (voice.env <= 0.0F)
                {
                    voice = Voice {};
                    continue;
                }
            }
            voice.phase += voice.freq / sampleRate_;
            if (voice.phase >= 1.0)
                voice.phase -= 1.0;
            mix += oscSample (wave_, voice.phase) * voice.env * voice.velocity;
        }
        // 8 voices x velocity 1 peak = 8; scale down, then soft-limit so
        // output is always finite and normal playing stays under 0 dBFS.
        const float shaped
            = static_cast<float> (std::tanh (static_cast<double> (mix * 0.25F * gainLin_)));
        const float out = shaped * 0.9F;
        left[s] = out;
        right[s] = out;
    }
}

int SelfTestEngine::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& voice : voices_)
    {
        if (voice.active)
            ++count;
    }
    return count;
}

} // namespace kraftonic::formats::selftest
