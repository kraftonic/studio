// SPDX-License-Identifier: MIT
//
// Real-time checks for the wavetable engine, as a plain executable (ctest:
// "wavetable: realtime and performance"):
//   1. setParameters / process / reset never allocate.
//   2. 16 voices x unison 1 render at least 3x faster than real time
//      (target: well under 10% of one core). Unison 8 cost is reported.
#include <kraftonic/wavetable/WaveEngine.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
bool tracking = false;
std::size_t allocations = 0;
std::size_t deallocations = 0;
} // namespace

void* operator new (std::size_t size)
{
    if (tracking)
        ++allocations;
    if (void* ptr = std::malloc (size == 0 ? 1 : size))
        return ptr;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    if (tracking)
        ++allocations;
    if (void* ptr = std::malloc (size == 0 ? 1 : size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete (void* ptr) noexcept
{
    if (tracking)
        ++deallocations;
    std::free (ptr);
}

void operator delete[] (void* ptr) noexcept
{
    if (tracking)
        ++deallocations;
    std::free (ptr);
}

void operator delete (void* ptr, std::size_t) noexcept
{
    if (tracking)
        ++deallocations;
    std::free (ptr);
}

void operator delete[] (void* ptr, std::size_t) noexcept
{
    if (tracking)
        ++deallocations;
    std::free (ptr);
}

namespace
{
using namespace kraftonic::wavetable;
constexpr int kRate = 48000;
constexpr int kBlock = 256;

/** Seconds to render `seconds` of audio with 16 held voices. */
double timeVoices (WaveParameters params, double seconds)
{
    WaveEngine engine;
    engine.setParameters (params);
    engine.prepare ({ kRate, kBlock });
    engine.setParameters (params);
    std::array<float, kBlock> left {}, right {};
    std::array<Event, kMaxVoices> notes {};
    for (int i = 0; i < kMaxVoices; ++i)
        notes[static_cast<std::size_t> (i)] = { Event::Type::noteOn, 0, 40 + 2 * i, 0.8f };
    engine.process ({ left.data(), right.data(), kBlock }, notes.data(), kMaxVoices);
    const int frames = static_cast<int> (seconds * kRate);
    const auto start = std::chrono::steady_clock::now();
    for (int at = 0; at < frames; at += kBlock)
    {
        engine.setParameters (params);
        engine.process ({ left.data(), right.data(), std::min (kBlock, frames - at) }, nullptr, 0);
    }
    return std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
}

void report (const char* name, double seconds, double rendered)
{
    std::printf ("%-44s %.3f s for %.1f s audio: %5.2f%% of one core, %6.1fx real time\n", name, seconds, rendered,
                 100.0 * seconds / rendered, rendered / seconds);
}
} // namespace

int main()
{
    // ---- 1. no allocation on the real-time path ---------------------------
    {
        WaveEngine engine;
        engine.prepare ({ kRate, kBlock });
        auto params = WaveParameters::defaults();
        std::array<float, kBlock> left {}, right {};
        std::array<Event, 40> events {};
        for (int i = 0; i < 20; ++i)
            events[static_cast<std::size_t> (i)] = { Event::Type::noteOn, i * 10, 40 + i, 0.9f };
        events[20] = { Event::Type::modWheel, 210, 0, 0.6f };
        events[21] = { Event::Type::pitchBend, 211, 0, -0.4f };
        events[22] = { Event::Type::sustainPedal, 212, 0, 1.0f };
        for (int i = 0; i < 10; ++i)
            events[static_cast<std::size_t> (23 + i)] = { Event::Type::noteOff, 220 + i, 40 + i, 0.0f };
        events[33] = { Event::Type::sustainPedal, 240, 0, 0.0f };
        events[34] = { Event::Type::allNotesOff, 250, 0, 0.0f };

        tracking = true;
        engine.setParameters (params);
        engine.process ({ left.data(), right.data(), kBlock }, events.data(), 35);
        params.set (ParamId::oscATable, 4.0f);
        params.set (ParamId::oscAUnison, 8.0f);
        params.set (ParamId::filterMode, 2.0f);
        params.set (ParamId::filterDrive, 12.0f);
        params.set (ParamId::voiceMode, 2.0f);
        params.set (ParamId::lfo1Depth, 1.0f);
        engine.setParameters (params);
        engine.process ({ left.data(), right.data(), kBlock }, events.data(), 20);
        engine.process ({ left.data(), left.data(), kBlock }, nullptr, 0); // mono buffer
        engine.reset();
        (void) engine.activeVoiceCount();
        tracking = false;
        if (allocations != 0 || deallocations != 0)
        {
            std::fprintf (stderr, "FAIL: real-time allocations=%zu deallocations=%zu\n", allocations, deallocations);
            return 1;
        }
        std::printf ("Real-time allocations=0 deallocations=0\n");
    }

    // ---- 2. CPU --------------------------------------------------------------
    constexpr double seconds = 3.0;
    auto base = WaveParameters::defaults();
    base.set (ParamId::oscAPosition, 0.6f);
    base.set (ParamId::filterCutoff, 3000.0f);
    base.set (ParamId::ampRelease, 5.0f);

    const double unison1 = timeVoices (base, seconds);
    report ("16 voices, osc A, unison 1", unison1, seconds);

    auto full = base;
    full.set (ParamId::oscBLevel, 0.7f);
    full.set (ParamId::subLevel, 0.3f);
    full.set (ParamId::noiseLevel, 0.1f);
    full.set (ParamId::filterDrive, 6.0f);
    full.set (ParamId::lfo1Depth, 0.3f);
    full.set (ParamId::lfo2Depth, 0.3f);
    report ("16 voices, osc A+B, sub, noise, drive, LFOs", timeVoices (full, seconds), seconds);

    auto wide = base;
    wide.set (ParamId::oscAUnison, 8.0f);
    report ("16 voices, osc A unison 8", timeVoices (wide, seconds), seconds);
    wide.set (ParamId::oscBLevel, 0.7f);
    wide.set (ParamId::oscBUnison, 8.0f);
    report ("16 voices, osc A+B unison 8 (worst case)", timeVoices (wide, seconds), seconds);

    const double factor = seconds / unison1;
    if (factor < 3.0)
    {
        std::fprintf (stderr, "FAIL: 16 voices x unison 1 only %.2fx real time (need >= 3x)\n", factor);
        return 1;
    }
    std::printf ("PASS: 16 voices x unison 1 at %.1fx real time (gate >= 3x)\n", factor);
    return 0;
}
