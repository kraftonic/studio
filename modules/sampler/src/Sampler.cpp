// SPDX-License-Identifier: MIT
#include "kraftonic/sampler/Sampler.h"

#include "kraftonic/core/Smoother.h"

#include "Interpolator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kraftonic::sampler
{
namespace
{
using core::Event;

/** Control-rate period: pitch bend, kernels and the resonance feedback are
    updated on absolute multiples of this many samples, so the output does not
    depend on the host's block size. */
constexpr int kControlPeriod = 32;
constexpr float kStealSeconds = 0.003f;
constexpr float kSilence = 1.0e-4f; // -80 dB: a releasing voice ends here
constexpr int kVoiceHeadroom = 16;  // slots kept free so a stolen voice can fade out

inline float dbToGain (float db) noexcept
{
    return db <= -96.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
}

/** Crossfade weight of a zone at MIDI velocity `v` (1..127). Linear: adjacent
    layers' weights sum to 1 (constant level for correlated layers). Equal
    power: their squares sum to 1 (constant level for uncorrelated layers). */
inline float velocityWeight (const Zone& z, float v, float width, bool equalPower = false) noexcept
{
    const float lowEdge = static_cast<float> (z.velocityLow) - 0.5f;
    const float highEdge = static_cast<float> (z.velocityHigh) + 0.5f;
    float in = 1.0f, out = 1.0f;
    if (z.velocityLow > 1)
        in = width > 0.0f ? std::clamp ((v - (lowEdge - 0.5f * width)) / width, 0.0f, 1.0f)
                          : (v >= lowEdge ? 1.0f : 0.0f);
    if (z.velocityHigh < 127)
        out = width > 0.0f ? std::clamp (((highEdge + 0.5f * width) - v) / width, 0.0f, 1.0f)
                           : (v < highEdge ? 1.0f : 0.0f);
    if (equalPower)
    {
        in = std::sin (1.5707963f * in);
        out = std::sin (1.5707963f * out);
    }
    return in * out;
}

// ---------------------------------------------------------------------------
//  Sympathetic resonance: while the sustain pedal is down every string is
//  free to ring. Approximated by 12 tuned feedback combs per channel (pitches
//  a fourth apart, so all 12 pitch classes are covered), excited by the dry
//  piano. Lifting the pedal shortens their decay, like dampers landing.
// ---------------------------------------------------------------------------
class PedalResonance
{
public:
    static constexpr int kCombs = 12;

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kCombs; ++i)
            {
                Comb& c = combs[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)];
                const double note = 28.0 + 5.0 * i + (ch == 0 ? 0.0 : 0.11); // right slightly detuned
                const double hz = 440.0 * std::exp2 ((note - 69.0) / 12.0);
                c.delay = static_cast<float> (sampleRate / hz);
                c.buffer.assign (static_cast<std::size_t> (std::ceil (c.delay)) + 4, 0.0f);
            }
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : combs)
            for (auto& c : channel)
            {
                std::fill (c.buffer.begin(), c.buffer.end(), 0.0f);
                c.write = 0;
                c.damp = 0.0f;
            }
        pedal = 0.0f;
        active = false;
    }

    /** Control-rate update: `pedalTarget` 0/1, `amount` 0..1. */
    void update (float pedalTarget, float amount) noexcept
    {
        const float coeff = pedalTarget > pedal ? 0.25f : 0.5f; // per control period
        pedal += coeff * (pedalTarget - pedal);
        if (std::abs (pedal - pedalTarget) < 1.0e-4f)
            pedal = pedalTarget;
        inputGain = 0.012f * amount * pedal;
        const double t60 = 0.12 + 3.2 * static_cast<double> (pedal); // seconds
        for (auto& channel : combs)
            for (auto& c : channel)
                c.feedback = static_cast<float> (std::exp (std::log (0.001) * c.delay / (t60 * rate)));
        if (amount > 0.0f && pedal > 0.0f)
            active = true;
    }

    /** Adds the resonance of (inL, inR) into (outL, outR). */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        if (! active)
            return;
        float energy = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = ch == 0 ? inL : inR;
            float* out = ch == 0 ? outL : outR;
            for (auto& c : combs[static_cast<std::size_t> (ch)])
            {
                const int size = static_cast<int> (c.buffer.size());
                float* buf = c.buffer.data();
                for (int i = 0; i < n; ++i)
                {
                    float readPos = static_cast<float> (c.write) - c.delay;
                    if (readPos < 0.0f)
                        readPos += static_cast<float> (size);
                    const int r0 = static_cast<int> (readPos);
                    const float frac = readPos - static_cast<float> (r0);
                    const int r1 = r0 + 1 >= size ? 0 : r0 + 1;
                    const float delayed = buf[r0] + frac * (buf[r1] - buf[r0]);
                    c.damp += 0.45f * (delayed - c.damp); // loss rises with frequency, like a string
                    const float y = in[i] * inputGain + c.feedback * c.damp;
                    buf[c.write] = y;
                    if (++c.write >= size)
                        c.write = 0;
                    out[i] += y;
                    energy += std::abs (y);
                }
            }
        }
        // Sleep once the pedal is up and the strings have died away.
        if (pedal <= 0.0f && energy < 1.0e-6f)
            active = false;
    }

private:
    struct Comb
    {
        std::vector<float> buffer;
        float delay = 1.0f;
        float feedback = 0.0f;
        float damp = 0.0f;
        int write = 0;
    };
    std::array<std::array<Comb, kCombs>, 2> combs;
    double rate = 48000.0;
    float pedal = 0.0f;
    float inputGain = 0.0f;
    bool active = false;
};

struct Voice
{
    enum class State : std::uint8_t
    {
        idle,
        playing,   // attack, then the sample's own decay or loop
        releasing, // exponential release
        stealing   // fast linear fade before the slot is reused
    };

    State state = State::idle;
    const Sample* sample = nullptr;
    const Zone* zone = nullptr;
    int note = -1;
    Trigger trigger = Trigger::attack;
    double position = 0.0;
    double baseIncrement = 1.0; // source frames per output frame, before bend/tune
    double increment = 1.0;
    int kernel = 0;
    float gain = 0.0f;
    float envelope = 0.0f;
    float attackStep = 0.0f; // > 0 while attacking
    float releaseCoeff = 1.0f;
    float stealStep = 0.0f;
    std::uint64_t startedAt = 0;
};

struct NoteState
{
    bool keyDown = false;
    bool sustained = false; // released while the pedal was down
    float velocity = 0.0f;
    std::uint64_t onAt = 0;
};
} // namespace

struct Sampler::Impl
{
    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::shared_ptr<const SampleBank> bank;
    Parameters params;

    std::array<Voice, kMaxVoices> voices {};
    std::array<NoteState, 128> notes {};
    std::array<std::uint32_t, 32> roundRobin {};
    bool pedalDown = false;
    std::uint64_t clock = 0;
    std::uint64_t voiceCounter = 0;

    float bendTarget = 0.0f;  // semitones incl. global tune
    float bendCurrent = 0.0f;
    double bendFactor = 1.0;
    float bendInput = 0.0f; // last pitchBend event value
    core::LinearSmoother gain, width;
    PedalResonance resonance;
    std::vector<float> dryL, dryR;

    void prepare (const core::ProcessSetup& setup)
    {
        sampleRate = std::clamp (setup.sampleRate, 8000.0, 768000.0);
        maxBlock = std::max (1, setup.maxBlockSize);
        (void) detail::sincTables(); // build the shared tables outside the audio thread
        gain.prepare (sampleRate, 0.02);
        width.prepare (sampleRate, 0.02);
        resonance.prepare (sampleRate);
        dryL.assign (kControlPeriod, 0.0f);
        dryR.assign (kControlPeriod, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& v : voices)
            v = Voice {};
        for (auto& n : notes)
            n = NoteState {};
        roundRobin.fill (0);
        pedalDown = false;
        bendInput = 0.0f;
        bendCurrent = bendTarget = params.tuneCents / 100.0f;
        bendFactor = std::exp2 (static_cast<double> (bendCurrent) / 12.0);
        gain.setImmediate (dbToGain (params.gainDb));
        width.setImmediate (params.stereoWidth);
        resonance.reset();
        clock = 0;
    }

    int polyphony() const noexcept
    {
        const int fromBank = bank ? bank->settings().polyphony : 64;
        const int p = params.polyphony > 0 ? params.polyphony : fromBank;
        return std::clamp (p, 1, kMaxVoices - kVoiceHeadroom);
    }

    float resonanceAmount() const noexcept
    {
        const float fromBank = bank ? bank->settings().pedalResonance : 0.0f;
        return std::clamp (params.pedalResonance >= 0.0f ? params.pedalResonance : fromBank, 0.0f, 1.0f);
    }

    // ---- voices ------------------------------------------------------------
    void beginSteal (Voice& v) noexcept
    {
        v.state = Voice::State::stealing;
        v.attackStep = 0.0f;
        v.stealStep = std::max (v.envelope, 1.0e-3f) / (kStealSeconds * static_cast<float> (sampleRate));
    }

    void beginRelease (Voice& v, float releaseMs) noexcept
    {
        if (v.state != Voice::State::playing)
            return;
        const float seconds = std::max (0.005f, releaseMs * 0.001f * std::clamp (params.releaseScale, 0.1f, 10.0f));
        v.state = Voice::State::releasing;
        v.attackStep = 0.0f;
        v.releaseCoeff = static_cast<float> (std::exp (std::log (0.001) / (static_cast<double> (seconds) * sampleRate)));
    }

    Voice* allocateVoice() noexcept
    {
        int live = 0;
        for (const auto& v : voices)
            if (v.state == Voice::State::playing || v.state == Voice::State::releasing)
                ++live;
        if (live >= polyphony())
        {
            // Steal the quietest releasing voice, else the oldest playing one.
            Voice* victim = nullptr;
            float quietest = 1.0e30f;
            for (auto& v : voices)
                if (v.state == Voice::State::releasing && v.envelope * v.gain < quietest)
                {
                    quietest = v.envelope * v.gain;
                    victim = &v;
                }
            if (victim == nullptr)
                for (auto& v : voices)
                    if (v.state == Voice::State::playing && (victim == nullptr || v.startedAt < victim->startedAt))
                        victim = &v;
            if (victim != nullptr)
                beginSteal (*victim);
        }
        for (auto& v : voices)
            if (v.state == Voice::State::idle)
                return &v;
        // Every slot busy (all fading): reuse the most faded one.
        Voice* best = nullptr;
        for (auto& v : voices)
            if (v.state == Voice::State::stealing && (best == nullptr || v.envelope < best->envelope))
                best = &v;
        return best;
    }

    void startVoice (const Zone& zone, int note, float voiceGain) noexcept
    {
        Voice* slot = allocateVoice();
        if (slot == nullptr || voiceGain <= 0.0f)
            return;
        const Sample& s = bank->sample (zone.sample);
        Voice& v = *slot;
        v = Voice {};
        v.state = Voice::State::playing;
        v.sample = &s;
        v.zone = &zone;
        v.note = note;
        v.trigger = zone.trigger;
        v.position = 0.0;
        const double semitones = static_cast<double> (note - zone.rootKey) * zone.keyTracking + zone.tuneCents / 100.0;
        v.baseIncrement = std::exp2 (semitones / 12.0) * static_cast<double> (s.sampleRate()) / sampleRate;
        v.increment = v.baseIncrement * bendFactor;
        v.kernel = detail::SincTables::kernelFor (v.increment);
        v.gain = voiceGain;
        const float attackSeconds = bank->settings().attackMs * 0.001f;
        if (attackSeconds > 0.0f)
        {
            v.envelope = 0.0f;
            v.attackStep = 1.0f / std::max (1.0f, attackSeconds * static_cast<float> (sampleRate));
        }
        else
            v.envelope = 1.0f;
        v.startedAt = ++voiceCounter;
    }

    /** Starts every zone of `trigger` matching key and velocity (0..1].
        `heldSeconds` makes release zones quieter the longer the note was held. */
    void triggerZones (Trigger trigger, int key, float velocity, float extraDb, float heldSeconds = 0.0f) noexcept
    {
        if (! bank)
            return;
        const auto& list = bank->zonesFor (trigger, key);
        if (list.empty())
            return;
        const InstrumentSettings& settings = bank->settings();
        const float v = std::clamp (velocity * 127.0f, 1.0f, 127.0f);
        const float sensitivity = std::clamp (params.velocitySensitivity, 0.0f, 2.0f);
        const float velocityDb = -settings.velocityRangeDb * sensitivity * (1.0f - (v - 1.0f) / 126.0f);

        // Round robin: count the candidates of each set, then play the
        // set's counter-th candidate.
        constexpr int kMaxSets = 8;
        std::array<int, kMaxSets> setIds {}, setCounts {}, setSeen {};
        int numSets = 0;
        for (int index : list)
        {
            const Zone& z = bank->zones()[static_cast<std::size_t> (index)];
            if (z.roundRobinSet == 0 || velocityWeight (z, v, settings.velocityCrossfade) <= 0.0f)
                continue;
            int s = 0;
            while (s < numSets && setIds[static_cast<std::size_t> (s)] != z.roundRobinSet)
                ++s;
            if (s == numSets && numSets < kMaxSets)
                setIds[static_cast<std::size_t> (numSets++)] = z.roundRobinSet;
            if (s < numSets)
                ++setCounts[static_cast<std::size_t> (s)];
        }

        int started = 0;
        for (int index : list)
        {
            const Zone& z = bank->zones()[static_cast<std::size_t> (index)];
            const float weight = velocityWeight (z, v, settings.velocityCrossfade, settings.equalPowerCrossfade);
            if (weight <= 0.0f)
                continue;
            if (z.roundRobinSet != 0)
            {
                int s = 0;
                while (s < numSets && setIds[static_cast<std::size_t> (s)] != z.roundRobinSet)
                    ++s;
                if (s == numSets)
                    continue;
                const auto si = static_cast<std::size_t> (s);
                const auto pick = roundRobin[static_cast<std::size_t> (z.roundRobinSet) & 31u]
                    % static_cast<std::uint32_t> (setCounts[si]);
                const bool chosen = static_cast<std::uint32_t> (setSeen[si]) == pick;
                ++setSeen[si];
                if (! chosen)
                    continue;
            }
            const float db = settings.gainDb + z.gainDb + velocityDb + extraDb - z.releaseDecayDbPerSecond * heldSeconds;
            startVoice (z, key, dbToGain (db) * weight);
            if (++started >= 16)
                break;
        }
        for (int s = 0; s < numSets; ++s)
            ++roundRobin[static_cast<std::size_t> (setIds[static_cast<std::size_t> (s)]) & 31u];
    }

    /** The key's damper falls: release its voices and fire release zones. */
    void damperFall (int note, bool withReleaseTriggers) noexcept
    {
        if (! bank)
            return;
        for (auto& v : voices)
            if (v.note == note && v.state == Voice::State::playing && v.trigger == Trigger::attack && v.zone->damped)
                beginRelease (v, v.zone->releaseMs >= 0.0f ? v.zone->releaseMs : bank->settings().releaseMs);
        NoteState& n = notes[static_cast<std::size_t> (note)];
        if (withReleaseTriggers && bank->hasReleaseZones())
        {
            const float heldSeconds = std::min (30.0f, static_cast<float> (static_cast<double> (clock - n.onAt) / sampleRate));
            triggerZones (Trigger::release, note, n.velocity,
                          bank->settings().releaseTriggerGainDb + params.releaseNoiseDb, heldSeconds);
        }
        n.sustained = false;
    }

    // ---- events ------------------------------------------------------------
    void handle (const Event& e) noexcept
    {
        switch (e.type)
        {
            case Event::Type::noteOn:
            {
                if (e.note < 0 || e.note > 127)
                    break;
                if (! (e.value > 0.0f))
                {
                    noteOff (e.note);
                    break;
                }
                if (! bank)
                    break;
                // A re-struck note: its previous sound fades instead of being cut.
                for (auto& v : voices)
                    if (v.note == e.note && v.state == Voice::State::playing && v.trigger == Trigger::attack)
                        beginRelease (v, bank->settings().retriggerReleaseMs);
                NoteState& n = notes[static_cast<std::size_t> (e.note)];
                n.keyDown = true;
                n.sustained = false;
                n.velocity = std::clamp (e.value, 0.0f, 1.0f);
                n.onAt = clock;
                triggerZones (Trigger::attack, e.note, n.velocity, 0.0f);
                break;
            }
            case Event::Type::noteOff:
                if (e.note >= 0 && e.note <= 127)
                    noteOff (e.note);
                break;
            case Event::Type::sustainPedal:
            {
                const bool down = e.value >= 0.5f;
                if (down == pedalDown)
                    break;
                pedalDown = down;
                if (! bank)
                    break;
                if (down)
                    triggerZones (Trigger::pedalDown, 0, 0.8f, 0.0f);
                else
                {
                    triggerZones (Trigger::pedalUp, 0, 0.8f, 0.0f);
                    for (int note = 0; note < 128; ++note)
                        if (notes[static_cast<std::size_t> (note)].sustained && ! notes[static_cast<std::size_t> (note)].keyDown)
                            damperFall (note, true);
                }
                break;
            }
            case Event::Type::allNotesOff:
                pedalDown = false;
                for (int note = 0; note < 128; ++note)
                {
                    notes[static_cast<std::size_t> (note)].keyDown = false;
                    notes[static_cast<std::size_t> (note)].sustained = false;
                }
                if (bank)
                    for (auto& v : voices)
                        if (v.state == Voice::State::playing)
                            beginRelease (v, v.zone->releaseMs >= 0.0f ? v.zone->releaseMs : bank->settings().releaseMs);
                break;
            case Event::Type::allSoundOff:
                for (auto& v : voices)
                    if (v.state != Voice::State::idle)
                        beginSteal (v);
                for (auto& n : notes)
                    n = NoteState {};
                pedalDown = false;
                resonance.reset();
                break;
            case Event::Type::pitchBend:
                bendInput = std::clamp (e.value, -1.0f, 1.0f);
                break;
            case Event::Type::modWheel:
            case Event::Type::polyPressure:
            case Event::Type::channelPressure:
                break;
        }
    }

    void noteOff (int note) noexcept
    {
        NoteState& n = notes[static_cast<std::size_t> (note)];
        if (! n.keyDown)
            return;
        n.keyDown = false;
        if (pedalDown)
            n.sustained = true;
        else
            damperFall (note, true);
    }

    // ---- rendering ---------------------------------------------------------
    void controlUpdate() noexcept
    {
        bendTarget = bendInput * std::clamp (params.pitchBendSemitones, 0.0f, 48.0f) + params.tuneCents / 100.0f;
        bendCurrent += 0.35f * (bendTarget - bendCurrent); // ~2 ms per step at 48 kHz: smooth, no zipper
        if (std::abs (bendTarget - bendCurrent) < 1.0e-5f)
            bendCurrent = bendTarget;
        bendFactor = std::exp2 (static_cast<double> (bendCurrent) / 12.0);
        for (auto& v : voices)
            if (v.state != Voice::State::idle)
            {
                v.increment = v.baseIncrement * bendFactor;
                v.kernel = detail::SincTables::kernelFor (v.increment);
            }
        resonance.update (pedalDown ? 1.0f : 0.0f, resonanceAmount());
    }

    /** Adds voice `v` into (l, r) for n samples. */
    static void renderVoice (Voice& v, float* l, float* r, int n, const detail::SincTables& tables) noexcept
    {
        const Sample& s = *v.sample;
        const std::int16_t* left = s.channel (0);
        const std::int16_t* right = s.channel (1);
        const bool stereo = s.channels() > 1;
        const auto frames = static_cast<double> (s.frames());
        const bool looped = s.isLooped();
        const auto loopStart = static_cast<double> (s.loopStart());
        const auto loopEnd = static_cast<double> (s.loopEnd());
        const float scale = v.gain * (1.0f / 32768.0f);
        float coeffs[detail::kInterpolationTaps];

        for (int i = 0; i < n; ++i)
        {
            // Envelope.
            switch (v.state)
            {
                case Voice::State::playing:
                    if (v.attackStep > 0.0f)
                    {
                        v.envelope += v.attackStep;
                        if (v.envelope >= 1.0f)
                        {
                            v.envelope = 1.0f;
                            v.attackStep = 0.0f;
                        }
                    }
                    break;
                case Voice::State::releasing:
                    v.envelope *= v.releaseCoeff;
                    if (v.envelope < kSilence)
                    {
                        v.state = Voice::State::idle;
                        return;
                    }
                    break;
                case Voice::State::stealing:
                    v.envelope -= v.stealStep;
                    if (v.envelope <= 0.0f)
                    {
                        v.state = Voice::State::idle;
                        return;
                    }
                    break;
                case Voice::State::idle:
                    return;
            }

            const auto index = static_cast<std::int64_t> (v.position);
            const auto frac = static_cast<float> (v.position - static_cast<double> (index));
            tables.coefficients (v.kernel, frac, coeffs);
            const std::int16_t* pl = left + index - (detail::kHalfTaps - 1);
            float sl = 0.0f;
            for (int j = 0; j < detail::kInterpolationTaps; ++j)
                sl += coeffs[j] * static_cast<float> (pl[j]);
            float sr = sl;
            if (stereo)
            {
                const std::int16_t* pr = right + index - (detail::kHalfTaps - 1);
                sr = 0.0f;
                for (int j = 0; j < detail::kInterpolationTaps; ++j)
                    sr += coeffs[j] * static_cast<float> (pr[j]);
            }
            const float g = scale * v.envelope;
            l[i] += sl * g;
            r[i] += sr * g;

            v.position += v.increment;
            if (looped)
            {
                if (v.position >= loopEnd)
                    v.position = loopStart + std::fmod (v.position - loopStart, loopEnd - loopStart);
            }
            else if (v.position >= frames)
            {
                v.state = Voice::State::idle;
                return;
            }
        }
    }

    void renderSegment (float* l, float* r, int n) noexcept
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        const auto& tables = detail::sincTables();
        for (auto& v : voices)
            if (v.state != Voice::State::idle)
                renderVoice (v, l, r, n, tables);

        // Sympathetic resonance (dry copy as excitation).
        std::copy (l, l + n, dryL.data());
        std::copy (r, r + n, dryR.data());
        resonance.process (dryL.data(), dryR.data(), l, r, n);

        for (int i = 0; i < n; ++i)
        {
            const float g = gain.next();
            const float w = width.next();
            const float mid = 0.5f * (l[i] + r[i]);
            const float side = 0.5f * (l[i] - r[i]) * w;
            float outL = (mid + side) * g;
            float outR = (mid - side) * g;
            if (! std::isfinite (outL))
                outL = 0.0f;
            if (! std::isfinite (outR))
                outR = 0.0f;
            l[i] = outL;
            r[i] = outR;
        }
        clock += static_cast<std::uint64_t> (n);
    }

    void process (const core::AudioBlock& block, const Event* events, int numEvents) noexcept
    {
        const int n = block.numSamples;
        if (events == nullptr)
            numEvents = 0;
        if (n <= 0 || block.left == nullptr)
        {
            for (int e = 0; e < numEvents; ++e)
                handle (events[e]);
            return;
        }
        float* l = block.left;
        float* r = block.right != nullptr && block.right != block.left ? block.right : nullptr;
        float scratch[kControlPeriod];

        int at = 0, next = 0;
        while (at < n)
        {
            while (next < numEvents && std::clamp (events[next].sampleOffset, 0, n - 1) <= at)
                handle (events[next++]);
            if (clock % kControlPeriod == 0)
                controlUpdate();
            int end = std::min (n, at + kControlPeriod - static_cast<int> (clock % kControlPeriod));
            if (next < numEvents)
                end = std::min (end, std::max (at + 1, std::clamp (events[next].sampleOffset, 0, n - 1)));
            const int count = end - at;
            renderSegment (l + at, r != nullptr ? r + at : scratch, count);
            if (r == nullptr && block.right == block.left)
                for (int i = 0; i < count; ++i)
                    l[at + i] = 0.5f * (l[at + i] + scratch[i]);
            at = end;
        }
        while (next < numEvents)
            handle (events[next++]);
    }
};

Sampler::Sampler() : impl (std::make_unique<Impl>()) {}
Sampler::~Sampler() = default;

void Sampler::prepare (const core::ProcessSetup& setup)
{
    impl->prepare (setup);
}

void Sampler::setBank (std::shared_ptr<const SampleBank> newBank)
{
    impl->bank = std::move (newBank);
    impl->reset();
}

const SampleBank* Sampler::bank() const noexcept
{
    return impl->bank.get();
}

void Sampler::reset() noexcept
{
    impl->reset();
}

void Sampler::setParameters (const Parameters& params) noexcept
{
    Parameters p = params;
    auto finiteOr = [] (float v, float fallback) { return std::isfinite (v) ? v : fallback; };
    p.gainDb = std::clamp (finiteOr (p.gainDb, 0.0f), -96.0f, 24.0f);
    p.tuneCents = std::clamp (finiteOr (p.tuneCents, 0.0f), -2400.0f, 2400.0f);
    p.pitchBendSemitones = finiteOr (p.pitchBendSemitones, 2.0f);
    p.releaseScale = finiteOr (p.releaseScale, 1.0f);
    p.velocitySensitivity = finiteOr (p.velocitySensitivity, 1.0f);
    p.pedalResonance = finiteOr (p.pedalResonance, -1.0f);
    p.releaseNoiseDb = std::clamp (finiteOr (p.releaseNoiseDb, 0.0f), -96.0f, 24.0f);
    p.stereoWidth = std::clamp (finiteOr (p.stereoWidth, 1.0f), 0.0f, 2.0f);
    impl->params = p;
    impl->gain.setTarget (dbToGain (p.gainDb));
    impl->width.setTarget (p.stereoWidth);
}

void Sampler::process (const core::AudioBlock& block, const core::Event* events, int numEvents) noexcept
{
    impl->process (block, events, numEvents);
}

int Sampler::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& v : impl->voices)
        if (v.state != Voice::State::idle)
            ++count;
    return count;
}
} // namespace kraftonic::sampler
