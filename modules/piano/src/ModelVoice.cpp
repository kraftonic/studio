// SPDX-License-Identifier: MIT
#include "ModelVoice.h"

#include <kraftonic/core/Math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kraftonic::piano::detail
{
namespace
{
    constexpr float kQuietLevel = 3.0e-6f; // about -110 dBFS
    constexpr int kQuietSamples = 2048;

    // Bright Clav: pickup positions (fractions of the loop), the balance of
    // the out-of-phase pair (1 cancels the fundamental completely; a little
    // less keeps the pitch audible) and make-up gain for the cancellation.
    constexpr double kBrightNear = 0.06;
    constexpr double kBrightFar = 0.16;
    constexpr float kBrightBalance = 0.95f;
    constexpr float kBrightPickupGain = 3.0f;

    // Harpsichord stand-in: where the plectrum plucks (a fraction of the
    // loop: the comb that leaves out every ninth harmonic or so), and the
    // jack's fall on release, a low thud stopped by the felt.
    constexpr double kPluckPoint = 0.11;
    constexpr double kThumpHz = 100.0;
    constexpr float kThumpLevel = 0.12f;

    float keyHigh (int note) noexcept { return std::max (0.0f, static_cast<float> (note - 60) / 48.0f); }
    float keyLow (int note) noexcept { return std::max (0.0f, static_cast<float> (60 - note) / 60.0f); }

    /** Deterministic sub-cent detune per key, like the Tine EP's. */
    double keyDetune (int note, double cents) noexcept
    {
        const int hash = (note * 37 + 17) % 23;
        return std::exp2 ((static_cast<double> (hash - 11) / 11.0) * cents / 1200.0);
    }

    float decayCoefficient (double seconds, double sampleRate) noexcept
    {
        return static_cast<float> (std::exp (-1.0 / (std::max (1.0e-4, seconds) * sampleRate)));
    }

    /** Per-sample magnitude for a partial that falls 60 dB in `t60` seconds. */
    double t60Radius (double t60, double sampleRate) noexcept
    {
        return std::exp (-6.907755278982137 / (std::max (0.01, t60) * sampleRate));
    }

    struct StringVoicing
    {
        int partials;
        double stiffness;     // inharmonicity B at C3, doubles every `stiffnessOctaves`
        double stiffnessOctaves;
        double hammerPoint;   // strike position as a fraction of the string
        double rolloff;       // spectral slope for a soft strike
        double hardRolloff;   // ... and for the hardest strike
        double pickupBump;    // under-bridge pickup presence around 2.6 kHz
        double t60Low;        // fundamental T60 at MIDI 36
        double t60Octaves;    // T60 halves every this many octaves up
        double t60Min, t60Max;
        double partialDamping; // extra damping per partial number
        double highDamping;    // extra damping by frequency (per (f / 5 kHz)^2)
        double secondLevel;    // the second, slower string ("aftersound")
        double secondT60;
        double detuneCents;
        float knock;
        float knockHz;
        float panWidth;
        float tiltOffset;
        float decayScale;
        float releaseScale;
    };

    // clang-format off
    constexpr StringVoicing kElectricGrand {
        12, 1.6e-4, 1.5, 1.0 / 9.0, 2.05, 1.05, 0.7,
        7.0, 2.0, 0.45, 5.0, 0.10, 0.6, 0.5, 1.9, 1.6,
        0.30f, 4200.0f, 0.15f, 0.0f, 0.8f, 0.7f };
    constexpr StringVoicing kBrightGrand {
        16, 0.7e-4, 1.33, 1.0 / 7.3, 2.3, 1.25, 0.0,
        11.0, 1.45, 0.7, 14.0, 0.07, 0.35, 0.6, 2.6, 1.0,
        0.14f, 1700.0f, 0.35f, 0.0f, 1.6f, 0.9f };
    constexpr StringVoicing kStudioGrand {
        16, 0.7e-4, 1.33, 1.0 / 7.3, 2.65, 1.55, 0.0,
        12.0, 1.45, 0.75, 15.0, 0.08, 0.45, 0.65, 2.8, 1.2,
        0.10f, 1200.0f, 0.5f, -0.25f, 1.7f, 1.0f };
    // clang-format on

    // Struck bars over resonators. A mode's level is soft + (hard - soft)
    // times the strike hardness; the fundamental's T60 is
    // t60Scale 2^(-(note - t60Note) / t60Span), clamped to [t60Min, t60Max],
    // and mode n (from 0) rings 1 + modeDamping n times shorter.
    struct BarVoicing
    {
        int modes;
        std::array<double, 4> ratios;
        std::array<double, 4> soft;
        std::array<double, 4> slope; // hard minus soft
        double t60Scale;
        int t60Note;
        double t60Span;
        double t60Min, t60Max;
        double modeDamping;
        float knock;
        double knockSeconds;
        double knockHz;
        float decayScale;
        float releaseScale;
        float panWidth;
    };

    // clang-format off
    // A free steel bar: modes at 1, 2.76, 5.40 and 8.93 times the fundamental.
    constexpr BarVoicing kCelestaBars {
        4, { 1.0, 2.756, 5.404, 8.933 }, { 1.0, 0.12, 0.036, 0.012 }, { 0.0, 0.24, 0.13, 0.06 },
        4.0, 48, 30.0, 1.0, 3.0, 2.5, 0.05f, 0.003, 3000.0, 3.0f, 0.6f, 0.10f };
    // Tuned metal bars (the arch under the bar puts the second mode at 4x and
    // the third near 10x); the tube's motor is left to the product's tremolo. A
    // soft mallet, a long ring: 8 s at the bottom, falling to 4 s.
    constexpr BarVoicing kVibesBars {
        3, { 1.0, 4.0, 9.92, 0.0 }, { 1.0, 0.05, 0.008, 0.0 }, { 0.0, 0.17, 0.05, 0.0 },
        8.0, 53, 24.0, 4.0, 8.0, 4.0, 0.035f, 0.003, 3500.0, 3.0f, 0.5f, 0.12f };
    // Tuned wooden bars (second mode at 4x, third near 9.8x): a hard knock
    // from the mallet on wood and a short ring, 2 s at the bottom, 0.4 s at
    // the top.
    constexpr BarVoicing kMarimbaBars {
        3, { 1.0, 4.0, 9.8, 0.0 }, { 1.0, 0.10, 0.012, 0.0 }, { 0.0, 0.25, 0.06, 0.0 },
        2.0, 45, 24.0, 0.4, 2.0, 5.0, 0.14f, 0.004, 2200.0, 3.0f, 0.5f, 0.12f };
    // clang-format on

    const BarVoicing& bars (Model model) noexcept
    {
        switch (model)
        {
            case Model::vibes: return kVibesBars;
            case Model::marimba: return kMarimbaBars;
            default: return kCelestaBars;
        }
    }

    const StringVoicing& voicing (Model model) noexcept
    {
        switch (model)
        {
            case Model::brightGrand: return kBrightGrand;
            case Model::studioGrand: return kStudioGrand;
            default: return kElectricGrand;
        }
    }
} // namespace

float modelTrim (Model model) noexcept
{
    // Calibrated against the Tine EP with a C-major triad at velocity 100
    // and default parameters: attack RMS (20-400 ms) within about 4 dB of the
    // Tine EP and peaks at or below -4 dBFS. The models have a higher crest
    // factor than the near-sinusoidal Tine, so headroom sets several trims;
    // the brighter models still sound as loud by ear.
    switch (model)
    {
        case Model::reed: return 0.426f;
        case Model::electricGrand: return 0.345f;
        case Model::clav: return 0.74f;
        case Model::brightGrand: return 0.369f;
        case Model::studioGrand: return 0.415f;
        case Model::brightTine: return 1.0f; // the Tine voice trims it
        case Model::hardReed: return 0.343f;
        case Model::glassReed: return 0.49f;
        case Model::brightClav: return 0.665f;
        case Model::celesta: return 0.551f;
        case Model::harpsichord: return 0.94f;
        case Model::vibes: return 0.556f;
        case Model::marimba: return 0.95f;
    }
    return 1.0f;
}

void ModelVoice::prepare (double rate)
{
    sampleRate = rate;
    env.prepare (rate);
    // The clav string's delay line holds one period of the lowest pitch it
    // plays (16 Hz; lower notes are clamped there) plus interpolation room.
    lineSize = static_cast<int> (std::ceil (rate / 16.0)) + 8;
    line.assign (static_cast<std::size_t> (lineSize), 0.0f);
    reset();
}

void ModelVoice::reset() noexcept
{
    env.reset();
    previousLeft = previousRight = 0.0f;
    carryLeftLevel = carryRightLevel = 0.0f;
    retriggerOffset = 0.0f;
    retriggerSamples = 0;
    released = false;
    quietSamples = 0;
    knockLevel = 0.0f;
    pluckLevel = 0.0f;
    numPartials = 0;
    exciteSamples = 0;
    thunkSamples = 0;
    thudLevel = 0.0f;
    std::fill (line.begin(), line.end(), 0.0f);
}

float ModelVoice::noise() noexcept
{
    random = random * 1664525U + 1013904223U;
    return static_cast<float> (static_cast<std::int32_t> (random)) * (1.0f / 2147483648.0f);
}

void ModelVoice::fadeOut() noexcept
{
    env.reset();
    carryLeftLevel = previousLeft;
    carryRightLevel = previousRight;
    retriggerOffset = 1.0f;
    retriggerSamples = std::max (1, static_cast<int> (sampleRate * 0.005));
    retriggerMultiplier = std::exp (-6.0f / static_cast<float> (retriggerSamples));
}

void ModelVoice::start (Model newModel, int note, float newVelocity, const Controls& controls, float carryLeft,
                        float carryRight, float gain) noexcept
{
    // The carried sample keeps its own stereo position while it fades.
    carryLeftLevel = carryLeft;
    carryRightLevel = carryRight;
    retriggerOffset = 1.0f;
    retriggerSamples = std::max (1, static_cast<int> (sampleRate * 0.005));
    retriggerMultiplier = std::exp (-6.0f / static_cast<float> (retriggerSamples));
    knockRampStep = static_cast<float> (1.0 / (0.0005 * sampleRate));
    onsetSamples = std::max (1, static_cast<int> (0.0007 * sampleRate));
    model = newModel;
    noteNumber = std::clamp (note, 0, 127);
    velocity = std::clamp (newVelocity, 0.0f, 1.0f);
    random = 0x9e3779b9U ^ static_cast<std::uint32_t> (noteNumber * 7919);
    const float sensitivity = std::clamp (controls.sensitivity, 0.0f, 1.0f);
    // Preserve normal playing level, then ease the loudest strikes down so a
    // dense chord retains headroom. The knee starts above MIDI velocity 100;
    // a 10-note fortissimo chord measures at least 3 dB below full scale.
    const float forte = std::clamp ((velocity - 100.0f / 127.0f) / (1.0f - 100.0f / 127.0f), 0.0f, 1.0f);
    // The harpsichord plays at an almost even level, so it needs no knee.
    const float headroom = newModel == Model::harpsichord ? 1.0f : 1.0f - 0.65f * forte;
    // The harpsichord's plectrum lets go at the same point however hard the
    // key is pressed: only a little level follows velocity.
    const float velocityExponent = newModel == Model::harpsichord ? 0.35f : 2.6f;
    level = std::pow (std::max (velocity, 0.001f), velocityExponent * sensitivity) * modelTrim (newModel) * gain * headroom;
    hardness = 1.0f - sensitivity + sensitivity * (0.10f + 0.90f * std::sqrt (velocity));
    released = false;
    quietSamples = 0;
    age = 0;
    knockState = 0.0f;
    exciteSamples = 0;
    thunkSamples = 0;
    thudLevel = 0.0f;
    numPartials = 0;
    env.reset();
    env.noteOn();
    switch (model)
    {
        case Model::reed:
        case Model::hardReed: startReed (noteNumber, controls); break;
        case Model::glassReed: startGlassReed (noteNumber, controls); break;
        case Model::clav:
        case Model::brightClav:
        case Model::harpsichord: startClav (noteNumber, controls); break;
        case Model::electricGrand:
        case Model::brightGrand:
        case Model::studioGrand: startStrings (noteNumber, controls); break;
        case Model::celesta:
        case Model::vibes:
        case Model::marimba: startBars (noteNumber, controls); break;
        case Model::brightTine: break; // played by the Tine voice (PianoLayer)
    }
}

void ModelVoice::release (float releaseSeconds) noexcept
{
    if (released)
        return;
    released = true;
    env.noteOff();
    if ((model == Model::clav || model == Model::brightClav) && numPartials == 0)
    {
        // The yarn damper lands on the string: the loop gain drops within a
        // few milliseconds (a longer Release lets it ring a little longer).
        const double f = sampleRate / static_cast<double> (std::max (1, loopDelay));
        const double t60 = 0.06 + 0.12 * std::clamp (static_cast<double> (releaseSeconds), 0.0, 5.0);
        dampedGain = static_cast<float> (std::pow (10.0, -3.0 / (t60 * f)));
        loopGainTarget = std::min (loopGainTarget, dampedGain);
        // The string leaves the striker: a small kick back into the string
        // plus a short low thud from the key and damper.
        thunkLength = std::max (2, static_cast<int> (sampleRate * 0.0012));
        thunkSamples = thunkLength;
        thunkLevel = -0.22f * (0.4f + 0.6f * hardness);
        thudStep = 85.0 / sampleRate;
        thudPhase = 0.0;
        thudLevel = 0.05f;
        thudDecay = decayCoefficient (0.022, sampleRate);
    }
    else if (model == Model::harpsichord)
    {
        // The jack falls: the felt damper stops the string within about
        // 0.1 s, and the jack lands on the rail with a low thud.
        const double f = sampleRate / static_cast<double> (std::max (1, loopDelay));
        const double t60 = 0.08 + 0.10 * std::clamp (static_cast<double> (releaseSeconds), 0.0, 5.0);
        dampedGain = static_cast<float> (std::pow (10.0, -3.0 / (t60 * f)));
        loopGainTarget = std::min (loopGainTarget, dampedGain);
        thudStep = kThumpHz / sampleRate;
        thudPhase = 0.0;
        thudLevel = kThumpLevel;
        thudDecay = decayCoefficient (0.025, sampleRate);
    }
}

bool ModelVoice::finished() const noexcept
{
    if (retriggerSamples > 0)
        return false;
    if (! env.isActive())
        return true;
    return released && quietSamples >= kQuietSamples;
}

// ---- Reed EP -----------------------------------------------------------------

void ModelVoice::startReed (int note, const Controls& controls) noexcept
{
    const double f = core::midiNoteToHz (note) * keyDetune (note, 1.2);
    reedPhase = modePhase = 0.0;
    reedStep = f / sampleRate;
    // A clamped-free bar's second mode sits at 6.27x the first: the short
    // metallic "ping" in the attack. Omitted before it could alias.
    modeStep = 6.27 * reedStep;
    modeLevel = static_cast<float> (std::clamp ((0.45 - modeStep) / 0.05, 0.0, 1.0))
        * 0.30f * (0.35f + 0.65f * hardness);
    modeDecay = decayCoefficient (0.020, sampleRate);
    const float high = keyHigh (note);
    const float low = keyLow (note);
    // How far the reed swings toward the pickup. Hard notes swing further
    // into the pickup's non-linear range, which is where the bark comes from.
    // Follows velocity directly (not the compressed `hardness`), so mezzo
    // notes stay round and only the top of the range really barks.
    const float sensitivity = std::clamp (controls.sensitivity, 0.0f, 1.0f);
    const float drive = sensitivity * std::pow (velocity, 2.5f) + (1.0f - sensitivity) * 0.5f;
    swing = (0.06f + 0.66f * drive) / (1.0f + 0.7f * high);
    // Hard Reed: the reed sits closer to an asymmetric pickup electrode, so
    // the curve's even-order term is several times stronger (the bark).
    evenBoost = model == Model::hardReed ? 2.4f : 0.0f;
    // The pickup curve is a fifth-order polynomial: keep its top harmonic
    // below 0.45 fs by fading the curve out over the top of the range.
    pickupCurve = static_cast<float> (std::clamp ((0.09 * sampleRate - f) / (0.04 * sampleRate), 0.0, 1.0));
    dcState = dcInput = 0.0f;
    dcCoefficient = static_cast<float> (std::exp (-core::kTwoPi * 12.0 / sampleRate));
    knockLevel = 0.10f * hardness;
    knockDecay = decayCoefficient (0.0025, sampleRate);
    knockCoefficient = static_cast<float> (std::exp (-core::kTwoPi * 2500.0 / sampleRate));
    envDecayScale = 0.8f * (1.0f + 0.5f * low) / (1.0f + 1.6f * high);
    envReleaseScale = 0.8f;
    voicePan = 0.08f * static_cast<float> (note - 64) / 64.0f;
}

void ModelVoice::startGlassReed (int note, const Controls& controls) noexcept
{
    startReed (note, controls);
    // Plucked, not struck: a felt tip lifts the reed and lets it go, with a
    // small, even swing at every velocity: clear, with little bark. There is
    // no steel-on-steel strike, so the bar mode stays faint; the felt leaves
    // a short, bright tick (2-8 kHz) instead of the struck reed's knock.
    swing = 0.16f + 0.10f * hardness;
    modeLevel *= 0.35f;
    knockLevel = 0.0f;
    pluckLevel = 1.3f * (0.4f + 0.6f * hardness);
    pluckDecay = decayCoefficient (0.004, sampleRate);
    pluckCoefficient = static_cast<float> (std::exp (-core::kTwoPi * 2000.0 / sampleRate));
    pluckLowCoefficient = static_cast<float> (std::exp (-core::kTwoPi * std::min (8000.0, 0.2 * sampleRate) / sampleRate));
    pluckState = pluckLow = 0.0f;
    // A shorter, more even decay than the struck reed's.
    envDecayScale = 0.60f;
    envReleaseScale = 0.45f;
    glassDecay = 1.0f;
    glassDecayCoefficient = decayCoefficient (2.5, sampleRate);
}

float ModelVoice::nextReed (const Controls& controls, float envelope) noexcept
{
    const float shape = static_cast<float> (std::sin (core::kTwoPi * reedPhase))
        + modeLevel * (0.3f + 2.0f * controls.bell) * static_cast<float> (std::sin (core::kTwoPi * modePhase));
    modeLevel *= modeDecay;
    reedPhase = core::wrapPhase (reedPhase + reedStep);
    modePhase = core::wrapPhase (modePhase + modeStep);
    // Pickup: the reed's swing u (signed, about -1..1 at the strike) seen
    // through a curve that grows steeper as the reed nears the pickup,
    // y = u + d u^2 + d^2 u^3 + d^3 u^4 + d^4 u^5. `d` follows the swing, the
    // Tone control and the key. The even terms are the bark; the odd ones
    // the buzz. They fade with the note because u decays with the envelope.
    const float u = envelope * shape;
    // Glass Reed's swing is small and even, so its Tone reaches further into
    // the pickup's curve to stay as live as the struck reed's.
    const float toneReach = model == Model::glassReed ? 0.1f + 3.4f * controls.tone : 0.55f + 0.9f * controls.tone;
    const float d = std::min (0.75f, swing * pickupCurve * toneReach);
    const float du = d * u;
    float y = u * (1.0f + du * (1.0f + du * (1.0f + du * (1.0f + du))));
    // The even terms shift the centre of the swing; remove that analytically
    // (mean of the u^2 and u^4 terms for a sine) and catch the rest with a
    // 12 Hz blocker, so held notes never thump or drift.
    const float e2 = envelope * envelope;
    y -= d * e2 * 0.5f + d * d * d * e2 * e2 * 0.375f;
    if (evenBoost > 0.0f)
        y += evenBoost * d * (u * u - e2 * 0.5f);
    const float blocked = y - dcInput + dcCoefficient * dcState;
    dcInput = y;
    dcState = blocked;
    return blocked;
}

// ---- Struck strings (Electric Grand and the grand stand-ins) ------------------

void ModelVoice::startStrings (int note, const Controls& controls) noexcept
{
    const auto& v = voicing (model);
    const double f = core::midiNoteToHz (note);
    const double octavesFromC3 = static_cast<double> (note - 48) / 12.0;
    const double stiffness = v.stiffness * std::exp2 (octavesFromC3 / v.stiffnessOctaves);
    const double rolloff = v.rolloff + (v.hardRolloff - v.rolloff) * static_cast<double> (hardness);
    const double t60 = std::clamp (v.t60Low * std::exp2 (-static_cast<double> (note - 36) / (12.0 * v.t60Octaves)),
                                   v.t60Min, v.t60Max);
    const double h = static_cast<double> (((note * 37 + 17) % 23)) / 22.0;
    const double detune = std::exp2 ((v.detuneCents * (0.6 + 0.4 * h)) / 1200.0);
    double energy = 0.0;
    std::array<double, kMaxPartials> amplitude {};
    int count = 0;
    for (int n = 1; n <= v.partials; ++n)
    {
        const double nn = static_cast<double> (n);
        const double fn = nn * f * std::sqrt (1.0 + stiffness * nn * nn);
        if (fn * detune > 0.42 * sampleRate)
            break;
        // Strike position comb (a hammer at 1/9 of the string barely excites
        // the 9th partial), a velocity-dependent slope and the pickup's
        // presence peak.
        const double comb = std::abs (std::sin (core::kPi * nn * v.hammerPoint));
        const double presence = std::log2 (fn / 2600.0);
        double a = (0.15 + comb) / std::pow (nn, rolloff) * (1.0 + v.pickupBump * std::exp (-presence * presence / 0.72));
        // Fade partials out near the top limit so key-to-key timbre is smooth.
        a *= std::clamp ((0.42 * sampleRate - fn) / (0.05 * sampleRate), 0.0, 1.0);
        amplitude[static_cast<std::size_t> (count)] = a;
        energy += a * a;
        partialHz[static_cast<std::size_t> (count)] = static_cast<float> (fn);
        ++count;
    }
    numPartials = count;
    const double norm = energy > 0.0 ? 1.0 / std::sqrt (energy) : 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        const double fn = static_cast<double> (partialHz[k]);
        const double damping = 1.0 + v.partialDamping * static_cast<double> (i) + v.highDamping * (fn / 5000.0) * (fn / 5000.0);
        const double r1 = t60Radius (t60 / damping, sampleRate);
        const double r2 = t60Radius (t60 * v.secondT60 / damping, sampleRate);
        const double w1 = core::kTwoPi * fn / sampleRate / std::sqrt (detune);
        const double w2 = core::kTwoPi * fn / sampleRate * std::sqrt (detune);
        const double a = amplitude[k] * norm;
        // Schroeder start phases (pi n^2 / N): partials that all start in
        // sine phase add up to a spike several dB above the note's level, and
        // chords stack those spikes; this spreads them for a low crest
        // factor. The envelope's attack ramp starts at zero, so a non-zero
        // start phase cannot click.
        const double nn = static_cast<double> (i + 1);
        const double phase = core::kPi * nn * nn / static_cast<double> (count) + 0.37 * static_cast<double> (note);
        re1[k] = a * std::cos (phase);
        im1[k] = a * std::sin (phase);
        // Both strings are struck together, so they start in phase and
        // drift apart by their detune (the slow beating).
        re2[k] = a * v.secondLevel * std::cos (phase);
        im2[k] = a * v.secondLevel * std::sin (phase);
        cr1[k] = r1 * std::cos (w1);
        ci1[k] = r1 * std::sin (w1);
        cr2[k] = r2 * std::cos (w2);
        ci2[k] = r2 * std::sin (w2);
    }
    tiltBase = -1.0f; // forces updateWeights
    updateWeights (controls.tone);
    knockLevel = v.knock * std::pow (hardness, 1.5f);
    knockDecay = decayCoefficient (0.004, sampleRate);
    knockCoefficient = static_cast<float> (std::exp (-core::kTwoPi * static_cast<double> (v.knockHz) / sampleRate));
    const float high = keyHigh (note);
    envDecayScale = v.decayScale / (1.0f + 0.8f * high);
    envReleaseScale = v.releaseScale;
    voicePan = v.panWidth * std::clamp (static_cast<float> (note - 64) / 40.0f, -1.0f, 1.0f);
}

// ---- Celesta: struck steel bars over resonators -----------------------------

void ModelVoice::startBars (int note, const Controls& controls) noexcept
{
    // Celesta: a free bar's modes sit at 1, 2.76, 5.40 and 8.93 times the
    // fundamental. Vibes and Marimba: tuned bars at 1, 4 and about 10. The
    // fundamental is exactly in tune; the resonator under each bar
    // reinforces it, so it dominates and outlasts the upper modes, which a
    // harder strike excites more.
    const auto& v = bars (model);
    const double h = static_cast<double> (hardness);
    const double f = core::midiNoteToHz (note);
    // Celesta: fundamental T60 3 s up to about C4, falling to 1.3 s at C7.
    const double t60 = std::clamp (v.t60Scale * std::exp2 (-static_cast<double> (note - v.t60Note) / v.t60Span),
                                   v.t60Min, v.t60Max);
    double energy = 0.0;
    int count = 0;
    for (std::size_t n = 0; n < static_cast<std::size_t> (v.modes); ++n)
    {
        const double fn = v.ratios[n] * f;
        if (fn > 0.42 * sampleRate)
            break;
        const double a = (v.soft[n] + v.slope[n] * h) * std::clamp ((0.42 * sampleRate - fn) / (0.05 * sampleRate), 0.0, 1.0);
        const auto k = static_cast<std::size_t> (count);
        const double r = t60Radius (t60 / (1.0 + v.modeDamping * static_cast<double> (n)), sampleRate);
        const double w = core::kTwoPi * fn / sampleRate;
        // Struck at rest: every mode starts in sine phase (the onset ramp
        // keeps the start smooth).
        re1[k] = a;
        im1[k] = 0.0;
        cr1[k] = r * std::cos (w);
        ci1[k] = r * std::sin (w);
        re2[k] = im2[k] = cr2[k] = ci2[k] = 0.0;
        partialHz[k] = static_cast<float> (fn);
        energy += a * a;
        ++count;
    }
    numPartials = count;
    const double norm = energy > 0.0 ? 1.0 / std::sqrt (energy) : 0.0;
    for (int i = 0; i < count; ++i)
    {
        re1[static_cast<std::size_t> (i)] *= norm;
        im1[static_cast<std::size_t> (i)] *= norm;
    }
    tiltBase = -1.0f; // forces updateWeights
    updateWeights (controls.tone);
    // The mallet or hammer: a short knock, brightest and strongest on wood.
    knockLevel = v.knock * std::pow (hardness, 1.5f);
    knockDecay = decayCoefficient (v.knockSeconds, sampleRate);
    knockCoefficient = static_cast<float> (std::exp (-core::kTwoPi * v.knockHz / sampleRate));
    // The bars set their own decay; the section envelope only shapes it.
    envDecayScale = v.decayScale;
    envReleaseScale = v.releaseScale;
    voicePan = v.panWidth * std::clamp (static_cast<float> (note - 64) / 40.0f, -1.0f, 1.0f);
}

void ModelVoice::updateWeights (float tone) noexcept
{
    if (std::abs (tone - tiltBase) < 0.002f)
        return;
    tiltBase = tone;
    // Tone tilts the spectrum about the fundamental: n^e, e = -0.7 .. +0.7.
    const float exponent = (tone - 0.5f) * 1.4f + voicing (model).tiltOffset;
    for (int i = 0; i < numPartials; ++i)
        weights[static_cast<std::size_t> (i)] = std::exp2 (exponent * std::log2 (static_cast<float> (i + 1)));
}

float ModelVoice::nextStrings (float envelope) noexcept
{
    double sum = 0.0;
    for (int i = 0; i < numPartials; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        const double a1 = re1[k] * cr1[k] - im1[k] * ci1[k];
        const double b1 = re1[k] * ci1[k] + im1[k] * cr1[k];
        const double a2 = re2[k] * cr2[k] - im2[k] * ci2[k];
        const double b2 = re2[k] * ci2[k] + im2[k] * cr2[k];
        re1[k] = a1;
        im1[k] = b1;
        re2[k] = a2;
        im2[k] = b2;
        sum += static_cast<double> (weights[k]) * (b1 + b2);
    }
    return static_cast<float> (sum) * envelope;
}

// ---- Clav --------------------------------------------------------------------

void ModelVoice::startClav (int note, const Controls& controls) noexcept
{
    const double f = std::max (16.5, core::midiNoteToHz (note) * keyDetune (note, 1.5));
    const double period = sampleRate / f;
    // Loop: delay M -> two-tap low-pass -> first-order all-pass (fraction).
    // The low-pass sets how fast the upper harmonics die; harder strikes
    // excite a brighter string, so the loop is a little more open.
    // The harpsichord's plectrum is mechanical: always the same, bright pluck.
    const bool plucked = model == Model::harpsichord;
    lowpassB = plucked ? 0.2f : 0.10f + 0.20f * (1.0f - hardness);
    lowpassA = 1.0f - lowpassB;
    const double w0 = core::kTwoPi * f / sampleRate;
    const double hr = static_cast<double> (lowpassA) + static_cast<double> (lowpassB) * std::cos (w0);
    const double hi = static_cast<double> (lowpassB) * std::sin (w0);
    const double lowpassDelay = std::atan2 (hi, hr) / w0;
    const double magnitude = std::sqrt (hr * hr + hi * hi);
    const double remaining = period - lowpassDelay;
    loopDelay = std::clamp (static_cast<int> (std::floor (remaining - 0.5)), 2, lineSize - 4);
    const double fraction = std::clamp (remaining - static_cast<double> (loopDelay), 0.1, 1.9);
    allpassCoefficient = static_cast<float> ((1.0 - fraction) / (1.0 + fraction));
    allpassIn = allpassOut = lowpassState = 0.0f;
    const double t60 = std::clamp (7.0 * std::exp2 (-static_cast<double> (note - 36) / 30.0), 1.0, 7.0);
    loopGain = loopGainTarget = static_cast<float> (std::min (0.9995, std::pow (10.0, -3.0 / (t60 * f)) / magnitude));
    dampedGain = loopGain;
    gainSlew = static_cast<float> (1.0 - std::exp (-1.0 / (0.004 * sampleRate)));
    // Initial string state. The striker hits the string from below and
    // sets a short stretch of it moving (15% of its length): in this loop
    // that is a narrow pulse, whose 1/n spectrum the pickups' differencing
    // lifts into the bright, nasal clav tone. Its edges are rounded by a
    // velocity-dependent low-pass (about 2 kHz soft to 16 kHz hard): the
    // funky attack. The mean is removed so no offset circulates.
    {
        const float sensitivity = std::clamp (controls.sensitivity, 0.0f, 1.0f);
        const double strikeSpeed = plucked ? 0.85 : static_cast<double> (sensitivity * velocity + (1.0f - sensitivity) * 0.7f);
        const double cutoff = std::min (500.0 * std::exp2 (5.0 * strikeSpeed), 0.45 * sampleRate);
        const auto smooth = static_cast<float> (std::exp (-core::kTwoPi * cutoff / sampleRate));
        // The plectrum pulls a narrower stretch of string aside than the
        // clav's striker.
        const int width = std::max (1, static_cast<int> (std::lround ((plucked ? 0.06 : 0.15) * loopDelay)));
        float state = 0.0f;
        double mean = 0.0;
        for (int j = 0; j < loopDelay; ++j)
        {
            const float shape = j < width ? 1.0f : 0.0f;
            state = shape + smooth * (state - shape);
            line[static_cast<std::size_t> (j)] = state;
            mean += static_cast<double> (state);
        }
        const auto offset = static_cast<float> (mean / static_cast<double> (loopDelay));
        for (int j = 0; j < loopDelay; ++j)
            line[static_cast<std::size_t> (j)] -= offset;
        std::fill (line.begin() + loopDelay, line.begin() + std::min (lineSize, loopDelay + 4), 0.0f);
    }
    writeIndex = loopDelay; // reads start at the beginning of the shape
    // Two pickups at different points along the string: each reads the
    // difference of two taps, a comb that gives the nasal, hollow colour.
    pickupNear = std::max (1, static_cast<int> (std::lround (0.07 * loopDelay)));
    pickupFar = std::max (2, static_cast<int> (std::lround (0.21 * loopDelay)));
    if (model == Model::brightClav)
    {
        // Both pickups out of phase. Each reads the string's shape at its
        // position, sin (pi n x) for harmonic n; the near pickup's reading is
        // delayed to line up with the far one's, so the two are compared at
        // the same instant. Balanced so the fundamental region cancels
        // (rising about 12 dB per octave), they also cancel exactly where
        // gNear sin (pi n a) = gFar sin (pi n b): a deep comb notch.
        pickupNear = std::max (1, static_cast<int> (std::lround (kBrightNear * loopDelay)));
        pickupFar = std::max (pickupNear + 2, static_cast<int> (std::lround (kBrightFar * loopDelay)));
        pickupShift = (pickupFar - pickupNear) / 2;
        pickupNearGain = static_cast<float> (pickupFar) / static_cast<float> (pickupFar + pickupNear);
        pickupFarGain = kBrightBalance * static_cast<float> (pickupNear) / static_cast<float> (pickupFar + pickupNear);
    }
    if (plucked)
    {
        // The soundboard is read at the bridge: the string's slope there,
        // with the plucking point's comb.
        pickupNear = std::max (1, static_cast<int> (std::lround (kPluckPoint * loopDelay)));
    }
    // Strike noise from the striker hitting the string (Bell scales it); the
    // plectrum's quieter click does not follow velocity.
    exciteLength = std::max (2, static_cast<int> (std::lround (sampleRate * 0.0008)));
    exciteSamples = exciteLength;
    exciteAt = 0;
    exciteLevel = plucked ? 0.15f : (model == Model::brightClav ? 0.20f : 0.12f) * hardness;
    thunkSamples = 0;
    thudLevel = 0.0f;
    toneState = 0.0f;
    tiltBase = -1.0f;
    knockLevel = 0.0f;
    const float high = keyHigh (note);
    // The string sets its own decay; the section envelope only shapes it.
    envDecayScale = 3.0f / (1.0f + 0.8f * high);
    envReleaseScale = 1.0f;
    voicePan = 0.12f * static_cast<float> (note - 64) / 64.0f;
}

float ModelVoice::nextClav (const Controls& controls, float envelope) noexcept
{
    int readIndex = writeIndex - loopDelay;
    if (readIndex < 0)
        readIndex += lineSize;
    const float read = line[static_cast<std::size_t> (readIndex)];
    const float lp = lowpassA * read + lowpassB * lowpassState;
    lowpassState = read;
    const float ap = allpassCoefficient * (lp - allpassOut) + allpassIn;
    allpassIn = lp;
    allpassOut = ap;
    loopGain += (loopGainTarget - loopGain) * gainSlew;
    float input = loopGain * ap;
    if (exciteSamples > 0)
    {
        const float t = static_cast<float> (exciteAt++) / static_cast<float> (exciteLength);
        const float window = static_cast<float> (std::sin (core::kPi * static_cast<double> (t)));
        input += exciteLevel * (0.4f + 1.7f * controls.bell) * window * noise();
        --exciteSamples;
    }
    if (thunkSamples > 0)
    {
        const float t = 1.0f - static_cast<float> (thunkSamples) / static_cast<float> (thunkLength);
        input += thunkLevel * static_cast<float> (std::sin (core::kPi * static_cast<double> (t)));
        --thunkSamples;
    }
    line[static_cast<std::size_t> (writeIndex)] = input;
    const auto tap = [this] (int back) noexcept
    {
        int i = writeIndex - back;
        if (i < 0)
            i += lineSize;
        return line[static_cast<std::size_t> (i)];
    };
    const float newest = input;
    float pickup = 0.0f;
    if (model == Model::brightClav)
        pickup = kBrightPickupGain * (pickupNearGain * (tap (pickupShift) - tap (pickupShift + pickupNear))
                                      - pickupFarGain * (newest - tap (pickupFar)));
    else if (model == Model::harpsichord)
        pickup = newest - tap (pickupNear);
    else
        pickup = 0.65f * (newest - tap (pickupNear)) + 0.35f * (newest - tap (pickupFar));
    if (++writeIndex >= lineSize)
        writeIndex = 0;
    // Live Tone: a one-pole low-pass from about 1.2 kHz (0) to open (1).
    if (std::abs (controls.tone - tiltBase) >= 0.002f)
    {
        tiltBase = controls.tone;
        const double fc = std::min (0.45 * sampleRate, 1200.0 * std::exp2 (4.6 * static_cast<double> (controls.tone)));
        toneCoefficient = static_cast<float> (std::exp (-core::kTwoPi * fc / sampleRate));
    }
    toneState = pickup + toneCoefficient * (toneState - pickup);
    float out = toneState * envelope;
    if (thudLevel > 1.0e-6f)
    {
        out += thudLevel * static_cast<float> (std::sin (core::kTwoPi * thudPhase));
        thudPhase = core::wrapPhase (thudPhase + thudStep);
        thudLevel *= thudDecay;
    }
    return out;
}

// ---- Shared --------------------------------------------------------------------

void ModelVoice::next (const Controls& controls, float& left, float& right) noexcept
{
    if (! env.isActive() && retriggerSamples == 0)
    {
        previousLeft = previousRight = 0.0f;
        return;
    }
    float signal = 0.0f;
    // Only a carried sample is left once the envelope is idle (fadeOut()).
    if (env.isActive())
    {
        const float envelope = env.process();
        switch (model)
        {
            case Model::reed: signal = nextReed (controls, envelope); break;
            case Model::hardReed:
            case Model::glassReed: signal = nextReed (controls, envelope); break;
            case Model::clav:
            case Model::brightClav:
            case Model::harpsichord: signal = nextClav (controls, envelope); break;
            case Model::electricGrand:
            case Model::brightGrand:
            case Model::studioGrand:
            case Model::celesta:
            case Model::vibes:
            case Model::marimba:
                updateWeights (controls.tone);
                signal = nextStrings (envelope);
                break;
            case Model::brightTine: break; // never started here
        }
        if (knockLevel > 1.0e-6f)
        {
            // Felt and key noise at the strike, scaled by the Bell control.
            const float white = noise();
            knockState = white + knockCoefficient * (knockState - white);
            // 0.5 ms fade-in: noise starting at full level would be a click.
            const float ramp = std::min (1.0f, static_cast<float> (age) * knockRampStep);
            signal += knockLevel * ramp * controls.bell * 2.0f * knockState;
            knockLevel *= knockDecay;
        }
        if (model == Model::glassReed)
        {
            if (pluckLevel > 1.0e-6f)
            {
                // The felt tick: noise band-passed to about 2-8 kHz.
                const float white = noise();
                pluckLow = white + pluckLowCoefficient * (pluckLow - white);
                pluckState = pluckLow + pluckCoefficient * (pluckState - pluckLow);
                const float ramp = std::min (1.0f, static_cast<float> (age) * knockRampStep);
                signal += pluckLevel * ramp * (pluckLow - pluckState);
                pluckLevel *= pluckDecay;
            }
            signal *= glassDecay;
            glassDecay *= glassDecayCoefficient;
        }
        signal *= level;
        if (age < onsetSamples)
        {
            // 0.7 ms onset: the struck-string partials start at scattered phases,
            // so the first samples are not near zero as a sine start would be.
            const float t = static_cast<float> (age) / static_cast<float> (onsetSamples);
            signal *= t * t;
        }
    }
    ++age;
    if (released && std::abs (signal) < kQuietLevel && age > 2400)
        ++quietSamples;
    else
        quietSamples = 0;
    float outLeft = signal * (1.0f - voicePan);
    float outRight = signal * (1.0f + voicePan);
    if (retriggerSamples > 0)
    {
        outLeft += carryLeftLevel * retriggerOffset;
        outRight += carryRightLevel * retriggerOffset;
        retriggerOffset *= retriggerMultiplier;
        if (--retriggerSamples == 0)
            retriggerOffset = 0.0f;
    }
    previousLeft = outLeft;
    previousRight = outRight;
    left += outLeft;
    right += outRight;
}
} // namespace kraftonic::piano::detail
