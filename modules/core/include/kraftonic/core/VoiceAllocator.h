// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>

namespace kraftonic::core
{
/** Decides which voice slot plays each note. Knows nothing about sound.

    Policy for a new note:
      1. A voice already playing the same note (retrigger, no doubled voice).
      2. A free voice.
      3. The oldest released voice (quietest to steal).
      4. The oldest held voice.
*/
template <int NumVoices>
class VoiceAllocator
{
    static_assert (NumVoices > 0);

public:
    static constexpr int kNoVoice = -1;

    struct Slot
    {
        int note = -1;
        bool sounding = false; // producing audio (includes release tail)
        bool held = false;     // key still down (or sustained by pedal)
        std::uint64_t startedAt = 0;
    };

    /** Allocates a voice for `note` and returns its index. */
    int noteOn (int note) noexcept
    {
        int chosen = findSoundingVoice (note);
        if (chosen == kNoVoice)
            chosen = findFreeVoice();
        if (chosen == kNoVoice)
            chosen = findOldest (false);
        if (chosen == kNoVoice)
            chosen = findOldest (true);

        auto& s = slots[static_cast<std::size_t> (chosen)];
        s.note = note;
        s.sounding = true;
        s.held = true;
        s.startedAt = ++clock;
        return chosen;
    }

    /** Marks the held voice playing `note` as released and returns it, or kNoVoice. */
    int noteOff (int note) noexcept
    {
        for (int i = 0; i < NumVoices; ++i)
        {
            auto& s = slots[static_cast<std::size_t> (i)];
            if (s.held && s.note == note)
            {
                s.held = false;
                return i;
            }
        }
        return kNoVoice;
    }

    /** Call when a voice's release tail has finished. */
    void voiceFinished (int index) noexcept
    {
        auto& s = slots[static_cast<std::size_t> (index)];
        s.sounding = false;
        s.held = false;
        s.note = -1;
    }

    void reset() noexcept
    {
        slots = {};
        clock = 0;
    }

    const Slot& slot (int index) const noexcept { return slots[static_cast<std::size_t> (index)]; }

    int numSounding() const noexcept
    {
        int n = 0;
        for (const auto& s : slots)
            n += s.sounding ? 1 : 0;
        return n;
    }

private:
    int findSoundingVoice (int note) const noexcept
    {
        for (int i = 0; i < NumVoices; ++i)
            if (slots[static_cast<std::size_t> (i)].sounding && slots[static_cast<std::size_t> (i)].note == note)
                return i;
        return kNoVoice;
    }

    int findFreeVoice() const noexcept
    {
        for (int i = 0; i < NumVoices; ++i)
            if (! slots[static_cast<std::size_t> (i)].sounding)
                return i;
        return kNoVoice;
    }

    int findOldest (bool held) const noexcept
    {
        int best = kNoVoice;
        for (int i = 0; i < NumVoices; ++i)
        {
            const auto& s = slots[static_cast<std::size_t> (i)];
            if (s.held != held)
                continue;
            if (best == kNoVoice || s.startedAt < slots[static_cast<std::size_t> (best)].startedAt)
                best = i;
        }
        return best;
    }

    std::array<Slot, NumVoices> slots {};
    std::uint64_t clock = 0;
};
} // namespace kraftonic::core
