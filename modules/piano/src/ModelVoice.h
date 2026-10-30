// SPDX-License-Identifier: MIT
#pragma once

// Internal to modules/piano: the modelled piano sounds other than the Tine EP
// (TineVoice.h). Moved unchanged from a product engine, so it renders bit for bit.
//
//   Reed EP         struck steel reed; a pickup that responds non-linearly to
//                   the reed's swing, so hard notes bark and buzz
//   Electric Grand  short struck strings, two per key, read by a bright
//                   under-bridge pickup: chiming, chorus-friendly, short sustain
//   Clav            a string struck from below and read by two pickups, with a
//                   yarn damper that mutes it (and thunks) when the key lifts
//   Grand stand-in  the struck-string model voiced as an acoustic grand; plays
//                   (quieter) when the sampled grand's data is missing
//   Celesta         struck steel bars over resonators
//   Harpsichord,    stand-ins for the sampled Harpsichord (the clav string,
//   Vibes, Marimba  plucked and read at the bridge) and the sampled mallets
//                   (tuned bars, partials at 1, 4 and about 10 times the
//                   fundamental); quieter, like the grand stand-ins
//
// One ModelVoice object plays any of these; the model is chosen per note at
// note-on, so changing the section's sound never touches sounding notes.
// Real-time: everything except prepare() is allocation-free and noexcept.

#include <kraftonic/core/Adsr.h>

#include <array>
#include <cstdint>
#include <vector>

namespace kraftonic::piano::detail
{
enum class Model : std::uint8_t
{
    reed,
    electricGrand,
    clav,
    brightGrand, // stand-in for the sampled Bright Grand
    studioGrand, // stand-in for the sampled Studio Grand (warmer, wider)
    brightTine,  // played by TineVoice; listed so the indices line up
    hardReed,    // reed with a strongly asymmetric pickup
    glassReed,   // plucked reed
    brightClav,  // clav with its two pickups out of phase
    celesta,     // struck steel bars over resonators
    harpsichord, // stand-in for the sampled Harpsichord: a plucked string
    vibes,       // stand-in for the sampled Vibes: tuned metal bars, long ring
    marimba      // stand-in for the sampled Marimba: tuned wooden bars, short ring
};

/** Live section controls, read every sample (already smoothed). */
struct Controls
{
    float tone = 0.5f;        // 0 dark .. 1 bright
    float bell = 0.35f;       // strength of the struck attack component
    float sensitivity = 0.7f; // velocity sensitivity
};

/** Level trims that bring each model to about the Tine EP's loudness for the
    same chord (measured in the original product's sound tests). */
float modelTrim (Model model) noexcept;

/** Extra gain for a stand-in that plays because a sampled sound's data is
    missing (-9 dB): clearly quieter, so nobody mistakes it for the samples. */
inline constexpr float kStandInGain = 0.354813f;

class ModelVoice
{
public:
    /** NOT real-time: sizes the clav string's delay line for `sampleRate`. */
    void prepare (double sampleRate);

    /** Starts a note. `carryLeft/Right` is the slot's previous output sample
        (stereo, as it was mixed), faded out over 5 ms so a stolen or re-struck
        slot never jumps. `gain` scales the voice (the engine uses it for the
        quieter missing-samples stand-in). */
    void start (Model model, int note, float velocity, const Controls& controls, float carryLeft,
                float carryRight, float gain = 1.0f) noexcept;

    /** The slot is taken by a Tine EP note: stop this voice, fading only its
        last output sample out over 5 ms (where it was panned). */
    void fadeOut() noexcept;

    /** Key released (or pedal let go): envelope release; the clav's damper. */
    void release (float releaseSeconds) noexcept;

    /** Hard stop; only when the output is not audible. */
    void reset() noexcept;

    /** Adds one sample of this voice (panned) to left/right. */
    void next (const Controls& controls, float& left, float& right) noexcept;

    bool finished() const noexcept;
    /** Producing output: playing, or fading out a carried sample. */
    bool sounding() const noexcept { return env.isActive() || retriggerSamples > 0; }
    float lastLeft() const noexcept { return previousLeft; }
    float lastRight() const noexcept { return previousRight; }

    /** Per-model envelope scaling applied by the engine to the ADSR times. */
    float decayScale() const noexcept { return envDecayScale; }
    float releaseScale() const noexcept { return envReleaseScale; }

    core::Adsr env;

private:
    static constexpr int kMaxPartials = 16;

    void startReed (int note, const Controls& controls) noexcept;
    void startGlassReed (int note, const Controls& controls) noexcept;
    void startStrings (int note, const Controls& controls) noexcept;
    void startClav (int note, const Controls& controls) noexcept;
    void startBars (int note, const Controls& controls) noexcept;
    void updateWeights (float tone) noexcept;
    float nextReed (const Controls& controls, float envelope) noexcept;
    float nextStrings (float envelope) noexcept;
    float nextClav (const Controls& controls, float envelope) noexcept;
    float noise() noexcept;

    double sampleRate = 48000.0;
    Model model = Model::reed;
    int noteNumber = 60;
    float velocity = 1.0f;
    float level = 1.0f;     // velocity loudness, model trim and stand-in gain
    float hardness = 1.0f;  // velocity brightness 0..1
    float envDecayScale = 1.0f;
    float envReleaseScale = 1.0f;
    float voicePan = 0.0f;
    float previousLeft = 0.0f, previousRight = 0.0f;
    float carryLeftLevel = 0.0f, carryRightLevel = 0.0f;
    float retriggerOffset = 0.0f;
    float retriggerMultiplier = 0.0f;
    int retriggerSamples = 0;
    std::uint32_t random = 1;
    bool released = false;
    int quietSamples = 0;
    int age = 0;
    int onsetSamples = 1;

    // Shared attack noise ("knock"): one-pole-filtered noise with a fast decay.
    float knockLevel = 0.0f;
    float knockDecay = 0.0f;
    float knockState = 0.0f;
    float knockCoefficient = 0.0f;
    float knockRampStep = 0.0f;

    // Reed: two cantilever modes into a non-linear pickup.
    double reedPhase = 0.0, reedStep = 0.0;
    double modePhase = 0.0, modeStep = 0.0;
    float modeLevel = 0.0f, modeDecay = 0.0f;
    float evenBoost = 0.0f;  // Hard Reed: extra even-order pickup term
    // Glass Reed: the felt tick of the pluck and a faster overall decay.
    float pluckLevel = 0.0f, pluckDecay = 0.0f, pluckCoefficient = 0.0f;
    float pluckState = 0.0f, pluckLow = 0.0f, pluckLowCoefficient = 0.0f;
    float glassDecay = 1.0f, glassDecayCoefficient = 1.0f;
    float swing = 0.5f;     // reed excursion at the strike (drives the bark)
    float pickupCurve = 1.0f; // key-dependent limit on the non-linearity
    float dcState = 0.0f, dcInput = 0.0f, dcCoefficient = 0.0f;

    // Struck strings: two strings per key, partials as decaying phasors.
    int numPartials = 0;
    std::array<double, kMaxPartials> re1 {}, im1 {}, cr1 {}, ci1 {};
    std::array<double, kMaxPartials> re2 {}, im2 {}, cr2 {}, ci2 {};
    std::array<float, kMaxPartials> partialHz {};
    std::array<float, kMaxPartials> weights {};
    float tiltBase = 1.0f;

    // Clav: waveguide string, damper and pickups.
    std::vector<float> line;
    int lineSize = 0;
    int writeIndex = 0;
    int loopDelay = 1;
    int pickupNear = 1, pickupFar = 2;
    int pickupShift = 0; // Bright Clav: lines the near pickup up with the far one
    float pickupNearGain = 0.65f, pickupFarGain = 0.35f;
    float lowpassA = 0.75f, lowpassB = 0.25f, lowpassState = 0.0f;
    float allpassCoefficient = 0.0f, allpassIn = 0.0f, allpassOut = 0.0f;
    float loopGain = 0.99f, loopGainTarget = 0.99f, dampedGain = 0.9f, gainSlew = 0.0f;
    int exciteSamples = 0, exciteLength = 1, exciteAt = 0;
    float exciteLevel = 0.0f;
    int thunkSamples = 0, thunkLength = 1;
    float thunkLevel = 0.0f;
    double thudPhase = 0.0, thudStep = 0.0;
    float thudLevel = 0.0f, thudDecay = 0.0f;
    float toneState = 0.0f;
    float toneCoefficient = 0.0f;
};
} // namespace kraftonic::piano::detail
