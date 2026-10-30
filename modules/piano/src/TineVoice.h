// SPDX-License-Identifier: MIT
#pragma once

// Internal to modules/piano: the Tine EP voice (a two-operator FM body plus an
// inharmonic tine partial). Moved unchanged from a product's first engine,
// so that product's default sound renders bit for bit.
// Real-time: allocation-free and noexcept.

#include <kraftonic/core/Adsr.h>
#include <kraftonic/core/Math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kraftonic::piano::detail
{
inline constexpr double kBodyRatio = 1.003;
inline constexpr double kTineRatio = 6.81;
inline constexpr float kVoiceGain = 0.43f;
inline constexpr float kBrightTrim = 1.0f; // Bright Tine level against the Tine EP

/** The hammer and pickup the voice models. The standard Tine keeps the
    original constants; Bright changes the strike and the pickup, not the
    controls (see startBright()). */
enum class TineVariant : std::uint8_t
{
    standard,
    bright // hard hammer, pickup close to the tine: asymmetric, barking
};

struct TineVoice
{
    core::Adsr env;
    double carrierPhase = 0.0;
    double modPhase = 0.0;
    double tinePhase = 0.0;
    double carrierStep = 0.0;
    double modStep = 0.0;
    double tineStep = 0.0;
    float velocity = 1.0f;
    float strikeLevel = 1.0f;
    float strikeBrightness = 1.0f;
    float keyBrightness = 1.0f;
    float decayScale = 1.0f;
    float fmIndexLimit = 0.0f;
    float indexEnvelope = 1.0f;
    float indexDecay = 1.0f;
    float tineLevel = 0.0f;
    float tineDecay = 0.0f;
    TineVariant variant = TineVariant::standard;
    float indexScale = 1.0f;  // Bright: FM depth of the hammer
    float asymmetry = 0.0f;   // Bright: the pickup's even-order term
    float variantGain = 1.0f; // Bright: level trim, eased at fortissimo
    float lastOutput = 0.0f;
    float retriggerOffset = 0.0f;
    float retriggerMultiplier = 0.0f;
    int retriggerSamples = 0;
    double driftPhase = 0.0;
    double driftStep = 0.0;
    double detuneRatio = 1.0;

    void start (int note, float newVelocity, double sampleRate, float sensitivity,
                TineVariant newVariant = TineVariant::standard) noexcept
    {
        // Starting the new oscillators at zero leaves an old sample behind
        // on a stolen/retriggered slot. Decay that exact difference over
        // five milliseconds, including when a slot is stolen repeatedly.
        retriggerOffset = lastOutput;
        retriggerSamples = std::max (1, static_cast<int> (sampleRate * 0.005));
        retriggerMultiplier = std::exp (-6.0f / static_cast<float> (retriggerSamples));
        env.reset();
        env.noteOn();
        carrierPhase = modPhase = tinePhase = 0.0;
        velocity = std::clamp (newVelocity, 0.0f, 1.0f);
        // A power law keeps the soft end expressive without bunching ff
        // notes together. At the default sensitivity, MIDI 25 to 127
        // spans about 33 dB before the strike spectrum is considered.
        const float midLift = 1.60f * sensitivity * velocity * velocity * velocity * velocity
            * (1.0f - velocity);
        strikeLevel = std::pow (std::max (velocity, 0.001f), 3.35f * sensitivity) + midLift;
        strikeBrightness = 1.0f - sensitivity
            + sensitivity * (0.10f + 0.90f * std::sqrt (velocity));
        const float softness = 1.0f - strikeBrightness;
        indexEnvelope = 1.0f;
        const double f = core::midiNoteToHz (note);
        carrierStep = f / sampleRate;
        modStep = kBodyRatio * carrierStep;
        tineStep = kTineRatio * carrierStep;
        const float high = std::max (0.0f, static_cast<float> (note - 60) / 48.0f);
        const float low = std::max (0.0f, static_cast<float> (60 - note) / 60.0f);
        keyBrightness = (1.0f - 0.27f * low) * (1.0f + 0.12f * high) / (1.0f + 0.4f * high);
        decayScale = (1.0f + 0.55f * low) / (1.0f + 1.7f * high);
        // Deterministic, sub-cent tuning differences and slow drift keep
        // stacked notes from sounding like one rigid oscillator.
        const int hash = (note * 37 + 17) % 23;
        detuneRatio = std::exp2 ((static_cast<double> (hash - 11) * 0.045) / 1200.0);
        driftPhase = static_cast<double> ((note * 13) % 29) / 29.0;
        driftStep = (0.19 + 0.017 * static_cast<double> (note % 7)) / sampleRate;
        // The FM strike mellows over 0.3-1.2 s. High and soft notes
        // settle faster; velocitySensitivity=0 also removes the soft
        // note dependence from this envelope.
        const double indexSeconds = 1.15 / (1.0 + 1.1 * high + 0.6 * softness);
        indexDecay = static_cast<float> (std::exp (-1.0 / (indexSeconds * sampleRate)));

        // Carson bandwidth: fc + (I + 1) fm <= 0.40 fs. The 0.40
        // fs limit leaves a guard band below Nyquist for the small
        // residual Bessel sidebands beyond Carson's approximate width.
        fmIndexLimit = static_cast<float> (std::max (0.0, (0.40 * sampleRate - f) / (kBodyRatio * f) - 1.0));
        // The inharmonic attack partial is omitted before it can alias.
        const double partial = kTineRatio * f;
        tineLevel = static_cast<float> (std::clamp ((0.45 * sampleRate - partial) / (0.05 * sampleRate), 0.0, 1.0));
        tineDecay = static_cast<float> (std::exp (-1.0 / (0.085 * sampleRate)));
        variant = newVariant;
        if (variant == TineVariant::bright)
            startBright (sampleRate, sensitivity);
    }

    void startBright (double sampleRate, float sensitivity) noexcept
    {
        // Hard hammer and a pickup set close to the tine. The FM strike is
        // narrower than the standard Tine's; the bark comes from the pickup,
        // whose field is asymmetric about the tine: an even-order term that
        // grows with how far the tine swings (velocity). Its sign makes the
        // pickup's second harmonic add to the strike's rather than cancel it.
        const float drive = sensitivity * std::pow (velocity, 1.5f) + (1.0f - sensitivity) * 0.5f;
        indexScale = 0.15f;
        asymmetry = -1.10f * drive;
        // The hard hammer rings the tone bar louder and longer.
        tineLevel *= 1.8f;
        tineDecay = static_cast<float> (std::exp (-1.0 / (0.19 * sampleRate)));
        // Above MIDI velocity 100 the level eases off, as the model voices'
        // does, so a dense fortissimo chord keeps its headroom.
        const float forte = std::clamp ((velocity - 100.0f / 127.0f) / (1.0f - 100.0f / 127.0f), 0.0f, 1.0f);
        variantGain = kBrightTrim * (1.0f - 0.65f * forte);
    }

    float next (float tone, float bell, float sensitivity) noexcept
    {
        if (! env.isActive() && retriggerSamples == 0)
        {
            lastOutput = 0.0f;
            return 0.0f;
        }
        (void) sensitivity;
        const float level = strikeLevel;
        const float brightness = strikeBrightness;
        const float indexCurve = 0.32f + 0.68f * indexEnvelope;
        float desiredIndex = (0.10f + 4.0f * tone) * brightness * keyBrightness * indexCurve;
        if (variant != TineVariant::standard)
            desiredIndex *= indexScale;
        const float index = std::min (desiredIndex, fmIndexLimit);
        const float envelope = env.process();
        float signal = 0.0f;
        if (carrierStep < 0.45)
        {
            const double phase = carrierPhase + static_cast<double> (index) * std::sin (core::kTwoPi * modPhase) / core::kTwoPi;
            signal = static_cast<float> (std::sin (core::kTwoPi * phase)) * envelope / (1.0f + 0.08f * index);
            signal += 0.30f * bell * tineLevel * (0.65f + 0.35f * brightness)
                * (1.0f + 0.20f * std::max (0.0f, keyBrightness - 0.75f))
                * static_cast<float> (std::sin (core::kTwoPi * tinePhase)) * envelope;
        }
        if (variant != TineVariant::standard)
        {
            if (asymmetry != 0.0f && carrierStep < 0.45)
            {
                // y = s + a (s^2 - mean(s^2)): the FM body is a unit sine
                // under the envelope, so its mean square is known exactly and
                // removed here (no offset, no thump as the note decays).
                const float body = envelope / (1.0f + 0.08f * index);
                const float bar = 0.30f * bell * tineLevel * (0.65f + 0.35f * brightness)
                    * (1.0f + 0.20f * std::max (0.0f, keyBrightness - 0.75f)) * envelope;
                // Tone also sets how hard the tine drives the pickup (1 at the
                // default 0.5), so the control stays live on this voice.
                const float drive = asymmetry * (0.4f + 1.2f * tone);
                signal += drive * (signal * signal - 0.5f * (body * body + bar * bar));
            }
            signal *= variantGain;
        }
        signal *= kVoiceGain * level;
        // Tine energy dies much sooner than the body envelope.
        tineLevel *= tineDecay;
        indexEnvelope *= indexDecay;
        if (indexEnvelope < 1.0e-6f)
            indexEnvelope = 0.0f;
        const double drift = 1.0 + 0.00009 * (1.0 - 4.0 * std::abs (driftPhase - 0.5));
        driftPhase += driftStep;
        if (driftPhase >= 1.0)
            driftPhase -= 1.0;
        carrierPhase = core::wrapPhase (carrierPhase + carrierStep * detuneRatio * drift);
        modPhase = core::wrapPhase (modPhase + modStep * detuneRatio * drift);
        tinePhase = core::wrapPhase (tinePhase + tineStep);
        if (retriggerSamples > 0)
        {
            signal += retriggerOffset;
            retriggerOffset *= retriggerMultiplier;
            if (--retriggerSamples == 0)
                retriggerOffset = 0.0f;
        }
        lastOutput = signal;
        return signal;
    }
};
} // namespace kraftonic::piano::detail
