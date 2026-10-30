// SPDX-License-Identifier: MIT
//
// PendingParamUpdates tests The controller enqueues parameter
// changes received off the UI thread here and drains them later on the UI
// thread, so the VSTGUI editor is never touched from a background thread.
// These tests pin the queue's ordering, coalescing and thread-safety contract
// without needing a host or the VST3 SDK.

#include "kraftonic/formats/PendingParamUpdates.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <thread>
#include <vector>

using namespace kraftonic::formats;

namespace
{

bool hasEntry (const std::vector<PendingParamUpdate>& updates, std::uint32_t id,
               double value)
{
    for (const auto& update : updates)
    {
        if (update.id == id && update.normalized == value)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE ("pending updates preserve insertion order", "[pending]")
{
    PendingParamUpdates pending;
    pending.add (3, 0.3);
    pending.add (1, 0.1);
    pending.add (2, 0.2);

    const auto drained = pending.take();
    REQUIRE (drained.size() == 3);
    CHECK (drained[0].id == 3u);
    CHECK (drained[1].id == 1u);
    CHECK (drained[2].id == 2u);
    CHECK (pending.empty());
}

TEST_CASE ("pending updates coalesce repeats keeping first position", "[pending]")
{
    PendingParamUpdates pending;
    pending.add (7, 0.1);
    pending.add (8, 0.8);
    pending.add (7, 0.5);
    pending.add (7, 0.9);

    const auto drained = pending.take();
    REQUIRE (drained.size() == 2);
    CHECK (drained[0].id == 7u);
    CHECK (drained[0].normalized == 0.9);
    CHECK (drained[1].id == 8u);
    CHECK (drained[1].normalized == 0.8);
    CHECK (pending.take().empty());
}

TEST_CASE ("pending updates drain concurrent producers exactly once", "[pending]")
{
    PendingParamUpdates pending;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 200;

    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t)
    {
        producers.emplace_back ([&pending, t] {
            for (int i = 0; i < kPerThread; ++i)
            {
                const auto id = static_cast<std::uint32_t> (t * kPerThread + i);
                pending.add (id, static_cast<double> (id) * 0.001);
            }
        });
    }
    for (auto& producer : producers)
        producer.join();

    const auto drained = pending.take();
    REQUIRE (drained.size() == static_cast<std::size_t> (kThreads * kPerThread));
    for (int t = 0; t < kThreads; ++t)
    {
        for (int i = 0; i < kPerThread; ++i)
        {
            const auto id = static_cast<std::uint32_t> (t * kPerThread + i);
            CHECK (hasEntry (drained, id, static_cast<double> (id) * 0.001));
        }
    }
    CHECK (pending.empty());
}

TEST_CASE ("pending updates survive a concurrent drain", "[pending]")
{
    PendingParamUpdates pending;
    constexpr int kCount = 2000;
    for (int i = 0; i < kCount; ++i)
        pending.add (static_cast<std::uint32_t> (i), 0.5);

    // Each queued entry must come out of exactly one take no matter how the
    // two racing drains interleave; merging both takes must recover all of them.
    std::vector<PendingParamUpdate> raced;
    auto racer = std::thread ([&pending, &raced] { raced = pending.take(); });
    auto firstTake = pending.take();
    racer.join();

    firstTake.insert (firstTake.end(), raced.begin(), raced.end());
    CHECK (pending.empty());
    std::sort (firstTake.begin(), firstTake.end(),
               [] (const PendingParamUpdate& a, const PendingParamUpdate& b) {
                   return a.id < b.id;
               });
    REQUIRE (firstTake.size() == static_cast<std::size_t> (kCount));
    for (int i = 0; i < kCount; ++i)
    {
        CHECK (firstTake[static_cast<std::size_t> (i)].id
               == static_cast<std::uint32_t> (i));
    }
}
