// SPDX-License-Identifier: MIT
//
// Shared wavetable engine: 16 voices, each with two band-limited wavetable
// oscillators (unison 1-8), a sine sub, noise, pre-filter drive (ADAA), a
// multimode TPT state-variable filter, amp and mod envelopes and a key-synced
// LFO 1. LFO 2 runs free for the whole engine.
//
// Every engine-wide parameter is smoothed once per sample into a `Shared`
// snapshot that all voices read, so per-voice work is only what depends on
// the note. Everything runs per sample, so the output never depends on how
// the host splits blocks.

#include "kraftonic/wavetable/WaveEngine.h"

#include "dsp/Filter.h"
#include "dsp/Lfo.h"
#include "dsp/Random.h"
#include "dsp/WaveOscillator.h"
#include "dsp/Wavetables.h"
#include "kraftonic/core/Adsr.h"
#include "kraftonic/core/Math.h"
#include "kraftonic/core/Smoother.h"
#include "kraftonic/core/StereoPeakProtector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace kraftonic::wavetable
{
namespace
{
using namespace detail;

constexpr float kVoiceGain = 0.2f;
constexpr double kParameterRampSeconds = 0.01;
constexpr double kGainRampSeconds = 0.02;
constexpr double kStealFadeSeconds = 0.003;
constexpr double kTableFadeSeconds = 0.01;

// What full LFO depth (1.0) means for each destination.
constexpr float kLfoPitchSemitones = 12.0f; // +-1 octave
constexpr float kLfoCutoffOctaves = 4.0f;   // +-4 octaves
constexpr float kLfoPosition = 0.5f;        // +-half the table
// level: tremolo down to (1 - depth); pan: +-depth (1 = hard left/right)


enum Destination
{
    destOff = 0,
    destPitch = 1,
    destCutoff = 2,
    destPosition = 3,
    destLevel = 4,
    destPan = 5,
    numDestinations = 6
};

enum VoiceMode
{
    modePoly = 0,
    modeMono = 1,
    modeLegato = 2
};

float safeValue (const WaveParameters& params, ParamId id) noexcept
{
    const auto& info = paramInfo (id);
    const float value = params.get (id);
    return std::clamp (std::isfinite (value) ? value : info.defaultValue, info.minValue, info.maxValue);
}

int safeInt (const WaveParameters& params, ParamId id) noexcept
{
    return static_cast<int> (std::lround (safeValue (params, id)));
}

/** Engine-wide, per-sample snapshot of every smoothed parameter. */
struct Shared
{
    struct Osc
    {
        TableSelection selection;
        UnisonLayout layout;
        float position = 0.0f;
        float level = 0.0f;
        double tuneRatio = 1.0; // octave + semitone + fine
        bool audible = false;
    };
    std::array<Osc, 2> osc;
    float subLevel = 0.0f;
    double subRatio = 0.5;
    float noiseLevel = 0.0f;

    float cutoffOctaves = 0.0f; // log2 Hz
    float keyTrack = 0.0f;
    float envToCutoff = 0.0f;
    float envToPosition = 0.0f;
    float velocityToCutoff = 0.0f;
    float velocityToLevel = 0.0f;
    float damping = 1.0f; // 1 / Q
    float resonanceGain = 1.0f;
    std::array<float, 4> modeWeights {}; // LP12, LP24, BP, HP

    float driveMix = 0.0f;
    float driveGain = 1.0f;
    float driveMakeup = 1.0f;

    float bendSemitones = 0.0f;
    std::array<float, numDestinations> lfo1Amount {};
    double lfo1Increment = 0.0;
    int lfo1Shape = 0;

    // LFO 2 runs free; these are its contributions this sample.
    float lfo2Pitch = 0.0f;
    float lfo2Cutoff = 0.0f;
    float lfo2Position = 0.0f;
    float lfo2LevelGain = 1.0f;
    float lfo2Pan = 0.0f;

    double incrementPerA4 = 0.0; // 440 Hz / sample rate
    float piOverSampleRate = 0.0f;
    float maxCutoffOctaves = 14.0f;
};

struct Voice
{
    // Identity, for allocation.
    int note = -1;
    bool held = false;
    std::uint64_t startedAt = 0;
    float velocity = 0.0f;      // target, 0..1
    float velocityNow = 0.0f;   // smoothed, so a re-attack at a new velocity never clicks
    float velocityCoef = 0.0f;

    // Steal: fade the old sound out over a few ms, then start the new note.
    bool pending = false;
    float pendingVelocity = 0.0f;
    double pendingGlideFrom = -1.0;
    float fade = 1.0f;
    float fadeStep = 0.0f;

    std::array<WaveOscillator, 2> osc;
    double subPhase = 0.0;
    Random random;
    Lfo lfo1;
    Svf stage1Left, stage1Right, stage2Left, stage2Right;
    AdaaTanh driveLeft, driveRight;
    bool driveStale = true;
    core::Adsr amp;
    core::Adsr mod;

    double glidePitch = 60.0;
    double glideTarget = 60.0;
    double glideStep = 0.0;
    double glideSamples = 0.0; // glide time in samples, set by the engine

    // Per-sample caches: pitch and cutoff are often steady.
    double cachedPitch = -1.0e9;
    double cachedIncrement = 0.0;
    float cachedCutoffOctaves = -1.0e9f;
    float cachedDamping = -1.0f;
    SvfCoefficients stage1, stage2;

    bool isActive() const noexcept { return amp.isActive() || pending; }

    void prepare (double sampleRate, std::uint32_t seed) noexcept
    {
        amp.prepare (sampleRate);
        mod.prepare (sampleRate);
        lfo1.prepare (sampleRate);
        fadeStep = static_cast<float> (1.0 / std::max (1.0, kStealFadeSeconds * sampleRate));
        velocityCoef = static_cast<float> (std::exp (-1.0 / (0.003 * sampleRate)));
        random.setSeed (seed);
        kill();
    }

    void kill() noexcept
    {
        amp.reset();
        mod.reset();
        pending = false;
        fade = 1.0f;
        note = -1;
        held = false;
        clearState();
    }

    void clearState() noexcept
    {
        for (auto* s : { &stage1Left, &stage1Right, &stage2Left, &stage2Right })
            s->reset();
        driveLeft.reset();
        driveRight.reset();
        driveStale = true;
        subPhase = 0.0;
    }

    void setGlide (double from, double to) noexcept
    {
        glideTarget = to;
        if (from < 0.0 || glideSamples < 1.0)
        {
            glidePitch = to;
            glideStep = 0.0;
            return;
        }
        glidePitch = from;
        glideStep = (to - from) / glideSamples;
    }

    /** Starts from silence: clears the filter, sets fresh phases, syncs LFO 1. */
    void startFresh (int midiNote, float vel, double glideFrom, int unisonA, int unisonB) noexcept
    {
        amp.reset();
        mod.reset();
        clearState();
        osc[0].start (random, unisonA > 1);
        osc[1].start (random, unisonB > 1);
        lfo1.reset (random.nextInt());
        note = midiNote;
        setVelocity (vel);
        velocityNow = velocity;
        setGlide (glideFrom, static_cast<double> (midiNote));
        amp.noteOn();
        mod.noteOn();
        fade = 1.0f;
        pending = false;
    }

    /** Re-attacks a sounding voice in place (same-note repeat, mono). The
        envelopes rise from their current level, so the output is continuous. */
    void retrigger (int midiNote, float vel, double glideFrom, bool restartEnvelopes, bool syncLfo) noexcept
    {
        note = midiNote;
        if (restartEnvelopes)
            setVelocity (vel);
        setGlide (glideFrom, static_cast<double> (midiNote));
        if (restartEnvelopes)
        {
            amp.noteOn();
            mod.noteOn();
        }
        if (syncLfo)
            lfo1.reset (random.nextInt(), false);
    }

    /** Takes over a sounding voice for another note: fade out, then start. */
    void steal (int midiNote, float vel, double glideFrom) noexcept
    {
        note = midiNote;
        pending = true;
        pendingVelocity = vel;
        pendingGlideFrom = glideFrom;
    }

    void release() noexcept
    {
        held = false;
        if (! pending)
        {
            amp.noteOff();
            mod.noteOff();
        }
    }

    void setVelocity (float vel) noexcept
    {
        velocity = std::clamp (vel, 0.0f, 1.0f);
    }

    void render (const Shared& s, const WavetableBank& bank, int unisonA, int unisonB,
                 float& outLeft, float& outRight) noexcept
    {
        if (! isActive())
            return;

        if (glideStep != 0.0)
        {
            glidePitch += glideStep;
            if ((glideStep > 0.0 && glidePitch >= glideTarget) || (glideStep < 0.0 && glidePitch <= glideTarget))
            {
                glidePitch = glideTarget;
                glideStep = 0.0;
            }
        }

        const float lfo = lfo1.next (s.lfo1Increment, s.lfo1Shape);
        velocityNow = velocity + (velocityNow - velocity) * velocityCoef;
        const float modEnv = mod.process();
        const float ampEnv = amp.process();

        const double pitch = glidePitch + static_cast<double> (s.bendSemitones
                                                                + lfo * s.lfo1Amount[destPitch] * kLfoPitchSemitones
                                                                + s.lfo2Pitch);
        if (pitch != cachedPitch)
        {
            cachedPitch = pitch;
            cachedIncrement = s.incrementPerA4 * std::exp2 ((pitch - 69.0) / 12.0);
        }
        const double baseIncrement = cachedIncrement;
        const float positionOffset = s.envToPosition * modEnv + lfo * s.lfo1Amount[destPosition] * kLfoPosition
                                     + s.lfo2Position;

        float left = 0.0f, right = 0.0f;
        for (std::size_t o = 0; o < osc.size(); ++o)
        {
            const auto& so = s.osc[o];
            if (! so.audible)
                continue;
            float l = 0.0f, r = 0.0f;
            osc[o].process (bank, so.layout, so.selection, so.position + positionOffset, baseIncrement * so.tuneRatio, l, r);
            left += l * so.level;
            right += r * so.level;
        }
        float centre = 0.0f;
        if (s.subLevel > 0.0f)
        {
            const double subIncrement = baseIncrement * s.subRatio;
            centre += static_cast<float> (std::sin (core::kTwoPi * subPhase)) * s.subLevel * nyquistFade (subIncrement);
            subPhase += subIncrement;
            subPhase -= std::floor (subPhase);
        }
        if (s.noiseLevel > 0.0f)
            centre += random.nextBipolar() * s.noiseLevel * 0.5f;
        left += centre;
        right += centre;

        if (s.driveMix > 0.0f)
        {
            const float g = s.driveGain;
            if (driveStale)
            {
                driveLeft.track (left * g);
                driveRight.track (right * g);
                driveStale = false;
            }
            const float wetLeft = driveLeft.process (left * g) * s.driveMakeup;
            const float wetRight = driveRight.process (right * g) * s.driveMakeup;
            left += s.driveMix * (wetLeft - left);
            right += s.driveMix * (wetRight - right);
        }
        else
        {
            driveStale = true;
        }

        const float cutoffOctaves = s.cutoffOctaves
                                    + s.keyTrack * static_cast<float> ((glidePitch - 60.0) / 12.0)
                                    + s.envToCutoff * modEnv + s.velocityToCutoff * velocityNow
                                    + lfo * s.lfo1Amount[destCutoff] * kLfoCutoffOctaves + s.lfo2Cutoff;
        if (cutoffOctaves != cachedCutoffOctaves || s.damping != cachedDamping)
        {
            cachedCutoffOctaves = cutoffOctaves;
            cachedDamping = s.damping;
            // 20 Hz .. min (20 kHz, 0.45 fs)
            const float cutoff = std::exp2 (std::clamp (cutoffOctaves, 4.321928f, s.maxCutoffOctaves));
            const float g = std::tan (s.piOverSampleRate * cutoff);
            stage1.set (g, s.damping);
            stage2.set (g, 1.41421356f);
        }
        const auto& c1 = stage1;
        const auto& c2 = stage2;
        float lowLeft, bandLeft, highLeft, lowRight, bandRight, highRight;
        stage1Left.process (left, c1, lowLeft, bandLeft, highLeft);
        stage1Right.process (right, c1, lowRight, bandRight, highRight);
        const float low24Left = stage2Left.lowPass (lowLeft, c2);
        const float low24Right = stage2Right.lowPass (lowRight, c2);
        const auto& w = s.modeWeights;
        left = (w[0] * lowLeft + w[1] * low24Left + w[2] * s.damping * bandLeft + w[3] * highLeft) * s.resonanceGain;
        right = (w[0] * lowRight + w[1] * low24Right + w[2] * s.damping * bandRight + w[3] * highRight) * s.resonanceGain;

        const float levelLfo = (1.0f - s.lfo1Amount[destLevel] * 0.5f * (1.0f - lfo)) * s.lfo2LevelGain;
        const float velocityGain = 1.0f - s.velocityToLevel * (1.0f - velocityNow);
        const float gain = ampEnv * velocityGain * kVoiceGain * fade * levelLfo;
        const float pan = std::clamp (lfo * s.lfo1Amount[destPan] + s.lfo2Pan, -1.0f, 1.0f);
        const float panLeft = std::min (1.0f, 1.0f - pan);
        const float panRight = std::min (1.0f, 1.0f + pan);
        if (std::isfinite (left) && std::isfinite (right))
        {
            outLeft += left * gain * panLeft;
            outRight += right * gain * panRight;
        }
        else
        {
            clearState();
        }

        if (pending)
        {
            fade -= fadeStep;
            if (fade <= 0.0f || ! amp.isActive())
            {
                const bool stillHeld = held;
                startFresh (note, pendingVelocity, pendingGlideFrom, unisonA, unisonB);
                held = stillHeld;
                if (! held)
                    release();
            }
        }
        else if (! amp.isActive())
        {
            note = -1;
            held = false;
        }
    }
};
} // namespace

struct WaveEngine::Impl
{
    double sampleRate = 48000.0;
    const WavetableBank* bank = nullptr;
    std::array<Voice, kMaxVoices> voices;
    std::uint64_t clock = 0;
    Shared shared;
    WaveParameters params = WaveParameters::defaults();

    // Smoothed engine-wide values.
    core::LinearSmoother outputGain;
    std::array<core::LinearSmoother, 2> position, level, tune, detune, spread, tableMix;
    double lfo2Increment = 0.0;
    int lfo2Shape = 0;
    std::array<std::array<core::LinearSmoother, kMaxUnison>, 2> copyWeights;
    core::LinearSmoother subLevel, noiseLevel, cutoff, resonance, drive, keyTrack, envToCutoff, envToPosition,
        velocityToCutoff, velocityToLevel, wheel, wheelAmount, bend;
    std::array<core::LinearSmoother, 4> modeWeights;
    std::array<core::LinearSmoother, numDestinations> lfo1Depth, lfo2Depth;
    Lfo lfo2;

    std::array<int, 2> tables {}, unison { 1, 1 };
    int voiceMode = modePoly;
    int lastNote = -1;
    bool layoutDirty = true;

    // Keys and pedal.
    std::array<bool, 128> keyDown {};
    std::array<bool, 128> pedalHeld {};
    bool pedalDown = false;
    std::array<int, 128> monoStack {}; // held notes, oldest first
    int monoCount = 0;

    float wheelValue = 0.0f, bendValue = 0.0f;

    template <typename F>
    void forEachSmoother (F&& f) noexcept
    {
        for (auto* group : { &position, &level, &tune, &detune, &spread })
            for (auto& sm : *group)
                f (sm);
        for (auto& weights : copyWeights)
            for (auto& sm : weights)
                f (sm);
        for (auto* sm : { &subLevel, &noiseLevel, &cutoff, &resonance, &drive, &keyTrack, &envToCutoff,
                          &envToPosition, &velocityToCutoff, &velocityToLevel, &wheel, &wheelAmount, &bend })
            f (*sm);
        for (auto& sm : modeWeights)
            f (sm);
        for (auto& sm : lfo1Depth)
            f (sm);
        for (auto& sm : lfo2Depth)
            f (sm);
    }

    void prepare (double rate)
    {
        sampleRate = std::clamp (rate, 22050.0, 384000.0);
        bank = &WavetableBank::instance();
        for (std::size_t i = 0; i < voices.size(); ++i)
            voices[i].prepare (sampleRate, 0x1234567U + static_cast<std::uint32_t> (i) * 0x9e3779b9U);
        forEachSmoother ([this] (core::LinearSmoother& sm) { sm.prepare (sampleRate, kParameterRampSeconds); });
        for (auto& t : tableMix)
            t.prepare (sampleRate, kTableFadeSeconds);
        outputGain.prepare (sampleRate, kGainRampSeconds);
        lfo2.prepare (sampleRate);
        lfo2.reset (0xabcdef1U);
        shared.incrementPerA4 = 440.0 / sampleRate;
        shared.piOverSampleRate = static_cast<float> (core::kPi / sampleRate);
        shared.maxCutoffOctaves = std::log2 (static_cast<float> (std::min (20000.0, 0.45 * sampleRate)));
        silence();
    }

    void snap() noexcept
    {
        forEachSmoother ([] (core::LinearSmoother& sm) { sm.setImmediate (sm.getTarget()); });
        outputGain.setImmediate (outputGain.getTarget());
        for (std::size_t o = 0; o < tableMix.size(); ++o)
        {
            tableMix[o].setImmediate (1.0f);
            shared.osc[o].selection = { tables[o], tables[o], 1.0f };
        }
        layoutDirty = true;
    }

    void silence() noexcept
    {
        for (auto& v : voices)
            v.kill();
        keyDown = {};
        pedalHeld = {};
        pedalDown = false;
        monoCount = 0;
        lastNote = -1;
    }

    // ---- parameters --------------------------------------------------------

    void setParameters (const WaveParameters& p) noexcept
    {
        params = p;
        outputGain.setTarget (core::decibelsToGain (safeValue (p, ParamId::outputGain)));
        constexpr std::array<std::array<ParamId, 9>, 2> ids {{
            { ParamId::oscATable, ParamId::oscAPosition, ParamId::oscAOctave, ParamId::oscASemitone, ParamId::oscAFine,
              ParamId::oscALevel, ParamId::oscAUnison, ParamId::oscADetune, ParamId::oscASpread },
            { ParamId::oscBTable, ParamId::oscBPosition, ParamId::oscBOctave, ParamId::oscBSemitone, ParamId::oscBFine,
              ParamId::oscBLevel, ParamId::oscBUnison, ParamId::oscBDetune, ParamId::oscBSpread },
        }};
        for (std::size_t o = 0; o < 2; ++o)
        {
            const auto& id = ids[o];
            tables[o] = std::clamp (safeInt (p, id[0]), 0, kNumTables - 1); // crossfaded in updateShared()
            position[o].setTarget (safeValue (p, id[1]));
            tune[o].setTarget (12.0f * static_cast<float> (safeInt (p, id[2])) + static_cast<float> (safeInt (p, id[3]))
                               + safeValue (p, id[4]) / 100.0f);
            level[o].setTarget (safeValue (p, id[5]));
            const int count = std::clamp (safeInt (p, id[6]), 1, kMaxUnison);
            if (count != unison[o])
                layoutDirty = true;
            unison[o] = count;
            for (int i = 0; i < kMaxUnison; ++i)
                copyWeights[o][static_cast<std::size_t> (i)].setTarget (i < count ? 1.0f : 0.0f);
            detune[o].setTarget (safeValue (p, id[7]));
            spread[o].setTarget (safeValue (p, id[8]));
        }
        subLevel.setTarget (safeValue (p, ParamId::subLevel));
        shared.subRatio = safeInt (p, ParamId::subOctave) == 0 ? 0.5 : 0.25;
        noiseLevel.setTarget (safeValue (p, ParamId::noiseLevel));

        const int mode = std::clamp (safeInt (p, ParamId::filterMode), 0, 3);
        for (int m = 0; m < 4; ++m)
            modeWeights[static_cast<std::size_t> (m)].setTarget (m == mode ? 1.0f : 0.0f);
        cutoff.setTarget (std::log2 (safeValue (p, ParamId::filterCutoff)));
        resonance.setTarget (safeValue (p, ParamId::filterResonance));
        drive.setTarget (safeValue (p, ParamId::filterDrive));
        keyTrack.setTarget (safeValue (p, ParamId::filterKeyTrack));
        envToCutoff.setTarget (safeValue (p, ParamId::filterEnvAmount));
        envToPosition.setTarget (safeValue (p, ParamId::modEnvToPosition));
        velocityToCutoff.setTarget (safeValue (p, ParamId::velocityToCutoff));
        velocityToLevel.setTarget (safeValue (p, ParamId::velocityToLevel));

        const core::Adsr::Parameters ampParams { safeValue (p, ParamId::ampAttack), safeValue (p, ParamId::ampDecay),
                                                 safeValue (p, ParamId::ampSustain), safeValue (p, ParamId::ampRelease) };
        const core::Adsr::Parameters modParams { safeValue (p, ParamId::modAttack), safeValue (p, ParamId::modDecay),
                                                 safeValue (p, ParamId::modSustain), safeValue (p, ParamId::modRelease) };
        const double glideSamples = static_cast<double> (safeValue (p, ParamId::glideTime)) * sampleRate;
        for (auto& v : voices)
        {
            v.amp.setParameters (ampParams);
            v.mod.setParameters (modParams);
            v.glideSamples = glideSamples;
        }

        const int lfo1Dest = std::clamp (safeInt (p, ParamId::lfo1Destination), 0, numDestinations - 1);
        const int lfo2Dest = std::clamp (safeInt (p, ParamId::lfo2Destination), 0, numDestinations - 1);
        for (int d = 1; d < numDestinations; ++d)
        {
            lfo1Depth[static_cast<std::size_t> (d)].setTarget (d == lfo1Dest ? safeValue (p, ParamId::lfo1Depth) : 0.0f);
            lfo2Depth[static_cast<std::size_t> (d)].setTarget (d == lfo2Dest ? safeValue (p, ParamId::lfo2Depth) : 0.0f);
        }
        shared.lfo1Increment = static_cast<double> (safeValue (p, ParamId::lfo1Rate)) / sampleRate;
        shared.lfo1Shape = std::clamp (safeInt (p, ParamId::lfo1Shape), 0, 4);
        lfo2Increment = static_cast<double> (safeValue (p, ParamId::lfo2Rate)) / sampleRate;
        lfo2Shape = std::clamp (safeInt (p, ParamId::lfo2Shape), 0, 4);
        wheelAmount.setTarget (safeValue (p, ParamId::modWheelToLfo1));
        bend.setTarget (bendValue * safeValue (p, ParamId::pitchBendRange));

        const int mode2 = std::clamp (safeInt (p, ParamId::voiceMode), 0, 2);
        if (mode2 != voiceMode)
        {
            // Switching voice handling: let everything ring out and start clean.
            for (auto& v : voices)
                v.release();
            monoCount = 0;
            voiceMode = mode2;
        }
    }

    /** Advances every smoother one sample and refreshes the shared snapshot. */
    void updateShared() noexcept
    {
        auto& s = shared;
        for (std::size_t o = 0; o < 2; ++o)
        {
            auto& so = s.osc[o];
            so.position = position[o].next();
            so.level = level[o].next();
            so.audible = so.level > 0.0f;
            if (tune[o].isSmoothing() || layoutDirty)
                so.tuneRatio = std::exp2 (static_cast<double> (tune[o].next()) / 12.0);
            else
                tune[o].next();
            // Table changes crossfade over 10 ms; a change that arrives mid-fade
            // waits for the current fade to finish, so nothing ever jumps.
            if (tables[o] != so.selection.table && ! tableMix[o].isSmoothing())
            {
                so.selection.previousTable = so.selection.table;
                so.selection.table = tables[o];
                tableMix[o].setImmediate (0.0f);
                tableMix[o].setTarget (1.0f);
            }
            so.selection.mix = tableMix[o].next();
            bool layoutMoving = layoutDirty || detune[o].isSmoothing() || spread[o].isSmoothing();
            std::array<float, kMaxUnison> weights {};
            for (std::size_t i = 0; i < kMaxUnison; ++i)
            {
                layoutMoving = layoutMoving || copyWeights[o][i].isSmoothing();
                weights[i] = copyWeights[o][i].next();
            }
            const float detuneCents = detune[o].next();
            const float stereo = spread[o].next();
            if (layoutMoving)
                layoutUnison (so.layout, unison[o], detuneCents, stereo, weights);
        }
        layoutDirty = false;
        s.subLevel = subLevel.next();
        s.noiseLevel = noiseLevel.next();

        s.cutoffOctaves = cutoff.next();
        s.keyTrack = keyTrack.next();
        s.envToCutoff = envToCutoff.next();
        s.envToPosition = envToPosition.next();
        s.velocityToCutoff = velocityToCutoff.next();
        s.velocityToLevel = velocityToLevel.next();
        const float r = resonance.next();
        s.damping = 1.0f / (0.70710678f * std::pow (28.0f, r)); // Q 0.707 .. ~20
        s.resonanceGain = 1.0f - 0.25f * r;
        for (std::size_t m = 0; m < 4; ++m)
            s.modeWeights[m] = modeWeights[m].next();

        const float driveDb = drive.next();
        s.driveMix = std::clamp (driveDb / 3.0f, 0.0f, 1.0f);
        if (s.driveMix > 0.0f)
        {
            s.driveGain = core::decibelsToGain (driveDb);
            s.driveMakeup = 1.0f / std::tanh (s.driveGain);
        }

        s.bendSemitones = bend.next();
        const float w = wheel.next();
        const float wa = wheelAmount.next();
        const float wheelScale = 1.0f - wa + wa * w;
        for (std::size_t d = 1; d < numDestinations; ++d)
            s.lfo1Amount[d] = lfo1Depth[d].next() * wheelScale;

        const float l2 = lfo2.next (lfo2Increment, lfo2Shape);
        s.lfo2Pitch = l2 * lfo2Depth[destPitch].next() * kLfoPitchSemitones;
        s.lfo2Cutoff = l2 * lfo2Depth[destCutoff].next() * kLfoCutoffOctaves;
        s.lfo2Position = l2 * lfo2Depth[destPosition].next() * kLfoPosition;
        s.lfo2LevelGain = 1.0f - lfo2Depth[destLevel].next() * 0.5f * (1.0f - l2);
        s.lfo2Pan = l2 * lfo2Depth[destPan].next();
    }

    // ---- notes -------------------------------------------------------------

    int findVoiceFor (int note) const noexcept
    {
        int best = -1;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto& v = voices[static_cast<std::size_t> (i)];
            if (v.isActive() && v.note == note)
                return i;
        }
        for (int i = 0; i < kMaxVoices; ++i)
            if (! voices[static_cast<std::size_t> (i)].isActive())
                return i;
        for (const bool held : { false, true })
        {
            for (int i = 0; i < kMaxVoices; ++i)
            {
                const auto& v = voices[static_cast<std::size_t> (i)];
                if (v.held != held)
                    continue;
                if (best < 0 || v.startedAt < voices[static_cast<std::size_t> (best)].startedAt)
                    best = i;
            }
            if (best >= 0)
                return best;
        }
        return 0;
    }

    double glideOrigin() const noexcept
    {
        return lastNote >= 0 ? static_cast<double> (lastNote) : -1.0;
    }

    void polyNoteOn (int note, float velocity) noexcept
    {
        const int index = findVoiceFor (note);
        auto& v = voices[static_cast<std::size_t> (index)];
        const double from = glideOrigin();
        if (! v.isActive())
            v.startFresh (note, velocity, from, unison[0], unison[1]);
        else if (v.note == note)
        {
            if (v.pending)
                v.pendingVelocity = velocity;
            else
                v.retrigger (note, velocity, from, true, true);
        }
        else
            v.steal (note, velocity, from);
        v.held = true;
        v.startedAt = ++clock;
        lastNote = note;
    }

    void polyRelease (int note) noexcept
    {
        for (auto& v : voices)
            if (v.held && v.note == note)
                v.release();
    }

    void monoPush (int note) noexcept
    {
        monoRemove (note);
        if (monoCount < 128)
            monoStack[static_cast<std::size_t> (monoCount++)] = note;
    }

    bool monoRemove (int note) noexcept
    {
        for (int i = 0; i < monoCount; ++i)
            if (monoStack[static_cast<std::size_t> (i)] == note)
            {
                for (int j = i + 1; j < monoCount; ++j)
                    monoStack[static_cast<std::size_t> (j - 1)] = monoStack[static_cast<std::size_t> (j)];
                --monoCount;
                return true;
            }
        return false;
    }

    /** Moves the mono voice to `note`. `overlapping` = another note was already held. */
    void monoPlay (int note, float velocity, bool overlapping) noexcept
    {
        auto& v = voices[0];
        const bool legato = voiceMode == modeLegato;
        const double from = v.isActive() && ! v.pending ? v.glidePitch : glideOrigin();
        const double glideFrom = (legato && ! overlapping) ? -1.0 : from;
        if (! v.isActive())
            v.startFresh (note, velocity, glideFrom, unison[0], unison[1]);
        else if (v.pending)
        {
            v.note = note;
            v.pendingVelocity = velocity;
            v.pendingGlideFrom = glideFrom;
        }
        else
        {
            const bool retrigger = ! (legato && overlapping);
            v.retrigger (note, velocity, glideFrom, retrigger, retrigger);
        }
        v.held = true;
        v.startedAt = ++clock;
        lastNote = note;
    }

    void monoNoteOn (int note, float velocity) noexcept
    {
        const bool overlapping = monoCount > 0;
        monoPush (note);
        monoPlay (note, velocity, overlapping);
    }

    /** After notes leave the stack: fall back to the newest held note, or release. */
    void monoUpdateAfterRelease() noexcept
    {
        auto& v = voices[0];
        if (monoCount == 0)
        {
            if (v.held)
                v.release();
            return;
        }
        const int top = monoStack[static_cast<std::size_t> (monoCount - 1)];
        if (v.note != top || ! v.held)
            monoPlay (top, v.velocity, true);
    }

    void noteOn (int note, float velocity) noexcept
    {
        keyDown[static_cast<std::size_t> (note)] = true;
        pedalHeld[static_cast<std::size_t> (note)] = false;
        if (voiceMode == modePoly)
            polyNoteOn (note, velocity);
        else
            monoNoteOn (note, velocity);
    }

    void noteOff (int note) noexcept
    {
        keyDown[static_cast<std::size_t> (note)] = false;
        if (pedalDown)
        {
            pedalHeld[static_cast<std::size_t> (note)] = true;
            return;
        }
        if (voiceMode == modePoly)
            polyRelease (note);
        else if (monoRemove (note))
            monoUpdateAfterRelease();
    }

    void pedalUp() noexcept
    {
        bool removed = false;
        for (int note = 0; note < 128; ++note)
        {
            if (! std::exchange (pedalHeld[static_cast<std::size_t> (note)], false)
                || keyDown[static_cast<std::size_t> (note)])
                continue;
            if (voiceMode == modePoly)
                polyRelease (note);
            else
                removed = monoRemove (note) || removed;
        }
        if (removed)
            monoUpdateAfterRelease();
    }

    void handle (const Event& event) noexcept
    {
        switch (event.type)
        {
            case Event::Type::noteOn:
                if (event.note < 0 || event.note > 127)
                    break;
                if (! std::isfinite (event.value) || event.value <= 0.0f)
                    noteOff (event.note);
                else
                    noteOn (event.note, std::min (event.value, 1.0f));
                break;
            case Event::Type::noteOff:
                if (event.note >= 0 && event.note <= 127)
                    noteOff (event.note);
                break;
            case Event::Type::sustainPedal:
            {
                const bool down = std::isfinite (event.value) && event.value >= 0.5f;
                if (pedalDown && ! down)
                {
                    pedalDown = false;
                    pedalUp();
                }
                pedalDown = down;
                break;
            }
            case Event::Type::allNotesOff:
                pedalDown = false;
                keyDown = {};
                pedalHeld = {};
                monoCount = 0;
                for (auto& v : voices)
                    v.release();
                break;
            case Event::Type::allSoundOff:
                silence();
                break;
            case Event::Type::modWheel:
                wheelValue = std::isfinite (event.value) ? std::clamp (event.value, 0.0f, 1.0f) : 0.0f;
                wheel.setTarget (wheelValue);
                break;
            case Event::Type::pitchBend:
                bendValue = std::isfinite (event.value) ? std::clamp (event.value, -1.0f, 1.0f) : 0.0f;
                bend.setTarget (bendValue * safeValue (params, ParamId::pitchBendRange));
                break;
            case Event::Type::polyPressure:
            case Event::Type::channelPressure:
                break;
        }
    }

    void render (float* left, float* right, int count) noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            updateShared();
            float l = 0.0f, r = 0.0f;
            for (auto& v : voices)
                v.render (shared, *bank, unison[0], unison[1], l, r);
            const float g = outputGain.next();
            l *= g;
            r *= g;
            if (! std::isfinite (l) || ! std::isfinite (r))
                l = r = 0.0f;
            core::StereoPeakProtector::process (l, r);
            if (right == left)
                left[i] = 0.5f * (l + r);
            else
            {
                left[i] = l;
                right[i] = r;
            }
        }
    }
};

WaveEngine::WaveEngine() : impl (std::make_unique<Impl>()) {}
WaveEngine::~WaveEngine() = default;

void WaveEngine::prepare (const ProcessSetup& setup)
{
    impl->prepare (setup.sampleRate);
    impl->setParameters (impl->params);
    impl->snap();
}

void WaveEngine::reset() noexcept
{
    impl->silence();
}

void WaveEngine::setParameters (const WaveParameters& params) noexcept
{
    impl->setParameters (params);
}

void WaveEngine::process (const AudioBlock& block, const Event* events, int numEvents) noexcept
{
    if (block.left == nullptr || block.right == nullptr || block.numSamples <= 0 || impl->bank == nullptr)
    {
        if (block.left != nullptr && block.numSamples > 0)
            std::fill_n (block.left, block.numSamples, 0.0f);
        if (block.right != nullptr && block.numSamples > 0)
            std::fill_n (block.right, block.numSamples, 0.0f);
        return;
    }
    int pos = 0;
    for (int i = 0; events != nullptr && i < numEvents && i < kMaxEventsPerBlock; ++i)
    {
        const int at = std::clamp (events[i].sampleOffset, pos, block.numSamples);
        if (at > pos)
        {
            impl->render (block.left + pos, block.right + pos, at - pos);
            pos = at;
        }
        impl->handle (events[i]);
    }
    if (pos < block.numSamples)
        impl->render (block.left + pos, block.right + pos, block.numSamples - pos);
}

int WaveEngine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : impl->voices)
        n += v.isActive() ? 1 : 0;
    return n;
}
} // namespace kraftonic::wavetable
