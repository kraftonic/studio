// SPDX-License-Identifier: MIT
#include "StarterSynthEngine.h"

#include <kraftonic/core/StereoPeakProtector.h>

#include <algorithm>
#include <cmath>

namespace startersynth
{
namespace
{
constexpr double kSmoothingSeconds = 0.02;

/** The SVF self-oscillates near resonance 1; 0.85 keeps a strong but tame peak. */
constexpr float kMaxFilterResonance = 0.85F;

synth::Waveform toWaveform (float step) noexcept
{
    switch (static_cast<Wave> (static_cast<int> (step + 0.5F)))
    {
        case Wave::square: return synth::Waveform::pulse;
        case Wave::sine: return synth::Waveform::sine;
        case Wave::saw: break;
    }
    return synth::Waveform::saw;
}

float decibelsToGain (float db) noexcept { return std::pow (10.0F, db / 20.0F); }

double noteToHz (float note) noexcept { return 440.0 * std::exp2 ((static_cast<double> (note) - 69.0) / 12.0); }
} // namespace

// --- Setup (not real-time) ---------------------------------------------------

void StarterSynthEngine::prepare (const ProcessSetup& setup)
{
    sampleRate_ = setup.sampleRate;
    for (auto& voice : voices_)
    {
        voice.osc1.prepare (sampleRate_);
        voice.osc2.prepare (sampleRate_);
        voice.filter.prepare (sampleRate_);
        voice.env.prepare (sampleRate_);
    }
    for (auto* smoother : {&mix_, &detune_, &cutoffOctaves_, &resonance_, &envAmount_, &gain_})
        smoother->prepare (sampleRate_, kSmoothingSeconds);
    setParameters (params_); // recompute everything that depends on the rate
    reset();
}

void StarterSynthEngine::reset() noexcept
{
    for (auto& voice : voices_)
    {
        voice.osc1.reset();
        voice.osc2.reset();
        voice.filter.reset();
        voice.env.reset();
        voice.sustained = false;
    }
    allocator_.reset();
    pedalDown_ = false;
    lastPitch_ = -1.0F;
    // Nothing is sounding, so the next parameters may apply at once instead
    // of ramping from stale values.
    snapSmoothers_ = true;
}

// --- Parameters (once per block, real-time) ---------------------------------

void StarterSynthEngine::setParameters (const StarterSynthParameters& params) noexcept
{
    params_ = params;
    // Smoothed controls: the engine reaches each new value over 20 ms. Setting
    // the same target again does nothing, so the result does not depend on how
    // the host splits blocks.
    mix_.setTarget (params.get (ParamId::oscMix));
    detune_.setTarget (params.get (ParamId::osc2Detune));
    cutoffOctaves_.setTarget (std::log2 (params.get (ParamId::cutoff))); // smooth in octaves: sounds even
    resonance_.setTarget (params.get (ParamId::resonance) * kMaxFilterResonance);
    envAmount_.setTarget (params.get (ParamId::envAmount));
    gain_.setTarget (decibelsToGain (params.get (ParamId::outputGain)));
    if (snapSmoothers_)
    {
        for (auto* smoother : {&mix_, &detune_, &cutoffOctaves_, &resonance_, &envAmount_, &gain_})
            smoother->setImmediate (smoother->getTarget());
        snapSmoothers_ = false;
    }

    // The envelope reads new times at its next step; no smoothing needed.
    const core::Adsr::Parameters env {params.get (ParamId::attack), params.get (ParamId::decay),
                                      params.get (ParamId::sustain), params.get (ParamId::release)};
    for (auto& voice : voices_)
        voice.env.setParameters (env);

    // Glide is a one-pole slide in pitch: each sample moves this fraction of
    // the remaining distance, so `glide` seconds is the time constant.
    const double glide = params.get (ParamId::glide);
    glideCoefficient_ = glide < 0.001 ? 1.0F : static_cast<float> (1.0 - std::exp (-1.0 / (glide * sampleRate_)));
}

// --- Events -----------------------------------------------------------------

void StarterSynthEngine::handleEvent (const Event& event) noexcept
{
    switch (event.type)
    {
        case Event::Type::noteOn:
            if (event.value > 0.0F)
                noteOn (event.note, std::min (event.value, 1.0F));
            else
                noteOff (event.note); // MIDI convention: velocity 0 means note off
            break;
        case Event::Type::noteOff:
            noteOff (event.note);
            break;
        case Event::Type::sustainPedal:
            pedalDown_ = event.value >= 0.5F;
            if (!pedalDown_)
                for (int i = 0; i < kMaxVoices; ++i)
                    if (voices_[static_cast<std::size_t> (i)].sustained)
                        releaseVoice (i);
            break;
        case Event::Type::allNotesOff:
            pedalDown_ = false;
            for (int i = 0; i < kMaxVoices; ++i)
                if (allocator_.slot (i).sounding)
                    releaseVoice (i);
            break;
        case Event::Type::allSoundOff:
            reset();
            break;
        default:
            break; // mod wheel, pitch bend, pressure: not used by this synth
    }
}

void StarterSynthEngine::noteOn (int note, float velocity) noexcept
{
    // The allocator picks the slot: a free voice, else it steals the oldest.
    const int index = allocator_.noteOn (note);
    auto& voice = voices_[static_cast<std::size_t> (index)];
    if (!voice.env.isActive())
    {
        // A silent voice starts clean. A stolen one keeps its state, and its
        // envelope restarts from the current level, so stealing never clicks.
        voice.osc1.reset();
        voice.osc2.reset (0.25); // a different start phase keeps the two oscillators from cancelling
        voice.filter.reset();
        voice.filter.setCutoffImmediate (std::exp2 (cutoffOctaves_.getCurrent()));
    }
    const auto pitch = static_cast<float> (note);
    // With glide on, the new note slides from the previous one.
    voice.pitch = (glideCoefficient_ < 1.0F && lastPitch_ >= 0.0F) ? lastPitch_ : pitch;
    voice.targetPitch = pitch;
    voice.velocity = velocity;
    voice.sustained = false;
    voice.env.noteOn();
    lastPitch_ = pitch;
}

void StarterSynthEngine::noteOff (int note) noexcept
{
    const int index = allocator_.noteOff (note);
    if (index == core::VoiceAllocator<kMaxVoices>::kNoVoice)
        return;
    if (pedalDown_)
        voices_[static_cast<std::size_t> (index)].sustained = true; // released when the pedal lifts
    else
        voices_[static_cast<std::size_t> (index)].env.noteOff();
}

void StarterSynthEngine::releaseVoice (int index) noexcept
{
    auto& voice = voices_[static_cast<std::size_t> (index)];
    voice.sustained = false;
    voice.env.noteOff();
    allocator_.noteOff (allocator_.slot (index).note);
}

// --- Audio ------------------------------------------------------------------

void StarterSynthEngine::process (const AudioBlock& block, const Event* events, int numEvents) noexcept
{
    // Events are sorted by sample offset. Render up to each event, apply it,
    // continue: a note starts on exactly the sample the host asked for.
    int position = 0;
    for (int i = 0; i < numEvents; ++i)
    {
        const int offset = std::clamp (events[i].sampleOffset, position, block.numSamples);
        render (block.left, block.right, position, offset);
        handleEvent (events[i]);
        position = offset;
    }
    render (block.left, block.right, position, block.numSamples);
}

void StarterSynthEngine::render (float* left, float* right, int start, int end) noexcept
{
    const auto wave1 = toWaveform (params_.get (ParamId::osc1Wave));
    const auto wave2 = toWaveform (params_.get (ParamId::osc2Wave));

    for (int n = start; n < end; ++n)
    {
        // Advance every smoother once per sample, voices or not.
        const float mix = mix_.next();
        const double detuneRatio = std::exp2 (static_cast<double> (detune_.next()) / 1200.0);
        const float cutoffOctaves = cutoffOctaves_.next();
        const float resonance = resonance_.next();
        const float envAmount = envAmount_.next();
        const float gain = gain_.next();
        if (resonance != appliedResonance_)
        {
            // setResonance recomputes filter coefficients: only when it moved.
            for (auto& voice : voices_)
                voice.filter.setResonance (resonance);
            appliedResonance_ = resonance;
        }

        float sum = 0.0F;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            if (!allocator_.slot (i).sounding)
                continue;
            auto& voice = voices_[static_cast<std::size_t> (i)];

            voice.pitch += (voice.targetPitch - voice.pitch) * glideCoefficient_;
            const double hz = noteToHz (voice.pitch);
            voice.osc1.setFrequency (hz);
            voice.osc2.setFrequency (hz * detuneRatio);
            const float oscillators = voice.osc1.process (wave1) * (1.0F - mix) + voice.osc2.process (wave2) * mix;

            // The envelope opens the filter by envAmount octaves at full level.
            const float level = voice.env.process();
            voice.filter.setCutoff (std::exp2 (std::clamp (cutoffOctaves + envAmount * level, 4.33F, 14.3F)));
            const float filtered = voice.filter.process (oscillators, synth::SVFMode::lowPass);

            sum += filtered * level * voice.velocity * kVoiceGain;

            if (!voice.env.isActive())
                allocator_.voiceFinished (i); // release tail done: the slot is free
        }

        float outL = sum * gain;
        float outR = outL;
        core::StereoPeakProtector::process (outL, outR); // soft knee for rare peaks above -1 dBFS
        left[n] = outL;
        right[n] = outR;
    }
}
} // namespace startersynth
