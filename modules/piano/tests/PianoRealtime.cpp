// SPDX-License-Identifier: MIT
//
// "piano: realtime and performance": no allocation or deallocation in any
// real-time call of PianoLayer (process, renderVoices/processBus, note and
// pedal events, setParameters, reset), finite output at the sample-rate
// extremes, and a CPU gate: 16 modelled voices plus a pedalled sampled grand
// with every bus feature on must render at least 3x faster than real time.

#include "PianoTestHelpers.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

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

using namespace kraftonic::piano;
using namespace kraftonic::piano::test;

namespace
{
    Parameters everything (int index)
    {
        Parameters p = sound (index);
        p.dynamicCompression = true;
        p.doubling = true;
        p.stringResonance = 0.7f;
        p.pedalNoise = 0.6f;
        p.level = 0.9f;
        return p;
    }

    /** Plays a dense passage through every real-time entry point, counting
        allocations. Returns the peak, or a negative value if the output was
        not finite. */
    float exercise (PianoLayer& layer, double rate, int seconds)
    {
        const int block = kBlock;
        std::vector<float> left (static_cast<std::size_t> (block)), right (static_cast<std::size_t> (block));
        allocations = 0;
        tracking = true;
        const int blocks = static_cast<int> (rate * seconds) / block;
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 40 == 0)
                layer.sustain (b % 80 == 0 ? 1.0f : 0.0f);
            if (b % 8 == 0)
                for (int i = 0; i < 6; ++i)
                    layer.noteOn (36 + (b * 7 + i * 5) % 60, 0.4f + 0.1f * static_cast<float> (i));
            if (b % 8 == 4)
                for (int i = 0; i < 6; ++i)
                    layer.noteOff (36 + ((b - 4) * 7 + i * 5) % 60);
            if (b % 50 == 25)
                layer.setParameters (everything ((b / 50) % kNumModels));
            if (b % 300 == 299)
                layer.allNotesOff();
            if (b % 2 == 0)
                layer.process (left.data(), right.data(), block);
            else
            {
                layer.renderVoices (left.data(), right.data(), block);
                layer.processBus (left.data(), right.data(), block);
            }
            for (int i = 0; i < block; ++i)
            {
                if (! std::isfinite (left[static_cast<std::size_t> (i)]) || ! std::isfinite (right[static_cast<std::size_t> (i)]))
                {
                    tracking = false;
                    return -1.0f;
                }
                peak = std::max ({ peak, std::abs (left[static_cast<std::size_t> (i)]), std::abs (right[static_cast<std::size_t> (i)]) });
            }
        }
        layer.reset();
        tracking = false;
        return peak;
    }
} // namespace

int main()
{
    GrandFolder folder;
    const auto bank = PianoLayer::loadGrandBank (folder.str());
    if (bank == nullptr)
    {
        std::fprintf (stderr, "Test sample set did not load\n");
        return 1;
    }

    // No allocation in any real-time call, at every sample-rate extreme.
    for (double rate : { 22050.0, 48000.0, 96000.0, 384000.0 })
    {
        PianoLayer layer;
        layer.setGrandBank (bank);
        layer.setParameters (everything (4));
        layer.prepare (rate, kBlock);
        const float peak = exercise (layer, rate, rate > 100000.0 ? 2 : 6);
        if (allocations != 0)
        {
            std::fprintf (stderr, "%.0f Hz: real-time allocations=%zu\n", rate, allocations);
            return 1;
        }
        // Finite only: the synthetic grand has no gain_db trim, so its level
        // means nothing here (PianoLayerTests measure the levels).
        if (peak < 0.0f)
        {
            std::fprintf (stderr, "%.0f Hz: output not finite\n", rate);
            return 1;
        }
        std::printf ("%.0f Hz: real-time allocations=0, output finite\n", rate);
    }

    // CPU: 16 modelled voices (the struck-string models are the costliest)
    // plus a pedalled sampled grand, every bus feature on.
    PianoLayer models, grand;
    models.setParameters (everything (2));
    models.prepare (kRate, kBlock);
    grand.setGrandBank (bank);
    grand.setParameters (everything (5));
    grand.prepare (kRate, kBlock);
    grand.sustain (1.0f);
    for (int i = 0; i < kMaxVoices; ++i)
    {
        models.noteOn (36 + 3 * i, 0.8f);
        grand.noteOn (36 + 3 * i, 0.8f);
    }
    std::vector<float> left (kBlock), right (kBlock);
    constexpr int frames = kRate * 10;
    const auto start = std::chrono::steady_clock::now();
    for (int at = 0; at < frames; at += kBlock)
    {
        models.process (left.data(), right.data(), kBlock);
        grand.process (left.data(), right.data(), kBlock);
    }
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    const double factor = 10.0 / elapsed;
    std::printf ("16 model voices + pedalled grand, bus on: 10 s rendered in %.3f s, %.1fx real time\n", elapsed, factor);
    // Gate at 3x real time, like the products': shared CI runners are noisy, and a
    // real regression in optimised code still shows up as a large drop.
    return factor >= 3.0 ? 0 : 1;
}
