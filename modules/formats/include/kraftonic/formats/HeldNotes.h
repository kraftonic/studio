// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — "notes currently held" feedback.
//
//  SDK-free helpers for lighting on-screen keys from MIDI/host input. The
//  processor folds every merged core::Event into a 128-bit HeldNotes set and
//  a HeldNotesFeed releases it at ~30 Hz (same cadence as the output meter)
//  as kHeldNoteWords 16-bit words. Each word travels as one hidden read-only
//  output parameter (normalized = word / 65535), which survives any host
//  that stores values as float. The controller decodes the words back.
//
//  Real-time safe: integers only, no allocation, locks or I/O.
// ============================================================================

#include "kraftonic/core/Instrument.h"

#include <array>
#include <cstdint>

namespace kraftonic::formats
{

inline constexpr int kHeldNoteWords = 8;   // 8 x 16 bits = 128 notes
inline constexpr int kHeldNoteWordBits = 16;

class HeldNotes
{
public:
    void set (int note, bool down) noexcept
    {
        if (note < 0 || note > 127)
            return;
        const auto bit = static_cast<std::uint16_t> (1u << (note % kHeldNoteWordBits));
        auto& word = words_[static_cast<std::size_t> (note / kHeldNoteWordBits)];
        word = static_cast<std::uint16_t> (down ? (word | bit) : (word & ~bit));
    }

    bool isHeld (int note) const noexcept
    {
        if (note < 0 || note > 127)
            return false;
        const auto word = words_[static_cast<std::size_t> (note / kHeldNoteWordBits)];
        return ((word >> (note % kHeldNoteWordBits)) & 1u) != 0;
    }

    void clear() noexcept { words_.fill (0); }

    bool any() const noexcept
    {
        for (auto word : words_)
        {
            if (word != 0)
                return true;
        }
        return false;
    }

    /** Applies one engine event (noteOn/noteOff/allNotesOff/allSoundOff). */
    void apply (const core::Event& e) noexcept
    {
        using Type = core::Event::Type;
        if (e.type == Type::noteOn)
            set (e.note, e.value > 0.0F);
        else if (e.type == Type::noteOff)
            set (e.note, false);
        else if (e.type == Type::allNotesOff || e.type == Type::allSoundOff)
            clear();
    }

    std::uint16_t word (int index) const noexcept
    {
        if (index < 0 || index >= kHeldNoteWords)
            return 0;
        return words_[static_cast<std::size_t> (index)];
    }

    void setWord (int index, std::uint16_t value) noexcept
    {
        if (index < 0 || index >= kHeldNoteWords)
            return;
        words_[static_cast<std::size_t> (index)] = value;
    }

    bool operator== (const HeldNotes& other) const noexcept = default;

    /** Word -> normalized parameter value (exact in float and double). */
    static double wordToNormalized (std::uint16_t word) noexcept
    {
        return static_cast<double> (word) / 65535.0;
    }

    /** Normalized parameter value -> word (clamped, NaN = 0). */
    static std::uint16_t wordFromNormalized (double normalized) noexcept
    {
        if (!(normalized >= 0.0))
            return 0;
        if (normalized >= 1.0)
            return 0xFFFF;
        return static_cast<std::uint16_t> (normalized * 65535.0 + 0.5);
    }

private:
    std::array<std::uint16_t, kHeldNoteWords> words_ {};
};

/**
 * ~30 Hz tick for the held-note feedback. The processor calls advance()
 * once per block; when it returns true, it sends the words that changed
 * since the last send (send() records them). A periodic full refresh
 * (every kRefreshTicks) keeps a controller that missed a value in sync.
 */
class HeldNotesFeed
{
public:
    static constexpr double kTickRate = 30.0;
    static constexpr int kRefreshTicks = 30; // full resend about once a second

    void setSampleRate (double sampleRate) noexcept
    {
        if (sampleRate < 1.0)
            sampleRate = 1.0;
        interval_ = static_cast<int> (sampleRate / kTickRate + 0.5);
        if (interval_ < 1)
            interval_ = 1;
        sinceTick_ = 0;
        ticks_ = 0;
        forceAll_ = true;
    }

    /** Counts samples; true when a tick is due (and restarts the tick). */
    bool advance (int numSamples) noexcept
    {
        sinceTick_ += numSamples < 0 ? 0 : numSamples;
        if (sinceTick_ < interval_)
            return false;
        sinceTick_ = 0;
        if (++ticks_ >= kRefreshTicks)
        {
            ticks_ = 0;
            forceAll_ = true;
        }
        return true;
    }

    /** True when word `index` must be sent this tick. */
    bool needsSend (const HeldNotes& current, int index) const noexcept
    {
        return forceAll_ || current.word (index) != sent_.word (index);
    }

    /** Records the state that was just sent. */
    void markSent (const HeldNotes& current) noexcept
    {
        sent_ = current;
        forceAll_ = false;
    }

    int intervalSamples() const noexcept { return interval_; }

private:
    HeldNotes sent_ {};
    int interval_ = 1600;
    int sinceTick_ = 0;
    int ticks_ = 0;
    bool forceAll_ = true;
};

} // namespace kraftonic::formats
