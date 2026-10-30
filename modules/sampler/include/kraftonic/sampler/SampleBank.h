// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic sampler: SampleBank, one sampled instrument held in memory.
//
//  A bank is built NOT in real time, either by loading an instrument folder
//  (an `instrument.ini` manifest plus WAV files, see modules/sampler/README.md)
//  or programmatically (tests, tools). After finalize() it is immutable and
//  shared read-only (std::shared_ptr<const SampleBank>) between any number of
//  Sampler engines, so two sounds can play the same recordings.
//
//  Memory: audio is kept as 16-bit planar PCM (half the RAM of float).
//  Streaming from disk can be added later behind the same API: Sample would
//  hold only a preloaded head (LoadOptions::preloadFrames) and the Sampler
//  would read the rest through a background reader. Callers keep calling
//  SampleBank::load() and Sampler::setBank() exactly as now.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kraftonic::sampler
{
/** Frames of guard audio stored before and after every sample, so the
    interpolator never reads out of bounds. Must be >= kInterpolationTaps / 2 + 1. */
inline constexpr int kSamplePadding = 16;

/** One recording: mono or stereo 16-bit PCM. */
class Sample
{
public:
    /** Converts float audio ([channel][frame], 1 or 2 channels) to 16-bit.
        If 0 <= loopStart < loopEnd <= frames, the sample loops: audio after
        loopEnd is dropped and the guard frames after it continue from
        loopStart, so interpolation across the loop point is seamless. */
    static Sample fromFloat (const std::vector<std::vector<float>>& audio, int sampleRate,
                             std::int64_t loopStart = -1, std::int64_t loopEnd = -1);

    int channels() const noexcept { return numChannels; }
    int sampleRate() const noexcept { return rate; }
    std::int64_t frames() const noexcept { return numFrames; }
    bool isLooped() const noexcept { return loopEndFrame > loopStartFrame; }
    std::int64_t loopStart() const noexcept { return loopStartFrame; }
    std::int64_t loopEnd() const noexcept { return loopEndFrame; }

    /** Frame 0 of channel `c` (clamped to the last channel). Frames
        [-kSamplePadding, frames() + kSamplePadding) are readable. */
    const std::int16_t* channel (int c) const noexcept
    {
        const int index = c < numChannels ? c : numChannels - 1;
        return data.data() + static_cast<std::size_t> (index) * stride() + kSamplePadding;
    }

    /** Peak absolute value in [0, 1]. */
    float peak() const noexcept { return peakValue; }
    std::size_t memoryBytes() const noexcept { return data.size() * sizeof (std::int16_t); }

private:
    std::size_t stride() const noexcept { return static_cast<std::size_t> (numFrames) + 2u * kSamplePadding; }

    std::vector<std::int16_t> data;
    std::int64_t numFrames = 0;
    std::int64_t loopStartFrame = -1;
    std::int64_t loopEndFrame = -1;
    int numChannels = 0;
    int rate = 44100;
    float peakValue = 0.0f;
};

enum class Trigger : std::uint8_t
{
    attack,    // note on
    release,   // key released (damper falls), e.g. piano hammer/damper noise
    pedalDown, // sustain pedal pressed (key range ignored)
    pedalUp    // sustain pedal lifted (key range ignored)
};

/** Where and how a sample plays. Velocities are MIDI units 1..127. */
struct Zone
{
    int sample = -1; // index into the bank's samples
    int keyLow = 0;
    int keyHigh = 127;
    int rootKey = 60;
    int velocityLow = 1;
    int velocityHigh = 127;
    float tuneCents = 0.0f;
    float gainDb = 0.0f;
    float keyTracking = 1.0f; // 1 = chromatic, 0 = fixed pitch (noises)
    Trigger trigger = Trigger::attack;
    int roundRobinSet = 0;     // zones sharing a non-zero set alternate (one per trigger)
    float releaseMs = -1.0f;   // < 0: instrument default
    bool damped = true;        // false: note-off doesn't stop it (undamped top piano strings)
    float releaseDecayDbPerSecond = 0.0f; // release triggers: quieter the longer the note was held
};

/** Instrument-wide playing behaviour, from the manifest's [instrument] section. */
struct InstrumentSettings
{
    std::string name;
    float gainDb = 0.0f;
    float velocityRangeDb = 12.0f;   // level difference between velocity 127 and 1
    float velocityCrossfade = 10.0f; // width of crossfades between velocity layers (MIDI units)
    bool equalPowerCrossfade = false; // true for uncorrelated layers (ensembles); false = linear (correlated)
    float attackMs = 1.0f;
    float releaseMs = 250.0f;
    float retriggerReleaseMs = 400.0f; // a re-struck note fades its previous voice in this time
    float pedalResonance = 0.0f;       // 0..1 sympathetic string resonance while the pedal is down
    float releaseTriggerGainDb = 0.0f;
    int polyphony = 64;
};

struct LoadOptions
{
    /** Reserved for disk streaming: 0 = load whole samples (the only mode in v1). */
    std::int64_t preloadFrames = 0;
};

struct LoadReport
{
    bool ok = false;
    std::string error;                 // why loading failed (ok == false)
    std::vector<std::string> warnings; // skipped zones, unknown keys, ...
    int zones = 0;
    int samples = 0;
    std::size_t memoryBytes = 0;
};

class SampleBank
{
public:
    /** NOT real-time. Loads `<directory>/instrument.ini` and its samples.
        Returns nullptr (and report.error) if the folder or manifest is
        missing, or no playable zone could be loaded. Zones whose file is
        missing or corrupt are skipped with a warning. Never throws. */
    static std::shared_ptr<const SampleBank> load (const std::string& directoryUtf8, LoadReport& report,
                                                   const LoadOptions& options = {});

    /** Parses manifest text; `directory` resolves relative sample paths. */
    static std::shared_ptr<const SampleBank> loadFromManifest (const std::string& manifestText,
                                                               const std::string& directoryUtf8,
                                                               LoadReport& report,
                                                               const LoadOptions& options = {});

    // ---- building (NOT real-time) ------------------------------------------
    int addSample (Sample sample);
    void addZone (const Zone& zone);
    InstrumentSettings& settings() noexcept { return instrument; }
    /** Validates zones (bad ones are dropped) and builds the key lookup.
        Returns false if nothing playable remains. */
    bool finalize (std::vector<std::string>* warnings = nullptr);

    // ---- queries (real-time safe) ------------------------------------------
    const InstrumentSettings& settings() const noexcept { return instrument; }
    const std::vector<Zone>& zones() const noexcept { return zoneList; }
    const Sample& sample (int index) const noexcept { return samples[static_cast<std::size_t> (index)]; }
    int numSamples() const noexcept { return static_cast<int> (samples.size()); }

    /** Zone indices for `key` with the given trigger (pedal triggers ignore the key). */
    const std::vector<int>& zonesFor (Trigger trigger, int key) const noexcept;

    bool hasReleaseZones() const noexcept { return anyRelease; }
    std::size_t memoryBytes() const noexcept;

private:
    static std::shared_ptr<const SampleBank> parseAndLoad (const std::string& manifestText,
                                                           const std::string& directoryUtf8,
                                                           LoadReport& report, const LoadOptions& options);

    InstrumentSettings instrument;
    std::vector<Sample> samples;
    std::vector<Zone> zoneList;
    // [trigger][key] -> zone indices. Pedal triggers use key 0 only.
    std::array<std::array<std::vector<int>, 128>, 4> byKey;
    bool anyRelease = false;
};
} // namespace kraftonic::sampler
