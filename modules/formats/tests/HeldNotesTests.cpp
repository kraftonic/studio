// SPDX-License-Identifier: MIT
//
// Held-note feedback tests: event folding,
// float-safe word packing and the ~30 Hz change-only feed.

#include "kraftonic/formats/HeldNotes.h"

#include <catch2/catch_test_macros.hpp>

using kraftonic::core::Event;
using kraftonic::formats::HeldNotes;
using kraftonic::formats::HeldNotesFeed;
using kraftonic::formats::kHeldNoteWords;

namespace
{
Event note (Event::Type type, int pitch, float value)
{
    Event e {};
    e.type = type;
    e.note = pitch;
    e.value = value;
    return e;
}
} // namespace

TEST_CASE ("held notes follow note on/off and all-notes-off", "[held-notes]")
{
    HeldNotes held {};
    held.apply (note (Event::Type::noteOn, 60, 0.8F));
    held.apply (note (Event::Type::noteOn, 127, 0.8F));
    held.apply (note (Event::Type::noteOn, 0, 0.8F));
    CHECK (held.isHeld (60));
    CHECK (held.isHeld (127));
    CHECK (held.isHeld (0));
    held.apply (note (Event::Type::noteOff, 60, 0.0F));
    CHECK_FALSE (held.isHeld (60));
    held.apply (note (Event::Type::noteOn, 61, 0.0F)); // velocity 0 = off
    CHECK_FALSE (held.isHeld (61));
    held.apply (note (Event::Type::allNotesOff, 0, 0.0F));
    CHECK_FALSE (held.any());
    held.set (200, true); // out of range: ignored
    CHECK_FALSE (held.any());
}

TEST_CASE ("held-note words survive a float round trip", "[held-notes]")
{
    for (unsigned value = 0; value <= 0xFFFFu; ++value)
    {
        const auto word = static_cast<std::uint16_t> (value);
        const auto asFloat = static_cast<float> (HeldNotes::wordToNormalized (word));
        REQUIRE (HeldNotes::wordFromNormalized (static_cast<double> (asFloat)) == word);
    }
    CHECK (HeldNotes::wordFromNormalized (-1.0) == 0);
    CHECK (HeldNotes::wordFromNormalized (2.0) == 0xFFFF);
}

TEST_CASE ("held-note feed ticks at ~30 Hz and sends only changes", "[held-notes]")
{
    HeldNotesFeed feed {};
    feed.setSampleRate (48000.0);
    CHECK (feed.intervalSamples() == 1600);
    HeldNotes held {};
    CHECK_FALSE (feed.advance (1000));
    CHECK (feed.advance (600));
    // First tick after prepare: everything goes out once.
    for (int w = 0; w < kHeldNoteWords; ++w)
        CHECK (feed.needsSend (held, w));
    feed.markSent (held);
    held.set (64, true); // word 4
    CHECK (feed.advance (1600));
    for (int w = 0; w < kHeldNoteWords; ++w)
        CHECK (feed.needsSend (held, w) == (w == 4));
    feed.markSent (held);
    // Periodic full refresh.
    int ticks = 0;
    bool refreshed = false;
    while (ticks < HeldNotesFeed::kRefreshTicks + 1 && !refreshed)
    {
        if (feed.advance (1600))
        {
            ++ticks;
            refreshed = feed.needsSend (held, 0);
            if (!refreshed)
                feed.markSent (held);
        }
    }
    CHECK (refreshed);
}
