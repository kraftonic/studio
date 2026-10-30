// SPDX-License-Identifier: MIT
//
// EventHelpers tests. Pins the MIDI/note translation the
// VST3 adapter runs on the audio thread: pitch clamping, offset clamping,
// velocity-0-means-noteOff, sustain and momentary-button semantics.

#include "kraftonic/formats/EventHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace kraftonic;
using namespace kraftonic::formats;

TEST_CASE ("clampNote keeps the MIDI range", "[plugin-events]")
{
    CHECK (clampNote (60) == 60);
    CHECK (clampNote (-1) == 0);
    CHECK (clampNote (-1000) == 0);
    CHECK (clampNote (128) == 127);
    CHECK (clampNote (1000) == 127);
}

TEST_CASE ("clampOffset stays inside the block", "[plugin-events]")
{
    CHECK (clampOffset (0, 64) == 0);
    CHECK (clampOffset (63, 64) == 63);
    CHECK (clampOffset (-5, 64) == 0);
    CHECK (clampOffset (64, 64) == 63);
    CHECK (clampOffset (100000, 64) == 63);
    CHECK (clampOffset (3, 0) == 0);
}

TEST_CASE ("velocity zero becomes noteOff", "[plugin-events]")
{
    const core::Event on = makeNoteEvent (60, 0.8, 10);
    CHECK (on.type == core::Event::Type::noteOn);
    CHECK (on.note == 60);
    CHECK (on.sampleOffset == 10);
    CHECK (on.value == Catch::Approx (0.8F));

    const core::Event off = makeNoteEvent (60, 0.0, 11);
    CHECK (off.type == core::Event::Type::noteOff);
    CHECK (off.value == 0.0F);

    const core::Event clamped = makeNoteEvent (200, 2.0, 0);
    CHECK (clamped.type == core::Event::Type::noteOn);
    CHECK (clamped.note == 127);
    CHECK (clamped.value == 1.0F);
}

TEST_CASE ("noteOff and sustain carry clamped values", "[plugin-events]")
{
    const core::Event off = makeNoteOffEvent (40, 0.5, 4);
    CHECK (off.type == core::Event::Type::noteOff);
    CHECK (off.note == 40);
    CHECK (off.sampleOffset == 4);

    const core::Event down = makeSustainEvent (1.0, 7);
    CHECK (down.type == core::Event::Type::sustainPedal);
    CHECK (down.value == 1.0F);

    const core::Event up = makeSustainEvent (0.0, 7);
    CHECK (up.type == core::Event::Type::sustainPedal);
    CHECK (up.value == 0.0F);
}

TEST_CASE ("momentary buttons fire on the pressed half", "[plugin-events]")
{
    CHECK (!isMomentaryTrigger (0.0));
    CHECK (!isMomentaryTrigger (0.499));
    CHECK (isMomentaryTrigger (0.5));
    CHECK (isMomentaryTrigger (1.0));
}

TEST_CASE ("mod wheel carries a clamped 0..1 value", "[plugin-events]")
{
    const core::Event full = makeModWheelEvent (1.0, 3);
    CHECK (full.type == core::Event::Type::modWheel);
    CHECK (full.sampleOffset == 3);
    CHECK (full.value == Catch::Approx (1.0F));

    const core::Event half = makeModWheelEvent (0.5, 0);
    CHECK (half.type == core::Event::Type::modWheel);
    CHECK (half.value == Catch::Approx (0.5F));

    CHECK (makeModWheelEvent (-2.0, 0).value == 0.0F);
    CHECK (makeModWheelEvent (2.0, 0).value == 1.0F);
}

TEST_CASE ("pressure event makers carry note and channel values safely", "[plugin-events]")
{
    const auto poly = makePolyPressureEvent (200, 0.75, 6);
    CHECK (poly.type == core::Event::Type::polyPressure);
    CHECK (poly.note == 127);
    CHECK (poly.value == Catch::Approx (0.75F));
    CHECK (poly.sampleOffset == 6);

    const auto channel = makeChannelPressureEvent (-2.0, 9);
    CHECK (channel.type == core::Event::Type::channelPressure);
    CHECK (channel.value == 0.0F);
    CHECK (channel.sampleOffset == 9);
    CHECK (makeChannelPressureEvent (2.0, 0).value == 1.0F);
}

TEST_CASE ("pitch bend maps host ranges onto -1..1", "[plugin-events]")
{
    // Normalized host parameter: 0..1 with centre 0.5.
    CHECK (pitchBendFromNormalized (0.5) == Catch::Approx (0.0F));
    CHECK (pitchBendFromNormalized (0.0) == Catch::Approx (-1.0F));
    CHECK (pitchBendFromNormalized (1.0) == Catch::Approx (1.0F));
    CHECK (pitchBendFromNormalized (0.75) == Catch::Approx (0.5F));
    CHECK (pitchBendFromNormalized (-1.0) == Catch::Approx (-1.0F));
    CHECK (pitchBendFromNormalized (2.0) == Catch::Approx (1.0F));

    // 14-bit MIDI: centre 8192 (MSB 64, LSB 0).
    CHECK (pitchBendFromMidi14 (0, 64) == Catch::Approx (0.0F));
    CHECK (pitchBendFromMidi14 (0, 0) == Catch::Approx (-1.0F));
    CHECK (pitchBendFromMidi14 (127, 127) == Catch::Approx (8191.0F / 8192.0F));
    CHECK (pitchBendFromMidi14 (0, 96) == Catch::Approx (0.5F));

    const core::Event bend = makePitchBendEvent (pitchBendFromNormalized (0.75), 9);
    CHECK (bend.type == core::Event::Type::pitchBend);
    CHECK (bend.sampleOffset == 9);
    CHECK (bend.value == Catch::Approx (0.5F));

    CHECK (makePitchBendEvent (-2.0F, 0).value == Catch::Approx (-1.0F));
    CHECK (makePitchBendEvent (2.0F, 0).value == Catch::Approx (1.0F));
}
