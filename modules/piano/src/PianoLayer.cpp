// SPDX-License-Identifier: MIT
#include "kraftonic/piano/PianoLayer.h"

#include "ModelVoice.h"
#include "TineVoice.h"

#include <kraftonic/core/Instrument.h>
#include <kraftonic/core/Math.h>
#include <kraftonic/core/Smoother.h>
#include <kraftonic/core/VoiceAllocator.h>
#include <kraftonic/fx/Reverb.h>
#include <kraftonic/sampler/SampleBank.h>
#include <kraftonic/sampler/SampleLocations.h>
#include <kraftonic/sampler/Sampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The code below moved from a product engine into this module. Its statements
// are kept as they were, including the order of every floating-point
// operation, so downstream products render bit for bit (see README.md).

namespace kraftonic::piano
{
namespace
{
    using core::Event;

    constexpr int kEnvelopeUpdateInterval = 64;

    // The sampled grand: Bright Grand and Studio Grand play the same bank.
    // Levels (with the sample set's own gain_db) put a velocity-100 triad
    // about 3 dB under the Tine EP's attack, leaving headroom for the
    // recordings' wider dynamic range; Studio is the warmer, wider voicing
    // (darker tilt, wider stereo image).
    constexpr int kFirstGrand = 4; // kModels index of the first grand
    constexpr int kNumGrands = 2;
    constexpr std::array<float, kNumGrands> kGrandGainDb { 19.0f, 19.0f };
    constexpr std::array<float, kNumGrands> kGrandWidth { 1.0f, 1.35f };
    constexpr std::array<float, kNumGrands> kGrandTilt { 1.0f, 0.45f };
    constexpr int kGrandQueue = 2 * core::kMaxEventsPerBlock;

    // The other sampled models, each with its own recordings: Harpsichord,
    // Vibes and Marimba. Gains (with each sample set's own gain_db) put a
    // velocity-100 triad at about the Tine EP's level (measured in the original product's sound tests).
    constexpr int kFirstKeys = 11; // kModels index of the first one
    constexpr int kNumKeys = 3;
    constexpr std::array<const char*, kNumKeys> kKeysFolder { "harpsichord", "vibes", "marimba" };
    constexpr std::array<float, kNumKeys> kKeysGainDb { 12.0f, 5.3f, 6.8f };

    // Which engine a voice slot is playing.
    constexpr std::uint8_t kTineSlot = 0;
    constexpr std::uint8_t kModelSlot = 1;

    // Smoothed controls.
    enum Smoothed : std::size_t
    {
        sTone,
        sStrike,
        sSensitivity,
        sAttack,
        sDecay,
        sSustain,
        sRelease,
        kNumSmoothed
    };

    // kModels indices of the sounds after the grands (appended; see PianoSounds.h).
    constexpr int kTineEp = 0;
    constexpr int kBrightTine = 6;
    constexpr int kHardReed = 7;
    constexpr int kGlassReed = 8;
    constexpr int kBrightClav = 9;
    constexpr int kCelesta = 10;
    constexpr int kHarpsichord = 11;
    constexpr int kVibes = 12;
    constexpr int kMarimba = 13;
    static_assert (kHarpsichord == kFirstKeys && kMarimba + 1 == kFirstKeys + kNumKeys,
                   "the sampled keys are kModels kFirstKeys onwards");
    static_assert (kMarimba + 1 == kNumModels, "map every appended model below");

    /** True for the sounds the Tine voice plays (the others use ModelVoice). */
    bool tineSound (int sound) noexcept { return sound == kTineEp || sound == kBrightTine; }

    detail::TineVariant tineVariant (int sound) noexcept
    {
        return sound == kBrightTine ? detail::TineVariant::bright : detail::TineVariant::standard;
    }

    /** Model index (kModels) -> voice model for the non-Tine sounds. */
    detail::Model voiceModel (int sound) noexcept
    {
        switch (sound)
        {
            case 1: return detail::Model::reed;
            case 2: return detail::Model::electricGrand;
            case 3: return detail::Model::clav;
            case 4: return detail::Model::brightGrand;
            case kHardReed: return detail::Model::hardReed;
            case kGlassReed: return detail::Model::glassReed;
            case kBrightClav: return detail::Model::brightClav;
            case kCelesta: return detail::Model::celesta;
            case kHarpsichord: return detail::Model::harpsichord;
            case kVibes: return detail::Model::vibes;
            case kMarimba: return detail::Model::marimba;
            default: return detail::Model::studioGrand;
        }
    }

    float guarded (float value, float fallback, float low, float high) noexcept
    {
        return std::clamp (std::isfinite (value) ? value : fallback, low, high);
    }

    /** Clamps every field into its documented range. */
    Parameters sanitised (const Parameters& p) noexcept
    {
        const Parameters d;
        Parameters s = p;
        s.type = static_cast<int> (p.type) < kNumTypes ? p.type : d.type;
        s.model = std::clamp (p.model, 0, std::max (0, modelCount (s.type) - 1));
        s.tone = guarded (p.tone, d.tone, 0.0f, 1.0f);
        s.timbre = static_cast<int> (p.timbre) <= static_cast<int> (Timbre::bright) ? p.timbre : d.timbre;
        s.strike = guarded (p.strike, d.strike, 0.0f, 1.0f);
        s.velocitySensitivity = guarded (p.velocitySensitivity, d.velocitySensitivity, 0.0f, 1.0f);
        s.touch = static_cast<int> (p.touch) <= static_cast<int> (Touch::fixed) ? p.touch : d.touch;
        s.attack = guarded (p.attack, d.attack, 0.001f, 0.5f);
        s.decay = guarded (p.decay, d.decay, 0.2f, 20.0f);
        s.sustain = guarded (p.sustain, d.sustain, 0.0f, 1.0f);
        s.release = guarded (p.release, d.release, 0.01f, 5.0f);
        s.stringResonance = guarded (p.stringResonance, d.stringResonance, 0.0f, 1.0f);
        s.pedalNoise = guarded (p.pedalNoise, d.pedalNoise, 0.0f, 1.0f);
        s.level = guarded (p.level, d.level, 0.0f, 2.0f);
        s.octave = std::clamp (p.octave, -3, 3);
        return s;
    }
} // namespace

struct PianoLayer::Impl
{
    double sampleRate = 48000.0;
    int scratchSize = 0;
    Parameters params = sanitised (Parameters {});
    std::array<core::LinearSmoother, kNumSmoothed> smoothers; // stepped with the voices
    core::LinearSmoother level;                                // stepped by the bus
    int envelopeUpdateCountdown = 0;

    // Each slot plays the Tine voice or a model voice, chosen per note at
    // note-on. Changing the sound therefore only affects new notes; sounding
    // notes finish on the sound they started with.
    std::array<detail::TineVoice, kMaxVoices> voices;
    std::array<detail::ModelVoice, kMaxVoices> modelVoices;
    std::array<std::uint8_t, kMaxVoices> slotKind {};
    core::VoiceAllocator<kMaxVoices> allocator;

    // Sampled grands (only used when the grand bank loaded). Events are
    // queued and delivered at the next render start.
    struct Grand
    {
        sampler::Sampler player;
        std::array<Event, kGrandQueue> queue {};
        int queued = 0;
        float lowLeft = 0.0f, lowRight = 0.0f; // Tone tilt state
    };
    Bank pendingBank;
    Bank grandBank;
    std::array<std::unique_ptr<Grand>, kNumGrands> grands;
    bool grandsLoaded = false;
    std::array<std::int8_t, 128> samplerKey {}; // grand playing a note, -1 none
    // Per render: the grands' low part (A) and tilted high part (B), so the
    // live Tone is applied per sample as A + g(tone) * B.
    std::vector<float> grandLeft, grandRight, grandHighLeft, grandHighRight, grandTemp;
    float grandLowCoefficient = 0.85f;
    bool grandLive = false; // the last render's grand output is non-zero

    // The other sampled models (kFirstKeys onwards): one player each, used
    // only when its bank loaded, with its own A/B buffers like the grands'.
    std::array<Bank, kNumKeys> pendingKeysBanks;
    std::array<std::unique_ptr<Grand>, kNumKeys> keys;
    std::array<std::int8_t, 128> keysKey {}; // sampled key model playing a note, -1 none
    std::vector<float> keysLeft, keysRight, keysHighLeft, keysHighRight;
    bool keysLive = false;

    // Key state: a note is "down" from its note-on until its note-off.
    std::array<bool, 128> down {};
    std::array<bool, 128> heldByPedal {};
    std::array<std::int8_t, 128> keyNote {}; // key -> note it started (octave), -1 none
    bool pedalDown = false;
    core::LinearSmoother pedalDepth;

    // Piano bus: dynamic compression, detuned doubling, sympathetic
    // resonance, mechanical pedal noise. Preallocated in prepare(), cleared
    // in silence().
    float dynCompEnv = 0.0f;
    float dynCompGain = 1.0f;
    std::vector<float> unisonLineL, unisonLineR;
    int unisonIndex = 0;
    double unisonPhase = 0.0;
    float pedalNoiseEnv = 0.0f;
    std::uint32_t pedalNoiseState = 0x243f6a88u;
    fx::Reverb reso;
    float unisonMix = 0.0f; // smoothed doubling wet mix (0, or about 0.35 when on)
    float resoMix = 0.0f;   // smoothed sympathetic mix (string resonance times the damper)
    std::vector<float> resoLeft, resoRight; // wet-only scratch for the resonance tank

    float value (Smoothed id) const noexcept { return smoothers[id].getCurrent(); }

    int sound() const noexcept { return modelIndex (params.type, params.model); }

    void setTargets() noexcept
    {
        smoothers[sTone].setTarget (params.tone);
        smoothers[sStrike].setTarget (params.strike);
        smoothers[sSensitivity].setTarget (params.velocitySensitivity);
        smoothers[sAttack].setTarget (params.attack);
        smoothers[sDecay].setTarget (params.decay);
        smoothers[sSustain].setTarget (params.sustain);
        smoothers[sRelease].setTarget (params.release);
        level.setTarget (params.level);
    }

    void updateEnvelopes() noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& voice = voices[static_cast<std::size_t> (i)];
            voice.env.setParameters ({ value (sAttack),
                                       value (sDecay) * voice.decayScale,
                                       value (sSustain),
                                       value (sRelease) * releaseScale() });
        }
        for (int i = 0; i < kMaxVoices; ++i)
            if (slotKind[static_cast<std::size_t> (i)] == kModelSlot)
                updateModelEnvelope (modelVoices[static_cast<std::size_t> (i)]);
    }

    void updateModelEnvelope (detail::ModelVoice& voice) noexcept
    {
        voice.env.setParameters ({ value (sAttack),
                                   value (sDecay) * voice.decayScale(),
                                   value (sSustain),
                                   value (sRelease) * voice.releaseScale() * releaseScale() });
    }

    detail::Controls controls() const noexcept
    {
        return { tone(), value (sStrike), value (sSensitivity) };
    }

    /** Timbre trims the tone control: Soft darkens, Bright lifts, Mid is flat. */
    float tone() const noexcept
    {
        return std::clamp (value (sTone)
                + (static_cast<float> (static_cast<int> (params.timbre)) - 1.0f) * 0.25f,
            0.0f, 1.0f);
    }

    /** Touch reshapes the velocity curve: Light plays louder, Heavy needs force. */
    float touchCurve (float velocity) const noexcept
    {
        const float v = std::clamp (velocity, 0.0f, 1.0f);
        switch (params.touch)
        {
            case Touch::light: return std::pow (v, 0.7f);
            case Touch::heavy: return v * v * (0.5f + 0.5f * v);
            case Touch::fixed: return 100.0f / 127.0f;
            case Touch::normal: break;
        }
        return v;
    }

    /** Soft Release shortens every release to a gentle third. */
    float releaseScale() const noexcept { return (params.softRelease ? 0.35f : 1.0f) * (1.0f + 3.0f * pedalDepth.getCurrent()); }

    void releaseVoice (int note) noexcept
    {
        const int index = allocator.noteOff (note);
        if (index == core::VoiceAllocator<kMaxVoices>::kNoVoice)
            return;
        if (slotKind[static_cast<std::size_t> (index)] == kModelSlot)
            modelVoices[static_cast<std::size_t> (index)].release (value (sRelease));
        else
            voices[static_cast<std::size_t> (index)].env.noteOff();
    }

    void pushGrand (int which, const Event& e) noexcept
    {
        auto& grand = *grands[static_cast<std::size_t> (which)];
        if (grand.queued < kGrandQueue)
        {
            auto copy = e;
            copy.sampleOffset = 0;
            grand.queue[static_cast<std::size_t> (grand.queued++)] = copy;
        }
    }

    void pushGrands (const Event& e) noexcept
    {
        if (grandsLoaded)
            for (int which = 0; which < kNumGrands; ++which)
                pushGrand (which, e);
        for (int which = 0; which < kNumKeys; ++which)
            if (keys[static_cast<std::size_t> (which)] != nullptr)
                pushKeys (which, e);
    }

    void pushKeys (int which, const Event& e) noexcept
    {
        auto& player = *keys[static_cast<std::size_t> (which)];
        if (player.queued < kGrandQueue)
        {
            auto copy = e;
            copy.sampleOffset = 0;
            player.queue[static_cast<std::size_t> (player.queued++)] = copy;
        }
    }

    /** Releases a note playing on a sampled key model (like releaseGrandNote). */
    bool releaseKeysNote (int note) noexcept
    {
        auto& which = keysKey[static_cast<std::size_t> (note)];
        if (which < 0)
            return false;
        pushKeys (which, { Event::Type::noteOff, 0, note, 0.0f });
        which = -1;
        return true;
    }

    /** The bank of the selected sampled model loaded at the last prepare(). */
    bool sampledLoaded (int current) const noexcept
    {
        const int which = current - kFirstKeys;
        if (which >= 0 && which < kNumKeys)
            return keys[static_cast<std::size_t> (which)] != nullptr;
        return grandsLoaded;
    }

    /** Releases a note that is playing on a sampled grand (it handles the
        sustain pedal itself, with its damper and pedal noises). */
    bool releaseGrandNote (int note) noexcept
    {
        auto& which = samplerKey[static_cast<std::size_t> (note)];
        if (which < 0)
            return false;
        pushGrand (which, { Event::Type::noteOff, 0, note, 0.0f });
        which = -1;
        return true;
    }

    /** A note-off: the sustain pedal holds the note. */
    void noteOff (int note) noexcept
    {
        down[static_cast<std::size_t> (note)] = false;
        if (releaseGrandNote (note) || releaseKeysNote (note))
            return;
        if (pedalDown)
            heldByPedal[static_cast<std::size_t> (note)] = true;
        else
            releaseVoice (note);
    }

    void silence() noexcept
    {
        allocator.reset();
        for (auto& voice : voices)
        {
            voice.env.reset();
            voice.lastOutput = 0.0f;
            voice.retriggerSamples = 0;
            voice.retriggerOffset = 0.0f;
        }
        for (auto& voice : modelVoices)
            voice.reset();
        slotKind.fill (kTineSlot);
        for (auto& grand : grands)
            if (grand != nullptr)
            {
                grand->player.reset();
                grand->queued = 0;
                grand->lowLeft = grand->lowRight = 0.0f;
            }
        samplerKey.fill (-1);
        for (auto& player : keys)
            if (player != nullptr)
            {
                player->player.reset();
                player->queued = 0;
                player->lowLeft = player->lowRight = 0.0f;
            }
        keysKey.fill (-1);
        keysLive = false;
        down = {};
        heldByPedal = {};
        keyNote.fill (-1);
        pedalDown = false;
        dynCompEnv = 0.0f;
        dynCompGain = 1.0f;
        std::fill (unisonLineL.begin(), unisonLineL.end(), 0.0f);
        std::fill (unisonLineR.begin(), unisonLineR.end(), 0.0f);
        unisonIndex = 0;
        unisonPhase = 0.0;
        unisonMix = 0.0f;
        resoMix = 0.0f;
        pedalNoiseEnv = 0.0f;
        reso.reset();
    }

    void trigger (int note, float velocity) noexcept
    {
        down[static_cast<std::size_t> (note)] = true;
        heldByPedal[static_cast<std::size_t> (note)] = false;
        const float curved = touchCurve (velocity);
        const int grandSound = sound() - kFirstGrand;
        const int keysSound = sound() - kFirstKeys;
        if (keysSound >= 0 && keysSound < kNumKeys && keys[static_cast<std::size_t> (keysSound)] != nullptr)
        {
            // A recorded key model, played like the grands.
            releaseVoice (note);
            releaseGrandNote (note);
            const auto previous = keysKey[static_cast<std::size_t> (note)];
            if (previous >= 0 && previous != keysSound)
                pushKeys (previous, { Event::Type::noteOff, 0, note, 0.0f });
            pushKeys (keysSound, { Event::Type::noteOn, 0, note, curved });
            keysKey[static_cast<std::size_t> (note)] = static_cast<std::int8_t> (keysSound);
            return;
        }
        releaseKeysNote (note); // the note may have been playing a sampled key model
        if (grandSound >= 0 && grandSound < kNumGrands && grandsLoaded)
        {
            // The recorded grand. A voice of this note on another engine is
            // released; the sampler fades a re-struck note of its own.
            releaseVoice (note);
            const auto previous = samplerKey[static_cast<std::size_t> (note)];
            if (previous >= 0 && previous != grandSound)
                pushGrand (previous, { Event::Type::noteOff, 0, note, 0.0f });
            pushGrand (grandSound, { Event::Type::noteOn, 0, note, curved });
            samplerKey[static_cast<std::size_t> (note)] = static_cast<std::int8_t> (grandSound);
            return;
        }
        const int index = allocator.noteOn (note);
        auto& voice = voices[static_cast<std::size_t> (index)];
        auto& model = modelVoices[static_cast<std::size_t> (index)];
        auto& kind = slotKind[static_cast<std::size_t> (index)];
        const int current = sound();
        releaseGrandNote (note); // the note may have been playing a grand
        if (! tineSound (current))
        {
            // A model sound. The slot's previous output (either engine) fades
            // out over 5 ms inside the new voice, so a steal never clicks.
            // Carry what the slot last produced: a Tine voice (mono) and/or
            // a model voice (stereo, possibly only fading out by now).
            float carryLeft = model.sounding() ? model.lastLeft() : 0.0f;
            float carryRight = model.sounding() ? model.lastRight() : 0.0f;
            if (kind == kTineSlot)
            {
                carryLeft += voice.lastOutput;
                carryRight += voice.lastOutput;
                voice.env.reset();
                voice.retriggerSamples = 0;
                voice.retriggerOffset = 0.0f;
                voice.lastOutput = 0.0f;
            }
            kind = kModelSlot;
            // A sampled model whose data is missing plays its quieter stand-in.
            const bool standIn = kModels[static_cast<std::size_t> (current)].source == Source::sampled
                && ! sampledLoaded (current);
            model.start (voiceModel (current), note, curved, controls(), carryLeft, carryRight,
                         standIn ? detail::kStandInGain : 1.0f);
            updateModelEnvelope (model);
            return;
        }
        if (kind == kModelSlot)
        {
            // Back to the Tine EP on a slot a model was using: the model
            // voice fades its last sample out in place (see renderVoices).
            model.fadeOut();
            kind = kTineSlot;
        }
        voice.start (note, curved, sampleRate, value (sSensitivity), tineVariant (current));
        voice.env.setParameters ({ value (sAttack),
                                   value (sDecay) * voice.decayScale,
                                   value (sSustain),
                                   value (sRelease) });
    }

    void sustain (float pedal) noexcept
    {
        // Both edges thump the mechanics when pedal noise is on.
        if ((pedal >= 0.5f) != pedalDown && params.pedalNoise > 0.0f)
            pedalNoiseEnv = 1.0f;
        pedalDown = pedal >= 0.5f;
        pushGrands ({ Event::Type::sustainPedal, 0, 0, pedal });
        if (! pedalDown)
            for (int note = 0; note < 128; ++note)
                if (std::exchange (heldByPedal[static_cast<std::size_t> (note)], false)
                    && ! down[static_cast<std::size_t> (note)])
                    releaseVoice (note);
    }

    void allNotesOff() noexcept
    {
        pedalDown = false;
        heldByPedal = {};
        down = {};
        keyNote.fill (-1);
        for (int note = 0; note < 128; ++note)
            releaseVoice (note);
        pushGrands ({ Event::Type::allNotesOff, 0, 0, 0.0f });
        samplerKey.fill (-1);
        keysKey.fill (-1);
    }

    bool grandsSounding() const noexcept
    {
        for (const auto& grand : grands)
            if (grand != nullptr && (grand->queued > 0 || grand->player.activeVoiceCount() > 0))
                return true;
        for (const auto& player : keys)
            if (player != nullptr && (player->queued > 0 || player->player.activeVoiceCount() > 0))
                return true;
        return false;
    }

    int grandVoices() const noexcept
    {
        int n = 0;
        for (const auto& grand : grands)
            if (grand != nullptr)
                n += grand->player.activeVoiceCount();
        for (const auto& player : keys)
            if (player != nullptr)
                n += player->player.activeVoiceCount();
        return n;
    }

    void updateGrandParameters() noexcept
    {
        for (int which = 0; which < kNumKeys; ++which)
            if (keys[static_cast<std::size_t> (which)] != nullptr)
            {
                // As for the grands; the harpsichord's release thump follows
                // the strike control like the grands' damper noise.
                sampler::Sampler::Parameters sp;
                sp.gainDb = kKeysGainDb[static_cast<std::size_t> (which)];
                sp.velocitySensitivity = std::clamp (params.velocitySensitivity / 0.7f, 0.0f, 2.0f);
                sp.releaseScale = std::clamp (params.release / 0.35f, 0.1f, 10.0f) * releaseScale();
                sp.releaseNoiseDb = std::clamp ((params.strike - 0.35f) * 20.0f, -30.0f, 6.0f);
                keys[static_cast<std::size_t> (which)]->player.setParameters (sp);
            }
        if (! grandsLoaded)
            return;
        // The controls that make sense for recordings: velocity sensitivity,
        // release time and strike (hammer/damper noise level). Attack, decay
        // and sustain shape the modelled sounds only.
        for (int which = 0; which < kNumGrands; ++which)
        {
            sampler::Sampler::Parameters sp;
            sp.gainDb = kGrandGainDb[static_cast<std::size_t> (which)];
            sp.velocitySensitivity = std::clamp (params.velocitySensitivity / 0.7f, 0.0f, 2.0f);
            sp.releaseScale = std::clamp (params.release / 0.35f, 0.1f, 10.0f)
                * releaseScale();
            sp.releaseNoiseDb = std::clamp ((params.strike - 0.35f) * 20.0f, -30.0f, 6.0f);
            sp.stereoWidth = kGrandWidth[static_cast<std::size_t> (which)];
            grands[static_cast<std::size_t> (which)]->player.setParameters (sp);
        }
    }

    /** Renders the grands into the A/B buffers; sets grandLive. */
    void renderGrands (int samples) noexcept
    {
        grandLive = false;
        if (! grandsLoaded)
            return;
        float* a0 = grandLeft.data();
        float* a1 = grandRight.data();
        float* b0 = grandHighLeft.data();
        float* b1 = grandHighRight.data();
        float* tempL = grandTemp.data();
        float* tempR = grandTemp.data() + scratchSize;
        std::fill (a0, a0 + samples, 0.0f);
        std::fill (a1, a1 + samples, 0.0f);
        std::fill (b0, b0 + samples, 0.0f);
        std::fill (b1, b1 + samples, 0.0f);
        for (int which = 0; which < kNumGrands; ++which)
        {
            auto& grand = *grands[static_cast<std::size_t> (which)];
            grand.player.process ({ tempL, tempR, samples }, grand.queue.data(), grand.queued);
            grand.queued = 0;
            const float tilt = kGrandTilt[static_cast<std::size_t> (which)];
            for (int i = 0; i < samples; ++i)
            {
                grand.lowLeft = tempL[i] + grandLowCoefficient * (grand.lowLeft - tempL[i]);
                grand.lowRight = tempR[i] + grandLowCoefficient * (grand.lowRight - tempR[i]);
                a0[i] += grand.lowLeft;
                a1[i] += grand.lowRight;
                b0[i] += tilt * (tempL[i] - grand.lowLeft);
                b1[i] += tilt * (tempR[i] - grand.lowRight);
            }
        }
        for (int i = 0; i < samples && ! grandLive; ++i)
            grandLive = a0[i] != 0.0f || a1[i] != 0.0f || b0[i] != 0.0f || b1[i] != 0.0f;
    }

    /** Renders the sampled key models into their A/B buffers; sets keysLive. */
    void renderKeys (int samples) noexcept
    {
        keysLive = false;
        float* a0 = keysLeft.data();
        float* a1 = keysRight.data();
        float* b0 = keysHighLeft.data();
        float* b1 = keysHighRight.data();
        float* tempL = grandTemp.data();
        float* tempR = grandTemp.data() + scratchSize;
        bool any = false;
        for (auto& player : keys)
        {
            if (player == nullptr)
                continue;
            if (! any)
            {
                std::fill (a0, a0 + samples, 0.0f);
                std::fill (a1, a1 + samples, 0.0f);
                std::fill (b0, b0 + samples, 0.0f);
                std::fill (b1, b1 + samples, 0.0f);
                any = true;
            }
            player->player.process ({ tempL, tempR, samples }, player->queue.data(), player->queued);
            player->queued = 0;
            for (int i = 0; i < samples; ++i)
            {
                player->lowLeft = tempL[i] + grandLowCoefficient * (player->lowLeft - tempL[i]);
                player->lowRight = tempR[i] + grandLowCoefficient * (player->lowRight - tempR[i]);
                a0[i] += player->lowLeft;
                a1[i] += player->lowRight;
                b0[i] += tempL[i] - player->lowLeft;
                b1[i] += tempR[i] - player->lowRight;
            }
        }
        for (int i = 0; i < samples && any && ! keysLive; ++i)
            keysLive = a0[i] != 0.0f || a1[i] != 0.0f || b0[i] != 0.0f || b1[i] != 0.0f;
    }

    /** Voices into left/right (replacing), at most scratchSize samples. */
    void renderVoices (float* outL, float* outR, int samples) noexcept
    {
        renderGrands (samples);
        renderKeys (samples);
        const float* keysL = keysLeft.data();
        const float* keysR = keysRight.data();
        const float* keysHighL = keysHighLeft.data();
        const float* keysHighR = keysHighRight.data();
        const float* grandL = grandLeft.data();
        const float* grandR = grandRight.data();
        const float* grandHighL = grandHighLeft.data();
        const float* grandHighR = grandHighRight.data();
        for (int sample = 0; sample < samples; ++sample)
        {
            for (auto& smoother : smoothers)
                smoother.next();
            pedalDepth.next();
            if (envelopeUpdateCountdown-- <= 0)
            {
                updateEnvelopes();
                envelopeUpdateCountdown = kEnvelopeUpdateInterval - 1;
            }
            float sum = 0.0f;
            float modelLeft = 0.0f;
            float modelRight = 0.0f;
            bool anyModel = false;
            for (int index = 0; index < kMaxVoices; ++index)
            {
                if (! allocator.slot (index).sounding)
                    continue;
                if (slotKind[static_cast<std::size_t> (index)] == kModelSlot)
                {
                    // Model voices mix on their own stereo pair; the Tine
                    // voices sum in mono.
                    auto& model = modelVoices[static_cast<std::size_t> (index)];
                    model.next ({ tone(), value (sStrike), value (sSensitivity) },
                                modelLeft, modelRight);
                    anyModel = true;
                    if (model.finished())
                        allocator.voiceFinished (index);
                    continue;
                }
                auto& voice = voices[static_cast<std::size_t> (index)];
                sum += voice.next (tone(), value (sStrike), value (sSensitivity));
                auto& fading = modelVoices[static_cast<std::size_t> (index)];
                if (fading.sounding())
                {
                    // A model note this Tine note stole, fading out (5 ms).
                    fading.next ({ tone(), value (sStrike), value (sSensitivity) },
                                 modelLeft, modelRight);
                    anyModel = true;
                }
                if (! voice.env.isActive() && voice.retriggerSamples == 0 && ! fading.sounding())
                    allocator.voiceFinished (index);
            }
            if (grandLive)
            {
                // Recorded grand, with the live Tone as a first-order tilt.
                const float tilt = 0.35f + 1.3f * tone();
                modelLeft += grandL[sample] + tilt * grandHighL[sample];
                modelRight += grandR[sample] + tilt * grandHighR[sample];
                anyModel = true;
            }
            if (keysLive)
            {
                // Recorded key models, with the same live Tone tilt.
                const float tilt = 0.35f + 1.3f * tone();
                modelLeft += keysL[sample] + tilt * keysHighL[sample];
                modelRight += keysR[sample] + tilt * keysHighR[sample];
                anyModel = true;
            }
            if (anyModel)
            {
                outL[sample] = sum + modelLeft;
                outR[sample] = sum + modelRight;
            }
            else
            {
                outL[sample] = sum;
                outR[sample] = sum;
            }
        }
    }

    /** The piano bus: gentle peak compression, detuned doubling, sympathetic
        resonance and mechanical pedal noise, in that order, then the level.
        Every bypass is bit-exact: the bus is only touched while its feature
        is audibly active, and disabled smoothers snap to their neutral
        values. At most scratchSize samples. */
    void processBus (float* pianoL, float* pianoR, int samples) noexcept
    {
        if (pianoL == nullptr || pianoR == nullptr || samples <= 0)
            return;
        const bool compOn = params.dynamicCompression;
        const bool unisonOn = params.doubling;
        const float resoAmount = std::clamp (params.stringResonance, 0.0f, 1.0f);
        const float noiseLevel = std::clamp (params.pedalNoise, 0.0f, 1.0f);
        // The damper opens fully while the pedal is down or any key is held,
        // and rests a quarter open otherwise.
        bool anyDown = pedalDown;
        for (int note = 0; note < 128 && ! anyDown; ++note)
            anyDown = down[static_cast<std::size_t> (note)];
        const float damper = anyDown ? 1.0f : 0.25f;

        const float rate = static_cast<float> (sampleRate);
        // Gentle peak compression: about -18 dBFS threshold, a fixed musical
        // 3:1 ratio, and a smoothed gain so onsets never jump.
        constexpr float kCompThreshold = 0.1258925f; // 10^(-18/20)
        constexpr float kCompRatio = 3.0f;
        const float compAttack = 1.0f - std::exp (-1.0f / (0.001f * rate));
        const float compRelease = 1.0f - std::exp (-1.0f / (0.150f * rate));
        const float gainAttack = 1.0f - std::exp (-1.0f / (0.005f * rate));
        const float gainRelease = 1.0f - std::exp (-1.0f / (0.080f * rate));
        // Detuned doubling: a short delay wobbled by a slow LFO and read
        // with linear interpolation. The right channel moves against the
        // left, and the mix fades over about 10 ms so toggling never clicks.
        const float uniStep = static_cast<float> (core::kTwoPi * 0.6 / sampleRate);
        const float uniBase = 0.007f * rate;
        const float uniDepth = 0.0025f * rate;
        constexpr float kUnisonMix = 0.35f;
        const float uniSmooth = 1.0f - std::exp (-1.0f / (0.010f * rate));
        const float resoSmooth = 1.0f - std::exp (-1.0f / (0.020f * rate));
        const float noiseDecay = std::exp (-1.0f / (0.120f * rate));
        const int uniSize = static_cast<int> (unisonLineL.size());
        const float uniSizeF = static_cast<float> (uniSize);

        for (int i = 0; i < samples; ++i)
        {
            float l = pianoL[i];
            float r = pianoR[i];
            // 1. Gentle peak compression above about -18 dBFS.
            const float peak = std::max (std::abs (l), std::abs (r));
            if (peak > dynCompEnv)
                dynCompEnv += compAttack * (peak - dynCompEnv);
            else
                dynCompEnv += compRelease * (peak - dynCompEnv);
            if (dynCompEnv < 1.0e-7f)
                dynCompEnv = 0.0f;
            float gainTarget = 1.0f;
            if (compOn && dynCompEnv > kCompThreshold)
                gainTarget = (kCompThreshold + (dynCompEnv - kCompThreshold) / kCompRatio) / dynCompEnv;
            if (gainTarget < dynCompGain)
                dynCompGain += gainAttack * (gainTarget - dynCompGain);
            else
                dynCompGain += gainRelease * (gainTarget - dynCompGain);
            if (! compOn && dynCompGain > 1.0f - 1.0e-6f)
                dynCompGain = 1.0f;
            l *= dynCompGain;
            r *= dynCompGain;
            // 2. Detuned doubling. The lines stay live while bypassed so
            // enabling never replays stale audio.
            const float uniTarget = unisonOn ? kUnisonMix : 0.0f;
            unisonMix += uniSmooth * (uniTarget - unisonMix);
            if (! unisonOn && unisonMix < 1.0e-6f)
                unisonMix = 0.0f;
            const float wobble = static_cast<float> (std::sin (unisonPhase));
            unisonPhase += uniStep;
            if (unisonPhase >= core::kTwoPi)
                unisonPhase -= core::kTwoPi;
            if (uniSize > 1)
            {
                unisonLineL[static_cast<std::size_t> (unisonIndex)] = l;
                unisonLineR[static_cast<std::size_t> (unisonIndex)] = r;
                if (unisonMix > 0.0f)
                {
                    float delayedL = static_cast<float> (unisonIndex) - (uniBase + uniDepth * wobble);
                    float delayedR = static_cast<float> (unisonIndex) - (uniBase - uniDepth * wobble);
                    if (delayedL < 0.0f)
                        delayedL += uniSizeF;
                    if (delayedR < 0.0f)
                        delayedR += uniSizeF;
                    // Float addition can round a tiny negative wrapped index
                    // to exactly uniSizeF; fold that endpoint back to zero.
                    if (delayedL >= uniSizeF)
                        delayedL = 0.0f;
                    if (delayedR >= uniSizeF)
                        delayedR = 0.0f;
                    const int i0l = static_cast<int> (delayedL);
                    const int i0r = static_cast<int> (delayedR);
                    const int i1l = i0l + 1 >= uniSize ? 0 : i0l + 1;
                    const int i1r = i0r + 1 >= uniSize ? 0 : i0r + 1;
                    const float fL = delayedL - static_cast<float> (i0l);
                    const float fR = delayedR - static_cast<float> (i0r);
                    const auto& lineL = unisonLineL;
                    const auto& lineR = unisonLineR;
                    const float wetL = lineL[static_cast<std::size_t> (i0l)] * (1.0f - fL)
                        + lineL[static_cast<std::size_t> (i1l)] * fL;
                    const float wetR = lineR[static_cast<std::size_t> (i0r)] * (1.0f - fR)
                        + lineR[static_cast<std::size_t> (i1r)] * fR;
                    l += unisonMix * wetL;
                    r += unisonMix * wetR;
                }
                if (++unisonIndex >= uniSize)
                    unisonIndex = 0;
            }
            pianoL[i] = l;
            pianoR[i] = r;
        }
        // 3. Sympathetic resonance: the doubled bus feeds the tank, whose wet
        // output (mix 1.0, set in prepare) is mixed behind the bus scaled by
        // the resonance level and the damper. Feeding pauses once the mix has
        // settled at zero, so a bypassed bus costs nothing and stays exact.
        const float resoTarget = resoAmount * damper;
        if (resoTarget > 0.0f || resoMix > 0.0f)
        {
            float* wetL = resoLeft.data();
            float* wetR = resoRight.data();
            std::copy (pianoL, pianoL + samples, wetL);
            std::copy (pianoR, pianoR + samples, wetR);
            reso.process (wetL, wetR, samples);
            for (int i = 0; i < samples; ++i)
            {
                resoMix += resoSmooth * (resoTarget - resoMix);
                if (resoTarget == 0.0f && resoMix > 0.0f && resoMix < 1.0e-6f)
                {
                    resoMix = 0.0f;
                    reso.reset();
                }
                if (resoMix > 0.0f)
                {
                    pianoL[i] += resoMix * wetL[i];
                    pianoR[i] += resoMix * wetR[i];
                }
            }
        }
        // 4. Mechanical pedal noise: a per-sample decaying burst of uniform
        // noise on both edges of the pedal, scaled by the noise level.
        if (pedalNoiseEnv > 0.0f)
        {
            for (int i = 0; i < samples && pedalNoiseEnv > 0.0f; ++i)
            {
                if (noiseLevel > 0.0f)
                {
                    pedalNoiseState ^= pedalNoiseState << 13;
                    pedalNoiseState ^= pedalNoiseState >> 17;
                    pedalNoiseState ^= pedalNoiseState << 5;
                    const float burst = static_cast<float> (static_cast<std::int32_t> (pedalNoiseState))
                        * (1.0f / 2147483648.0f);
                    const float thump = pedalNoiseEnv * noiseLevel * 0.5f * burst;
                    pianoL[i] += thump;
                    pianoR[i] += thump;
                }
                pedalNoiseEnv *= noiseDecay;
                if (pedalNoiseEnv < 1.0e-7f)
                    pedalNoiseEnv = 0.0f;
            }
        }
        // 5. Level, smoothed per sample. Skipped at unity, so the bus is
        // untouched.
        if (level.isSmoothing() || level.getCurrent() != 1.0f)
            for (int i = 0; i < samples; ++i)
            {
                const float gain = level.next();
                pianoL[i] *= gain;
                pianoR[i] *= gain;
            }
    }
};

PianoLayer::Bank PianoLayer::loadGrandBank (const std::string& rootUtf8)
{
    if (rootUtf8.empty())
        return nullptr;
    sampler::LoadReport report;
    return sampler::SampleBank::load (sampler::instrumentDirectory (rootUtf8, kGrandFolder), report);
}

const char* PianoLayer::modelFolder (int model) noexcept
{
    const int which = model - kFirstKeys;
    return which >= 0 && which < kNumKeys ? kKeysFolder[static_cast<std::size_t> (which)] : nullptr;
}

PianoLayer::Bank PianoLayer::loadModelBank (const std::string& rootUtf8, int model)
{
    const char* folder = modelFolder (model);
    if (rootUtf8.empty() || folder == nullptr)
        return nullptr;
    sampler::LoadReport report;
    return sampler::SampleBank::load (sampler::instrumentDirectory (rootUtf8, folder), report);
}

PianoLayer::PianoLayer() : impl (std::make_unique<Impl>()) {}
PianoLayer::~PianoLayer() = default;

void PianoLayer::setGrandBank (Bank bank)
{
    impl->pendingBank = std::move (bank);
}

void PianoLayer::setModelBank (int model, Bank bank)
{
    const int which = model - kFirstKeys;
    if (which >= 0 && which < kNumKeys)
        impl->pendingKeysBanks[static_cast<std::size_t> (which)] = std::move (bank);
}

void PianoLayer::prepare (double rate, int maxBlockSize)
{
    auto& d = *impl;
    d.sampleRate = std::clamp (rate, 22050.0, 384000.0);
    d.scratchSize = std::max (1, maxBlockSize);
    const auto scratch = static_cast<std::size_t> (d.scratchSize);
    for (auto& voice : d.voices)
        voice.env.prepare (d.sampleRate);
    for (auto& voice : d.modelVoices)
        voice.prepare (d.sampleRate);
    d.grandBank = d.pendingBank;
    d.grandsLoaded = d.grandBank != nullptr;
    for (auto& grand : d.grands)
    {
        if (! d.grandsLoaded)
        {
            grand.reset();
            continue;
        }
        if (grand == nullptr)
            grand = std::make_unique<Impl::Grand>();
        grand->player.prepare ({ d.sampleRate, d.scratchSize });
        grand->player.setBank (d.grandBank);
    }
    for (std::size_t which = 0; which < d.keys.size(); ++which)
    {
        auto& player = d.keys[which];
        if (d.pendingKeysBanks[which] == nullptr)
        {
            player.reset();
            continue;
        }
        if (player == nullptr)
            player = std::make_unique<Impl::Grand>();
        player->player.prepare ({ d.sampleRate, d.scratchSize });
        player->player.setBank (d.pendingKeysBanks[which]);
    }
    d.keysLeft.assign (scratch, 0.0f);
    d.keysRight.assign (scratch, 0.0f);
    d.keysHighLeft.assign (scratch, 0.0f);
    d.keysHighRight.assign (scratch, 0.0f);
    d.grandLeft.assign (scratch, 0.0f);
    d.grandRight.assign (scratch, 0.0f);
    d.grandHighLeft.assign (scratch, 0.0f);
    d.grandHighRight.assign (scratch, 0.0f);
    d.grandTemp.assign (2 * scratch, 0.0f);
    d.grandLowCoefficient = static_cast<float> (std::exp (-core::kTwoPi * 1200.0 / d.sampleRate));
    for (auto& smoother : d.smoothers)
        smoother.prepare (d.sampleRate, 0.02);
    d.level.prepare (d.sampleRate, 0.02);
    d.pedalDepth.prepare (d.sampleRate, 0.02);
    d.pedalDepth.setImmediate (0.0f);
    const core::ProcessSetup fxSetup { d.sampleRate, d.scratchSize };
    const auto unisonLineSize = static_cast<std::size_t> (d.sampleRate * 0.02) + 64;
    d.unisonLineL.assign (unisonLineSize, 0.0f);
    d.unisonLineR.assign (unisonLineSize, 0.0f);
    d.resoLeft.assign (scratch, 0.0f);
    d.resoRight.assign (scratch, 0.0f);
    // Sympathetic resonance: a bright chamber behind the bus, mixed in per
    // sample by the damper state. Only the mix follows a parameter.
    d.reso.prepare (fxSetup);
    d.reso.setParameters ({ true, 5.0f, 0.7f, 2.8f, 0.6f, 0.8f, 1.0f });
    d.silence();
    d.envelopeUpdateCountdown = 0;
    d.setTargets();
    for (auto& smoother : d.smoothers)
        smoother.setImmediate (smoother.getTarget());
    d.level.setImmediate (d.level.getTarget());
    d.updateGrandParameters();
    d.updateEnvelopes();
}

void PianoLayer::reset() noexcept
{
    impl->silence();
}

void PianoLayer::setParameters (const Parameters& params) noexcept
{
    impl->params = sanitised (params);
    impl->setTargets();
    impl->updateGrandParameters();
}

void PianoLayer::noteOn (int key, float velocity) noexcept
{
    if (key < 0 || key > 127 || impl->scratchSize <= 0) // ignored before prepare()
        return;
    if (! (velocity > 0.0f))
    {
        noteOff (key);
        return;
    }
    const int note = key + 12 * impl->params.octave;
    if (note < 0 || note > 127)
        return;
    impl->keyNote[static_cast<std::size_t> (key)] = static_cast<std::int8_t> (note);
    impl->trigger (note, velocity);
}

void PianoLayer::noteOff (int key) noexcept
{
    if (key < 0 || key > 127)
        return;
    const int note = std::exchange (impl->keyNote[static_cast<std::size_t> (key)], std::int8_t { -1 });
    if (note >= 0 && impl->down[static_cast<std::size_t> (note)])
        impl->noteOff (note);
}

void PianoLayer::sustain (float value) noexcept
{
    impl->sustain (std::isfinite (value) ? value : 0.0f);
}

void PianoLayer::setPedalDepth (float amount01) noexcept
{
    const float depth = std::clamp (std::isfinite (amount01) ? amount01 : 0.0f, 0.0f, 1.0f);
    impl->pedalDepth.setTarget (depth);
    // Keep sustain() binary and unchanged for existing products. Partial depth extends
    // released model tails without latching keys until full pedal is reached.
    if (depth >= 0.999f)
        impl->sustain (1.0f);
    else
        impl->sustain (0.0f);
}

void PianoLayer::allNotesOff() noexcept
{
    impl->allNotesOff();
}

void PianoLayer::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    if (impl->scratchSize <= 0) // before prepare(): silence
    {
        std::fill (left, left + numSamples, 0.0f);
        std::fill (right, right + numSamples, 0.0f);
        return;
    }
    for (int pos = 0; pos < numSamples; pos += impl->scratchSize)
    {
        const int n = std::min (impl->scratchSize, numSamples - pos);
        renderVoices (left + pos, right + pos, n);
        processBus (left + pos, right + pos, n);
    }
}

void PianoLayer::renderVoices (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0 || impl->scratchSize <= 0)
        return;
    for (int pos = 0; pos < numSamples; pos += impl->scratchSize)
    {
        const int n = std::min (impl->scratchSize, numSamples - pos);
        impl->renderVoices (left + pos, right + pos, n);
    }
}

void PianoLayer::processBus (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0 || impl->scratchSize <= 0)
        return;
    for (int pos = 0; pos < numSamples; pos += impl->scratchSize)
    {
        const int n = std::min (impl->scratchSize, numSamples - pos);
        impl->processBus (left + pos, right + pos, n);
    }
}

bool PianoLayer::isSounding() const noexcept
{
    return impl->allocator.numSounding() > 0 || impl->grandLive || impl->keysLive || impl->grandsSounding();
}

int PianoLayer::activeVoiceCount() const noexcept
{
    return impl->allocator.numSounding() + impl->grandVoices();
}

bool PianoLayer::hasGrand() const noexcept
{
    return impl->grandsLoaded;
}

bool PianoLayer::samplesMissing() const noexcept
{
    const int current = impl->sound();
    return kModels[static_cast<std::size_t> (current)].source == Source::sampled && ! impl->sampledLoaded (current);
}
} // namespace kraftonic::piano
