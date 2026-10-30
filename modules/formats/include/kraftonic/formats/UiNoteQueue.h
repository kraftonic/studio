// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — UI-to-audio note queue.
//
//  Fixed-capacity, lock-free single-producer/single-consumer ring for notes
//  played on the on-screen keyboard. The producer is the processor's
//  IMessage handler (host message thread, normally the UI thread); the
//  consumer is process() on the audio thread, which drains it at the start
//  of each block and injects the notes as core::Event noteOn/noteOff at
//  offset 0.
//
//  Real-time safe on both ends: a std::array plus two atomic indices, no
//  allocation, no locks, no exceptions. A full queue drops the newest note
//  (push returns false) rather than block or grow.
// ============================================================================

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kraftonic::formats
{

struct UiNote
{
    std::int32_t note = 0;  // MIDI note 0..127
    float velocity = 0.0F;  // 0..1 (noteOn); release velocity for noteOff
    bool on = false;        // true = noteOn, false = noteOff
};

/** The ring for any trivially copyable item (the CLAP adapter queues GUI
    parameter edits through it). */
template <typename Item, std::size_t Capacity>
class SpscQueue
{
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                   "capacity must be a power of two");

public:
    static constexpr std::size_t kCapacity = Capacity;

    /** Producer side. False when full (the item is dropped). */
    bool push (const Item& item) noexcept
    {
        const std::size_t head = head_.load (std::memory_order_relaxed);
        const std::size_t tail = tail_.load (std::memory_order_acquire);
        if (head - tail >= Capacity)
            return false;
        slots_[head & (Capacity - 1)] = item;
        head_.store (head + 1, std::memory_order_release);
        return true;
    }

    /** Consumer side. False when empty. */
    bool pop (Item& out) noexcept
    {
        const std::size_t tail = tail_.load (std::memory_order_relaxed);
        const std::size_t head = head_.load (std::memory_order_acquire);
        if (tail == head)
            return false;
        out = slots_[tail & (Capacity - 1)];
        tail_.store (tail + 1, std::memory_order_release);
        return true;
    }

    /** Approximate fill level (exact when called from either single end). */
    std::size_t size() const noexcept
    {
        return head_.load (std::memory_order_acquire) - tail_.load (std::memory_order_acquire);
    }

private:
    std::array<Item, Capacity> slots_ {};
    std::atomic<std::size_t> head_ {0};
    std::atomic<std::size_t> tail_ {0};
};

template <std::size_t Capacity>
using SpscNoteQueue = SpscQueue<UiNote, Capacity>;

/** The queue the shared VST3 processor uses (256 notes per block is plenty). */
using UiNoteQueue = SpscNoteQueue<256>;

} // namespace kraftonic::formats
