// SPDX-License-Identifier: MIT
//
// Listening-WAV renderer for the self-test sine instrument.
//
// Drives SelfTestEngine directly (no host, no SDK) and writes a 48 kHz
// 32-bit float stereo WAV. Usage:
//      kraftonic_selftest_render <output.wav>
// The phrase exercises all three parameters: wave switching (sine/square/
// triangle by name), attack differences, sustain-pedal hold, and release.

#include "SelfTestEngine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

using namespace kraftonic::formats::selftest;

void writeU32LE (std::FILE* f, std::uint32_t v)
{
    std::uint8_t b[4] {
        static_cast<std::uint8_t> (v & 0xFFu),
        static_cast<std::uint8_t> ((v >> 8) & 0xFFu),
        static_cast<std::uint8_t> ((v >> 16) & 0xFFu),
        static_cast<std::uint8_t> ((v >> 24) & 0xFFu),
    };
    std::fwrite (b, 1, 4, f);
}

void writeU16LE (std::FILE* f, std::uint16_t v)
{
    std::uint8_t b[2] {
        static_cast<std::uint8_t> (v & 0xFFu),
        static_cast<std::uint8_t> ((v >> 8) & 0xFFu),
    };
    std::fwrite (b, 1, 2, f);
}

struct Note
{
    double start;
    double length;
    int midi;
    float velocity;
};

} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: kraftonic_selftest_render <output.wav>\n");
        return 1;
    }

    constexpr double kRate = 48000.0;
    SelfTestEngine engine {};
    ProcessSetup setup {};
    setup.sampleRate = kRate;
    setup.maxBlockSize = 512;
    engine.prepare (setup);

    // A short phrase in A minor; the pedal holds the first chord while the
    // melody plays over it, then everything releases.
    const std::vector<Note> notes = {
        {0.0, 2.4, 45, 0.8F}, // A2 (pedalled)
        {0.0, 2.4, 52, 0.7F}, // E3 (pedalled)
        {0.0, 2.4, 57, 0.7F}, // A3 (pedalled)
        {0.5, 0.4, 69, 0.85F}, // A4
        {1.0, 0.4, 72, 0.85F}, // C5
        {1.5, 0.4, 76, 0.9F}, // E5
        {2.0, 0.9, 79, 0.9F}, // G5
        {3.0, 0.5, 76, 0.8F}, // E5 (triangle)
        {3.5, 1.2, 74, 0.8F}, // D5
    };
    constexpr double kLength = 6.0;
    const int total = static_cast<int> (kLength * kRate);

    std::vector<float> left (static_cast<std::size_t> (total), 0.0F);
    std::vector<float> right (static_cast<std::size_t> (total), 0.0F);

    SelfTestParameters params = SelfTestParameters::defaults();
    params.set (ParamId::outputGain, -6.0F);
    params.set (ParamId::wave, 0.0F); // sine for the chord + first melody
    params.set (ParamId::attack, 0.01F);

    std::vector<char> noteDone (notes.size(), 0);
    std::vector<char> noteOffDone (notes.size(), 0);
    bool pedalDown = false;
    bool pedalUp = false;
    bool switched = false;

    constexpr int kBlock = 512;
    std::vector<Event> events {};
    events.reserve (32);

    for (int at = 0; at < total; at += kBlock)
    {
        const int n = at + kBlock <= total ? kBlock : total - at;
        const double t = static_cast<double> (at) / kRate;
        events.clear();

        if (!pedalDown && t >= 0.0)
        {
            pedalDown = true;
            events.push_back (Event {Event::Type::sustainPedal, 0, 0, 1.0F});
        }
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            if (!noteDone[i] && t >= notes[i].start)
            {
                noteDone[i] = 1;
                const int offset = static_cast<int> (
                    (notes[i].start - t) * kRate + 0.5);
                events.push_back (
                    Event {Event::Type::noteOn, offset < 0 ? 0 : offset,
                           notes[i].midi, notes[i].velocity});
            }
            if (noteDone[i] && !noteOffDone[i]
                && t >= notes[i].start + notes[i].length)
            {
                noteOffDone[i] = 1;
                events.push_back (Event {Event::Type::noteOff, 0, notes[i].midi, 0.0F});
            }
        }
        if (!switched && t >= 2.9)
        {
            // Second half: square wave with a slower attack.
            switched = true;
            params.set (ParamId::wave, 1.0F);
            params.set (ParamId::attack, 0.06F);
        }
        if (!pedalUp && t >= 2.6)
        {
            pedalUp = true;
            events.push_back (Event {Event::Type::sustainPedal, 0, 0, 0.0F});
        }
        // Keep offsets inside the block and sorted (the adapter guarantees).
        for (auto& e : events)
        {
            if (e.sampleOffset < 0)
                e.sampleOffset = 0;
            if (e.sampleOffset >= n)
                e.sampleOffset = n - 1;
        }

        engine.setParameters (params);
        const AudioBlock block {
            left.data() + at, right.data() + at, n};
        engine.process (block, events.empty() ? nullptr : events.data(),
                        static_cast<int> (events.size()));
    }

    std::FILE* f = std::fopen (argv[1], "wb");
    if (f == nullptr)
    {
        std::fprintf (stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t> (total) * 2u * 4u;
    std::fwrite ("RIFF", 1, 4, f);
    writeU32LE (f, 36 + dataBytes);
    std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f);
    writeU32LE (f, 16);
    writeU16LE (f, 3); // IEEE float
    writeU16LE (f, 2);
    writeU32LE (f, 48000);
    writeU32LE (f, 48000 * 2 * 4);
    writeU16LE (f, 8);
    writeU16LE (f, 32);
    std::fwrite ("data", 1, 4, f);
    writeU32LE (f, dataBytes);
    for (int s = 0; s < total; ++s)
    {
        std::fwrite (&left[static_cast<std::size_t> (s)], 4, 1, f);
        std::fwrite (&right[static_cast<std::size_t> (s)], 4, 1, f);
    }
    std::fclose (f);
    std::printf ("wrote %s (%d samples)\n", argv[1], total);
    return 0;
}
