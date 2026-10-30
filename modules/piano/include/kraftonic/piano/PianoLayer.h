// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic piano module: PianoLayer, one playable piano (shared contract
//  once a second product uses it; changes need a maintainer's agreement in an issue).
//
//  A layer plays every sound in PianoSounds.h: the modelled pianos with 16
//  voices, and the sampled grands through modules/sampler. After the voices
//  it runs the piano bus: dynamic compression, detuned doubling, string
//  (sympathetic) resonance, pedal noise and the layer level. Products keep
//  their own parameter ids, key routing and effects.
//
//  Lifecycle (one thread at a time):
//      PianoLayer layer;
//      layer.setGrandBank (PianoLayer::loadGrandBank (root)); // NOT real-time
//      layer.setParameters (params);
//      layer.prepare (sampleRate, maxBlockSize);              // NOT real-time
//      loop (real-time):
//          layer.setParameters (params);   // once per block is enough
//          layer.noteOn/noteOff/sustain/allNotesOff (...); // between process calls
//          layer.process (left, right, numSamples);
//
//  Events take effect at the next sample processed, so a host that wants
//  sample-accurate timing splits process() at each event's offset.
//  process() may also be split into renderVoices() then processBus(), for a
//  product that inserts its own effect between the two (a tremolo, say).
//
//  The other sampled models (Harpsichord, Vibes, Marimba) load the same way
//  from their own folder: setModelBank (model, loadModelBank (root, model)).
//
//  Real-time guarantees for everything except prepare(), setGrandBank(),
//  loadGrandBank(), setModelBank() and loadModelBank(): no allocation, locks, I/O or exceptions. Output is
//  finite. The output for an event/parameter stream does not depend on how
//  it is split into process() calls.
// ============================================================================

#include <kraftonic/piano/PianoSounds.h>

#include <memory>
#include <string>

namespace kraftonic::sampler
{
class SampleBank;
}

namespace kraftonic::piano
{
inline constexpr int kMaxVoices = 16;

/** Velocity curve applied to every note-on. */
enum class Touch : std::uint8_t
{
    light,  // plays louder: velocity^0.7
    normal, // as played
    heavy,  // needs force: v^2 (0.5 + 0.5 v)
    fixed   // every note at velocity 100 of 127
};

/** Voicing offset added to the tone control. */
enum class Timbre : std::uint8_t
{
    soft,   // tone - 0.25
    mid,    // tone as set
    bright  // tone + 0.25
};

struct Parameters
{
    Type type = Type::electric;
    int model = 0;                     // within `type`, see modelCount()
    float tone = 0.5f;                 // 0 dark .. 1 bright (smoothed)
    Timbre timbre = Timbre::mid;
    float strike = 0.35f;              // 0..1 strike noise / tine bell / grand hammer noise (smoothed)
    float velocitySensitivity = 0.7f;  // 0..1 (smoothed)
    Touch touch = Touch::normal;
    // Envelope of the modelled sounds (smoothed). The sampled grands use the
    // release time only.
    float attack = 0.002f;             // seconds, 0.001..0.5
    float decay = 6.0f;                // seconds, 0.2..20
    float sustain = 0.2f;              // 0..1
    float release = 0.35f;             // seconds, 0.01..5
    bool softRelease = false;          // every release about a third as long
    bool dynamicCompression = false;   // gentle 3:1 peak compression above -18 dBFS
    bool doubling = false;             // detuned doubling (a short wobbling delay)
    float stringResonance = 0.0f;      // 0..1 sympathetic resonance, opened by the pedal and held keys
    float pedalNoise = 0.0f;           // 0..1 mechanical noise on both pedal edges
    float level = 1.0f;                // 0..2 linear output gain (smoothed)
    int octave = 0;                    // -3..+3, applied at note-on
};

class PianoLayer
{
public:
    using Bank = std::shared_ptr<const sampler::SampleBank>;

    /** Name of the grand's instrument folder inside a sample root. */
    static constexpr const char* kGrandFolder = "grand";

    /** NOT real-time. Loads `<rootUtf8>/grand`; null when it is missing or
        invalid (the grands then play their stand-ins). */
    static Bank loadGrandBank (const std::string& rootUtf8);

    /** Instrument folder of a sampled kModels entry other than the grands
        ("harpsichord", "vibes", "marimba"), or nullptr. */
    static const char* modelFolder (int model) noexcept;

    /** NOT real-time. Loads `<rootUtf8>/<modelFolder (model)>`; null when it
        is missing or invalid, or `model` has no folder (that model then
        plays its stand-in). */
    static Bank loadModelBank (const std::string& rootUtf8, int model);

    PianoLayer();
    ~PianoLayer();
    PianoLayer (const PianoLayer&) = delete;
    PianoLayer& operator= (const PianoLayer&) = delete;

    /** NOT real-time. The sampled grands' recordings (null = none). Takes
        effect at the next prepare(). */
    void setGrandBank (Bank bank);

    /** NOT real-time. The recordings of sampled model `model` (a kModels
        index with a modelFolder(); others are ignored). Null = none. Takes
        effect at the next prepare(). */
    void setModelBank (int model, Bank bank);

    /** NOT real-time: may allocate. Silences everything and jumps the
        smoothed controls to the current parameters. `sampleRate` is clamped
        to 22.05..384 kHz. process() calls may be longer than maxBlockSize. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Immediate silence: voices, pedal, keys and every tail. */
    void reset() noexcept;

    void setParameters (const Parameters& params) noexcept;

    /** `velocity` 0..1 before the touch curve; 0 is a note-off. */
    void noteOn (int key, float velocity) noexcept;
    void noteOff (int key) noexcept;
    /** CC64 as 0..1. At or above 0.5 the dampers lift. */
    void sustain (float value) noexcept;
    /** Continuous pedal depth for products that support half-pedal response. */
    void setPedalDepth (float amount01) noexcept;
    /** Releases every key and the pedal (tails ring out). */
    void allNotesOff() noexcept;

    /** Replaces `left`/`right` with the layer's output. */
    void process (float* left, float* right, int numSamples) noexcept;
    /** The two halves of process(): the voices (replacing the buffers), then
        the piano bus in place. Always call both, in this order. */
    void renderVoices (float* left, float* right, int numSamples) noexcept;
    void processBus (float* left, float* right, int numSamples) noexcept;

    /** Voices or grand tails are sounding (after the last renderVoices). */
    bool isSounding() const noexcept;
    int activeVoiceCount() const noexcept;
    /** The grand bank loaded at the last prepare(). */
    bool hasGrand() const noexcept;
    /** The selected model is sampled but its data (loaded at the last
        prepare()) is missing. */
    bool samplesMissing() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace kraftonic::piano
