// SPDX-License-Identifier: MIT
#include "kraftonic/fx/Chorus.h"
#include "kraftonic/fx/Delay.h"
#include "kraftonic/fx/Drive.h"
#include "kraftonic/fx/EQ.h"
#include "kraftonic/fx/Phaser.h"
#include "kraftonic/fx/Reverb.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <string>
#include <vector>

namespace
{
bool tracking = false;
std::size_t allocations = 0;
std::size_t deallocations = 0;
}

void* operator new (std::size_t size)
{
    if (tracking) ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size)
{
    if (tracking) ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept
{
    if (tracking) ++deallocations;
    std::free (p);
}
void operator delete[] (void* p) noexcept
{
    if (tracking) ++deallocations;
    std::free (p);
}
void operator delete (void* p, std::size_t) noexcept
{
    if (tracking) ++deallocations;
    std::free (p);
}
void operator delete[] (void* p, std::size_t) noexcept
{
    if (tracking) ++deallocations;
    std::free (p);
}

namespace
{
using namespace kraftonic::fx;

template <typename Effect>
bool run (const char* name)
{
    Effect effect;
    effect.prepare ({ 48000.0, 512 });
    typename Effect::Parameters params;
    std::array<float, 512> l {}, r {};
    tracking = true;
    effect.reset();
    effect.setParameters (params);
    for (int block = 0; block < 128; ++block)
    {
        effect.process (l.data(), r.data(), 512);
        params.enabled = (block & 1) == 0;
        effect.setParameters (params);
    }
    tracking = false;
    if (allocations != 0 || deallocations != 0)
    {
        std::fprintf (stderr, "%s allocations=%zu deallocations=%zu\n", name, allocations, deallocations);
        return false;
    }
    params.enabled = true;
    effect.setParameters (params);
    for (int i = 0; i < 512; ++i)
    {
        l[static_cast<std::size_t> (i)] = 0.15f * std::sin (0.071f * static_cast<float> (i));
        r[static_cast<std::size_t> (i)] = 0.15f * std::sin (0.093f * static_cast<float> (i));
    }
    const auto sourceL = l, sourceR = r;
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < 400; ++block)
    {
        l = sourceL;
        r = sourceR;
        effect.process (l.data(), r.data(), 512);
    }
    const auto elapsed = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count();
    std::printf ("%s: %.1f ns/sample (stereo), allocations=0 deallocations=0, final=%.4f\n",
                 name, elapsed / (400.0 * 512.0), l[0]);
    return true;
}

void put16 (std::ofstream& file, std::uint16_t v)
{
    const char b[2] { static_cast<char> (v & 255), static_cast<char> (v >> 8) };
    file.write (b, 2);
}
void put32 (std::ofstream& file, std::uint32_t v)
{
    put16 (file, static_cast<std::uint16_t> (v));
    put16 (file, static_cast<std::uint16_t> (v >> 16));
}
void wave (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    std::ofstream file (path, std::ios::binary);
    const auto bytes = static_cast<std::uint32_t> (l.size() * 8);
    file.write ("RIFF", 4); put32 (file, 36 + bytes);
    file.write ("WAVEfmt ", 8); put32 (file, 16); put16 (file, 3); put16 (file, 2);
    put32 (file, 48000); put32 (file, 48000 * 8); put16 (file, 8); put16 (file, 32);
    file.write ("data", 4); put32 (file, bytes);
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        file.write (reinterpret_cast<const char*> (&l[i]), 4);
        file.write (reinterpret_cast<const char*> (&r[i]), 4);
    }
}
std::vector<float> phrase()
{
    constexpr int count = 48000 * 8;
    std::vector<float> audio (count);
    constexpr float notes[8] { 261.626f, 329.628f, 392.0f, 523.251f,
                               440.0f, 392.0f, 329.628f, 261.626f };
    for (int i = 0; i < count; ++i)
    {
        if (i >= 48000 * 4)
            continue;
        const int note = (i / 24000) % 8;
        const float t = static_cast<float> (i % 24000) / 48000.0f;
        const float attack = 1.0f - std::exp (-t * 140.0f);
        const float envelope = attack * std::exp (-t * 4.0f);
        const float f = notes[note];
        const float phase = 2.0f * detail::pi * f * t;
        audio[static_cast<std::size_t> (i)] = 0.23f * envelope *
            (std::sin (phase) + 0.24f * std::sin (2.01f * phase) + 0.10f * std::sin (4.02f * phase));
    }
    return audio;
}
template <typename Effect>
void render (const std::string& directory, const char* name, const std::vector<float>& dry,
             typename Effect::Parameters a, typename Effect::Parameters b)
{
    for (int setting = 0; setting < 2; ++setting)
    {
        Effect effect;
        effect.prepare ({ 48000.0, 512 });
        effect.setParameters (setting == 0 ? a : b);
        auto l = dry, r = dry;
        for (std::size_t at = 0; at < l.size(); at += 512)
            effect.process (l.data() + at, r.data() + at,
                            static_cast<int> (std::min<std::size_t> (512, l.size() - at)));
        wave (directory + "/" + name + (setting == 0 ? "_gentle.wav" : "_strong.wav"), l, r);
    }
}
int renderAll (const std::string& directory)
{
    const auto dry = phrase();
    wave (directory + "/dry.wav", dry, dry);
    Drive::Parameters driveA, driveB;
    driveA.drive = 0.22f; driveB.drive = 0.75f;
    render<Drive> (directory, "drive", dry, driveA, driveB);
    Phaser::Parameters phaserA, phaserB;
    phaserA.depth = 0.35f; phaserB.depth = 1.0f; phaserB.feedback = 0.65f;
    render<Phaser> (directory, "phaser", dry, phaserA, phaserB);
    Chorus::Parameters chorusA, chorusB;
    chorusA.taps = 1; chorusB.taps = 3; chorusB.depth = 0.9f;
    render<Chorus> (directory, "chorus", dry, chorusA, chorusB);
    Delay::Parameters delayA, delayB;
    delayA.timeMs = 250.0f; delayB.timeMs = 400.0f; delayB.pingPong = true; delayB.feedback = 0.6f;
    render<Delay> (directory, "delay", dry, delayA, delayB);
    Reverb::Parameters reverbA, reverbB;
    reverbA.decaySeconds = 1.1f; reverbB.decaySeconds = 4.5f; reverbB.mix = 0.45f;
    render<Reverb> (directory, "reverb", dry, reverbA, reverbB);
    EQ::Parameters eqA, eqB;
    eqA.lowGainDb = 3.0f; eqA.highGainDb = 2.0f;
    eqB.lowGainDb = -4.0f; eqB.midGainDb = 5.0f; eqB.highGainDb = 4.0f;
    render<EQ> (directory, "eq", dry, eqA, eqB);
    std::printf ("Rendered dry and 12 effect WAVs in %s\n", directory.c_str());
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc == 3 && std::string (argv[1]) == "--render")
        return renderAll (argv[2]);
    const bool ok = run<Drive> ("Drive") && run<Phaser> ("Phaser")
        && run<Chorus> ("Chorus") && run<Delay> ("Delay")
        && run<Reverb> ("Reverb") && run<EQ> ("EQ");
    return ok ? 0 : 1;
}
