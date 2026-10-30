// SPDX-License-Identifier: MIT
//
// "sampler: realtime and performance": zero allocations in process() and a
// CPU gate (a dense pedalled passage must render >= 3x faster than real
// time), using a synthetic piano-sized bank so it runs without sample data.
//
//   kraftonic_sampler_realtime                         the test (ctest)
//   kraftonic_sampler_realtime --render <out> <root>   listening renders of the
//       installed instruments in <root> (grand, strings, brass), 48 kHz float WAV
//   kraftonic_sampler_realtime --bench <root>          CPU and RAM of the real banks

#include "kraftonic/sampler/SampleBank.h"
#include "kraftonic/sampler/SampleLocations.h"
#include "kraftonic/sampler/Sampler.h"
#include "kraftonic/sampler/WavFile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace
{
bool tracking = false;
std::size_t allocations = 0;
} // namespace

void* operator new (std::size_t size)
{
    if (tracking)
        ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size)
{
    if (tracking)
        ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept
{
    if (tracking)
        ++allocations;
    std::free (p);
}
void operator delete[] (void* p) noexcept
{
    if (tracking)
        ++allocations;
    std::free (p);
}
void operator delete (void* p, std::size_t) noexcept
{
    if (tracking)
        ++allocations;
    std::free (p);
}
void operator delete[] (void* p, std::size_t) noexcept
{
    if (tracking)
        ++allocations;
    std::free (p);
}

namespace
{
using namespace kraftonic::sampler;
using kraftonic::core::Event;

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/** 30 roots x 4 velocity layers of 4 s decaying stereo harmonics, plus
    chromatic release noises and pedal noises: shaped like the real grand. */
std::shared_ptr<const SampleBank> syntheticGrand()
{
    auto bank = std::make_shared<SampleBank>();
    auto& s = bank->settings();
    s.velocityCrossfade = 10.0f;
    s.velocityRangeDb = 12.0f;
    s.releaseMs = 300.0f;
    s.pedalResonance = 0.3f;
    s.polyphony = 128;
    s.gainDb = -12.0f;
    std::mt19937 random (5);
    constexpr int frames = 44100 * 4;
    for (int root = 21; root <= 108; root += 3)
        for (int layer = 0; layer < 4; ++layer)
        {
            const double hz = 440.0 * std::exp2 ((root - 69) / 12.0);
            std::vector<std::vector<float>> audio (2, std::vector<float> (frames));
            for (int i = 0; i < frames; ++i)
            {
                const double t = i / 44100.0;
                double v = 0.0;
                for (int h = 1; h <= 6; ++h)
                    if (hz * h < 20000.0)
                        v += std::sin (2.0 * 3.14159265 * hz * h * t) * std::exp (-t * (1.0 + h)) / h;
                audio[0][static_cast<std::size_t> (i)] = static_cast<float> (0.2 * (layer + 1) * v / 4.0);
                audio[1][static_cast<std::size_t> (i)] = static_cast<float> (0.2 * (layer + 1) * v / 4.0 * 0.9);
            }
            const int index = bank->addSample (Sample::fromFloat (audio, 44100));
            Zone z;
            z.sample = index;
            z.rootKey = root;
            z.keyLow = std::max (0, root - 1);
            z.keyHigh = std::min (127, root + 1);
            z.velocityLow = 1 + layer * 32;
            z.velocityHigh = layer == 3 ? 127 : layer * 32 + 32;
            bank->addZone (z);
        }
    std::vector<std::vector<float>> noise (2, std::vector<float> (20000));
    for (auto& channel : noise)
        for (std::size_t i = 0; i < channel.size(); ++i)
            channel[i] = 0.02f * std::exp (-static_cast<float> (i) / 4000.0f) * (static_cast<float> (random() % 2001) / 1000.0f - 1.0f);
    const int noiseIndex = bank->addSample (Sample::fromFloat (noise, 44100));
    Zone release;
    release.sample = noiseIndex;
    release.trigger = Trigger::release;
    release.keyTracking = 0.0f;
    bank->addZone (release);
    Zone pedalNoise = release;
    pedalNoise.trigger = Trigger::pedalDown;
    bank->addZone (pedalNoise);
    bank->finalize();
    return bank;
}

/** A dense, pedalled passage: chords of 6 notes every 120 ms, pedal changes,
    pitch bends. Returns events with absolute offsets. */
std::vector<Event> densePassage (int seconds, int lowKey, int highKey)
{
    std::vector<Event> events;
    std::mt19937 random (9);
    const int total = static_cast<int> (kRate) * seconds;
    for (int at = 0, chord = 0; at < total - 6000; at += 5760, ++chord)
    {
        if (chord % 8 == 0)
            events.push_back ({ Event::Type::sustainPedal, at, 0, 0.0f });
        if (chord % 8 == 1)
            events.push_back ({ Event::Type::sustainPedal, at, 0, 1.0f });
        for (int n = 0; n < 6; ++n)
        {
            const int note = lowKey + static_cast<int> (random() % static_cast<unsigned> (highKey - lowKey + 1));
            events.push_back ({ Event::Type::noteOn, at + n * 37, note, 0.3f + 0.7f * static_cast<float> (random() % 100) / 100.0f });
            events.push_back ({ Event::Type::noteOff, at + 4000 + n * 51, note, 0.0f });
        }
        if (chord % 5 == 0)
            events.push_back ({ Event::Type::pitchBend, at + 100, 0, static_cast<float> (random() % 100) / 200.0f });
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sampleOffset < b.sampleOffset; });
    return events;
}

struct Result
{
    std::vector<float> left, right;
    double seconds = 0.0; // wall time of process() calls
    int peakVoices = 0;
    bool finite = true;
};

Result play (Sampler& sampler, const std::vector<Event>& events, int frames)
{
    Result r;
    r.left.assign (static_cast<std::size_t> (frames), 0.0f);
    r.right.assign (static_cast<std::size_t> (frames), 0.0f);
    std::vector<Event> local;
    local.reserve (kraftonic::core::kMaxEventsPerBlock);
    std::size_t next = 0;
    for (int at = 0; at < frames; at += kBlock)
    {
        const int n = std::min (kBlock, frames - at);
        local.clear();
        while (next < events.size() && events[next].sampleOffset < at + n && local.size() < local.capacity())
        {
            Event e = events[next++];
            e.sampleOffset -= at;
            local.push_back (e);
        }
        const auto start = std::chrono::steady_clock::now();
        tracking = true;
        sampler.process ({ r.left.data() + at, r.right.data() + at, n }, local.data(), static_cast<int> (local.size()));
        tracking = false;
        r.seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        r.peakVoices = std::max (r.peakVoices, sampler.activeVoiceCount());
    }
    for (std::size_t i = 0; i < r.left.size(); ++i)
        r.finite = r.finite && std::isfinite (r.left[i]) && std::isfinite (r.right[i]);
    return r;
}

float peakDb (const Result& r)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < r.left.size(); ++i)
        peak = std::max ({ peak, std::abs (r.left[i]), std::abs (r.right[i]) });
    return 20.0f * std::log10 (std::max (peak, 1e-9f));
}

int test()
{
    const auto bank = syntheticGrand();
    Sampler sampler;
    sampler.prepare ({ kRate, kBlock });
    sampler.setBank (bank);
    sampler.setParameters ({});
    constexpr int seconds = 20;
    const auto events = densePassage (seconds, 30, 100);
    allocations = 0;
    const Result r = play (sampler, events, static_cast<int> (kRate) * seconds);
    const double realtime = seconds / r.seconds;
    std::printf ("synthetic grand: %d s pedalled passage, peak %d voices, %.1fx real time "
                 "(%.1f%% of one core), %.0f ns per voice-sample (avg), allocations=%zu, peak %.1f dBFS, bank %.1f MB\n",
                 seconds, r.peakVoices, realtime, 100.0 / realtime,
                 r.seconds * 1e9 / (static_cast<double> (seconds) * kRate * std::max (1, r.peakVoices / 2)),
                 allocations, static_cast<double> (peakDb (r)), static_cast<double> (bank->memoryBytes()) / 1e6);
    bool ok = true;
    if (allocations != 0)
    {
        std::fprintf (stderr, "FAIL: process() allocated %zu times\n", allocations);
        ok = false;
    }
    if (! r.finite)
    {
        std::fprintf (stderr, "FAIL: non-finite output\n");
        ok = false;
    }
    if (r.peakVoices < 60)
    {
        std::fprintf (stderr, "FAIL: the passage only reached %d voices (expected a dense load)\n", r.peakVoices);
        ok = false;
    }
    if (realtime < 3.0)
    {
        std::fprintf (stderr, "FAIL: %.2fx real time is below the 3x gate\n", realtime);
        ok = false;
    }
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  Listening renders and benchmarks with the real (installed or built) banks
// ---------------------------------------------------------------------------
std::vector<Event> scale (int low, int high, int step, int noteSamples, int gapSamples, float velocity, int start = 0)
{
    std::vector<Event> events;
    int at = start;
    for (int note = low; note <= high; note += step, at += noteSamples + gapSamples)
    {
        events.push_back ({ Event::Type::noteOn, at, note, velocity });
        events.push_back ({ Event::Type::noteOff, at + noteSamples, note, 0.0f });
    }
    return events;
}

void sortEvents (std::vector<Event>& events)
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sampleOffset < b.sampleOffset; });
}

std::vector<Event> chords (const std::vector<std::vector<int>>& progression, int chordSamples, float velocity,
                           bool pedalled, int start = 0)
{
    std::vector<Event> events;
    int at = start;
    for (const auto& chord : progression)
    {
        if (pedalled)
        {
            events.push_back ({ Event::Type::sustainPedal, at + 200, 0, 0.0f });
            events.push_back ({ Event::Type::sustainPedal, at + 2400, 0, 1.0f });
        }
        for (std::size_t i = 0; i < chord.size(); ++i)
        {
            const int roll = pedalled ? static_cast<int> (i) * 240 : 0;
            events.push_back ({ Event::Type::noteOn, at + roll, chord[i], velocity });
            events.push_back ({ Event::Type::noteOff, at + chordSamples - 2400, chord[i], 0.0f });
        }
        at += chordSamples;
    }
    if (pedalled)
        events.push_back ({ Event::Type::sustainPedal, at + 2400, 0, 0.0f });
    sortEvents (events);
    return events;
}

bool writeRender (const std::string& path, const Result& r)
{
    std::string error;
    if (! writeWav (path, { r.left, r.right }, static_cast<int> (kRate), WavFormat::float32, error))
    {
        std::fprintf (stderr, "%s: %s\n", path.c_str(), error.c_str());
        return false;
    }
    std::printf ("  %s  peak %.1f dBFS, %d voices max, %.1f%% CPU\n", path.c_str(), static_cast<double> (peakDb (r)),
                 r.peakVoices, 100.0 * r.seconds / (static_cast<double> (r.left.size()) / kRate));
    return true;
}

int renderAll (const std::string& outDir, const std::string& root)
{
    int failures = 0;
    auto loadBank = [&] (const char* name) -> std::shared_ptr<const SampleBank>
    {
        LoadReport report;
        const auto start = std::chrono::steady_clock::now();
        auto bank = SampleBank::load (instrumentDirectory (root, name), report);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
        if (! bank)
            std::printf ("%s: not available (%s)\n", name, report.error.c_str());
        else
            std::printf ("%s: %d zones, %d samples, %.1f MB RAM, loaded in %.0f ms, %zu warnings\n", name, report.zones,
                         report.samples, static_cast<double> (report.memoryBytes) / 1e6, ms, report.warnings.size());
        return bank;
    };
    auto run = [&] (const std::shared_ptr<const SampleBank>& bank, std::vector<Event> events, double seconds,
                    const Sampler::Parameters& params, const std::string& file)
    {
        Sampler sampler;
        sampler.prepare ({ kRate, kBlock });
        sampler.setBank (bank);
        sampler.setParameters (params);
        sortEvents (events);
        const Result r = play (sampler, events, static_cast<int> (seconds * kRate));
        if (! r.finite || ! writeRender (outDir + "/" + file, r))
            ++failures;
    };
    const int beat = static_cast<int> (kRate * 0.5);

    if (const auto grand = loadBank ("grand"))
    {
        // Velocity sweep on one key: every layer and crossfade in turn.
        std::vector<Event> sweep;
        for (int i = 0; i < 16; ++i)
        {
            sweep.push_back ({ Event::Type::noteOn, i * beat, 60, static_cast<float> (i + 1) / 16.0f });
            sweep.push_back ({ Event::Type::noteOff, i * beat + beat - 2000, 60, 0.0f });
        }
        run (grand, sweep, 9.5, {}, "grand_velocity_sweep_C4.wav");
        // Chromatic run across the keyboard at mezzo-forte: zone seams and transposition.
        run (grand, scale (21, 108, 1, 9000, 600, 0.6f), 88 * 0.2 + 3.0, {}, "grand_chromatic_A0_C8.wav");
        // Pedalled chords with sympathetic resonance, then the same dry.
        const std::vector<std::vector<int>> progression { { 36, 48, 55, 64, 67 }, { 41, 53, 60, 65, 69 },
                                                          { 43, 55, 59, 62, 67 }, { 36, 48, 52, 60, 64, 72 } };
        run (grand, chords (progression, static_cast<int> (kRate * 2.2), 0.7f, true), 12.0, {}, "grand_pedal_chords.wav");
        Sampler::Parameters dry;
        dry.pedalResonance = 0.0f;
        run (grand, chords (progression, static_cast<int> (kRate * 2.2), 0.7f, true), 12.0, dry, "grand_pedal_chords_no_resonance.wav");
        // Staccato repeated notes: release noises and retrigger behaviour.
        std::vector<Event> stacc;
        for (int i = 0; i < 24; ++i)
        {
            const int note = 48 + (i % 8) * 3;
            stacc.push_back ({ Event::Type::noteOn, i * 7200, note, 0.45f + 0.02f * static_cast<float> (i) });
            stacc.push_back ({ Event::Type::noteOff, i * 7200 + 2400, note, 0.0f });
        }
        run (grand, stacc, 5.0, {}, "grand_staccato_release_noise.wav");
        // Dense pedalled passage (the CPU test's load) on the real recordings.
        run (grand, densePassage (12, 30, 100), 12.0, {}, "grand_dense_passage.wav");
    }
    if (const auto strings = loadBank ("strings"))
    {
        const std::vector<std::vector<int>> progression { { 43, 55, 62, 71 }, { 40, 52, 59, 67 },
                                                          { 45, 57, 64, 72 }, { 38, 50, 57, 66, 74 } };
        run (strings, chords (progression, static_cast<int> (kRate * 3.5), 0.6f, false), 15.5, {}, "strings_chords.wav");
        run (strings, scale (36, 96, 2, 14000, 2000, 0.8f), 31 * (16000.0 / kRate) + 2.0, {}, "strings_scale_C2_C7.wav");
        std::vector<Event> held { { Event::Type::noteOn, 0, 62, 0.5f }, { Event::Type::noteOn, 0, 69, 0.5f },
                                  { Event::Type::noteOff, static_cast<int> (kRate * 12), 62, 0.0f },
                                  { Event::Type::noteOff, static_cast<int> (kRate * 12), 69, 0.0f } };
        run (strings, held, 14.0, {}, "strings_long_hold_loop_check.wav");
    }
    if (const auto brass = loadBank ("brass"))
    {
        const std::vector<std::vector<int>> progression { { 48, 55, 64, 72 }, { 53, 60, 65, 69 },
                                                          { 43, 55, 62, 71 }, { 48, 55, 64, 67, 72 } };
        run (brass, chords (progression, static_cast<int> (kRate * 2.5), 0.85f, false), 11.5, {}, "brass_chords_forte.wav");
        run (brass, chords (progression, static_cast<int> (kRate * 2.5), 0.35f, false), 11.5, {}, "brass_chords_piano.wav");
        run (brass, scale (34, 84, 2, 9000, 1500, 0.7f), 26 * (10500.0 / kRate) + 2.0, {}, "brass_scale.wav");
        std::vector<Event> held { { Event::Type::noteOn, 0, 60, 0.7f }, { Event::Type::noteOn, 0, 67, 0.7f },
                                  { Event::Type::noteOff, static_cast<int> (kRate * 10), 60, 0.0f },
                                  { Event::Type::noteOff, static_cast<int> (kRate * 10), 67, 0.0f } };
        run (brass, held, 12.0, {}, "brass_long_hold_loop_check.wav");
    }
    return failures == 0 ? 0 : 1;
}

int bench (const std::string& root)
{
    std::size_t total = 0;
    for (const char* name : { "grand", "strings", "brass" })
    {
        LoadReport report;
        const auto bank = SampleBank::load (instrumentDirectory (root, name), report);
        if (! bank)
        {
            std::printf ("%s: not available (%s)\n", name, report.error.c_str());
            continue;
        }
        total += bank->memoryBytes();
        Sampler sampler;
        sampler.prepare ({ kRate, kBlock });
        sampler.setBank (bank);
        sampler.setParameters ({});
        const auto events = densePassage (20, 36, 96);
        allocations = 0;
        const Result r = play (sampler, events, static_cast<int> (kRate * 20));
        std::printf ("%s: %.1f MB RAM; dense 20 s passage: peak %d voices, %.1f%% of one core (%.1fx real time), "
                     "allocations=%zu, peak %.1f dBFS\n",
                     name, static_cast<double> (bank->memoryBytes()) / 1e6, r.peakVoices, 100.0 * r.seconds / 20.0,
                     20.0 / r.seconds, allocations, static_cast<double> (peakDb (r)));
    }
    std::printf ("total RAM for the set: %.1f MB\n", static_cast<double> (total) / 1e6);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc == 4 && std::string (argv[1]) == "--render")
        return renderAll (argv[2], argv[3]);
    if (argc == 3 && std::string (argv[1]) == "--bench")
        return bench (argv[2]);
    return test();
}
