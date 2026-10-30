// SPDX-License-Identifier: MIT
#include <kraftonic/core/VoiceAllocator.h>

#include <catch2/catch_test_macros.hpp>

#include <set>

using Allocator = kraftonic::core::VoiceAllocator<4>;

TEST_CASE ("VoiceAllocator gives each new note a free voice", "[voices]")
{
    Allocator a;
    std::set<int> used;
    for (int note = 60; note < 64; ++note)
        used.insert (a.noteOn (note));
    REQUIRE (used.size() == 4);
    REQUIRE (a.numSounding() == 4);
}

TEST_CASE ("VoiceAllocator reuses the voice already playing a repeated note", "[voices]")
{
    Allocator a;
    const int first = a.noteOn (60);
    a.noteOff (60);
    REQUIRE (a.noteOn (60) == first);
    REQUIRE (a.numSounding() == 1);
}

TEST_CASE ("VoiceAllocator steals released voices before held ones", "[voices]")
{
    Allocator a;
    const int v60 = a.noteOn (60);
    a.noteOn (61);
    const int v62 = a.noteOn (62);
    a.noteOn (63);

    a.noteOff (62); // still sounding (release tail) but no longer held
    REQUIRE (a.noteOn (70) == v62);

    // Everything held now: the oldest held voice (note 60) is stolen.
    REQUIRE (a.noteOn (71) == v60);
}

TEST_CASE ("VoiceAllocator frees voices when their tails finish", "[voices]")
{
    Allocator a;
    const int v = a.noteOn (60);
    REQUIRE (a.noteOff (60) == v);
    REQUIRE (a.noteOff (60) == Allocator::kNoVoice);
    a.voiceFinished (v);
    REQUIRE (a.numSounding() == 0);
}
