// SPDX-License-Identifier: MIT
//
// UI-to-audio note queue tests: FIFO order,
// wrap-around, overflow, no allocation, and a two-thread SPSC run.

#include "kraftonic/formats/UiNoteQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>

namespace
{
// Counts every global allocation in this test binary so the queue tests can
// prove push/pop never allocate.
std::atomic<long> gAllocations {0};
} // namespace

void* operator new (std::size_t size)
{
    gAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept
{
    std::free (p);
}

void operator delete (void* p, std::size_t /*size*/) noexcept
{
    std::free (p);
}

using kraftonic::formats::SpscNoteQueue;
using kraftonic::formats::UiNote;
using kraftonic::formats::UiNoteQueue;

TEST_CASE ("note queue keeps FIFO order across wrap-around", "[note-queue]")
{
    SpscNoteQueue<8> queue {};
    int expected = 0;
    int next = 0;
    for (int round = 0; round < 50; ++round)
    {
        for (int i = 0; i < 5; ++i)
        {
            REQUIRE (queue.push (UiNote {next % 128, 0.5F, (next % 2) == 0}));
            ++next;
        }
        UiNote out {};
        while (queue.pop (out))
        {
            CHECK (out.note == expected % 128);
            CHECK (out.on == ((expected % 2) == 0));
            ++expected;
        }
    }
    CHECK (expected == next);
}

TEST_CASE ("note queue drops the newest note when full", "[note-queue]")
{
    SpscNoteQueue<4> queue {};
    for (int i = 0; i < 4; ++i)
        REQUIRE (queue.push (UiNote {60 + i, 1.0F, true}));
    CHECK (queue.size() == 4);
    CHECK_FALSE (queue.push (UiNote {99, 1.0F, true}));
    UiNote out {};
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE (queue.pop (out));
        CHECK (out.note == 60 + i); // the dropped 99 never appears
    }
    CHECK_FALSE (queue.pop (out));
    // Space again after draining.
    CHECK (queue.push (UiNote {61, 0.0F, false}));
}

TEST_CASE ("note queue push and pop never allocate", "[note-queue]")
{
    auto* queue = new UiNoteQueue(); // allocation outside the measured region
    const long before = gAllocations.load();
    UiNote out {};
    for (int i = 0; i < 10000; ++i)
    {
        queue->push (UiNote {i % 128, 0.7F, true});
        if ((i % 3) == 0)
            queue->pop (out);
    }
    while (queue->pop (out))
    {
    }
    CHECK (gAllocations.load() == before);
    delete queue;
}

TEST_CASE ("note queue survives a producer and a consumer thread", "[note-queue]")
{
    SpscNoteQueue<64> queue {};
    constexpr int kCount = 200000;
    std::thread producer ([&queue] {
        for (int i = 0; i < kCount;)
        {
            if (queue.push (UiNote {i, 0.5F, true}))
                ++i;
        }
    });
    int expected = 0;
    bool ordered = true;
    UiNote out {};
    while (expected < kCount)
    {
        if (queue.pop (out))
        {
            ordered = ordered && out.note == expected;
            ++expected;
        }
    }
    producer.join();
    CHECK (ordered);
    CHECK_FALSE (queue.pop (out));
}
