// SPDX-License-Identifier: MIT
// "startereffect: realtime and performance": the audio thread must never
// allocate, and processing must run well faster than real time.
// A plain executable (no Catch2): it counts every operator new/delete.
#include "StarterEffectEngine.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
bool tracking = false;
std::size_t allocations = 0;
} // namespace

// Replace the global allocator so any allocation while tracking is counted.
void* operator new (std::size_t size)
{
    if (tracking)
        ++allocations;
    if (void* ptr = std::malloc (size == 0 ? 1 : size))
        return ptr;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* ptr) noexcept
{
    if (tracking && ptr != nullptr)
        ++allocations;
    std::free (ptr);
}
void operator delete[] (void* ptr) noexcept { operator delete (ptr); }
void operator delete (void* ptr, std::size_t) noexcept { operator delete (ptr); }
void operator delete[] (void* ptr, std::size_t) noexcept { operator delete (ptr); }

int main()
{
    using namespace startereffect;
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256, kBlocks = 4000;

    StarterEffectEngine engine;
    engine.prepare ({kRate, kBlock}); // allowed to allocate: not the audio thread
    std::array<float, kBlock> left {}, right {};
    auto params = StarterEffectParameters::defaults();

    tracking = true;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < kBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            left[static_cast<std::size_t> (i)] = right[static_cast<std::size_t> (i)]
                = 0.5F * std::sin (0.05F * static_cast<float> (b * kBlock + i));
        // Worst case: every parameter jumps end to end on every block.
        for (const auto& info : kParamInfo)
            params.set (info.id, (b % 2) != 0 ? info.maxValue : info.minValue);
        engine.setParameters (params);
        engine.process ({left.data(), right.data(), kBlock});
    }
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    engine.reset();
    tracking = false;

    const double speed = (kBlock * kBlocks / kRate) / elapsed;
    std::printf ("allocations=%zu, %.1fx real time\n", allocations, speed);
    if (allocations != 0 || speed < 3.0)
        return 1;
    std::puts ("PASS: realtime and performance");
    return 0;
}
