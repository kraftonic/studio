// SPDX-License-Identifier: MIT
// Renders a short test phrase through every factory preset to one 16-bit
// stereo WAV and prints its peak and RMS. Not a test: a listening aid.
//
// Usage: startereffect_render <out.wav>
#include "StarterEffectPresets.h"

#include <kraftonic/formats/Preset.h>
#include "StarterEffectEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <vector>

using namespace startereffect;

namespace
{
constexpr int kRate = 48000;
constexpr int kBlock = 256;

/** Applies a preset without the plug-in traits (the engine needs only the table). */
StarterEffectParameters presetParameters (const kraftonic::formats::FactoryPreset& preset)
{
    auto params = StarterEffectParameters::defaults();
    for (std::size_t i = 0; i < preset.count; ++i)
        for (const auto& info : kParamInfo)
            if (std::string_view (info.key) == preset.values[i].key)
                params.set (info.id, preset.values[i].plain);
    return params;
}

/** The dry test signal: four plucked notes (decaying saw-ish tones), then silence for the echoes. */
float source (int n)
{
    const double t = static_cast<double> (n) / kRate;
    const double notes[] {196.0, 247.0, 294.0, 392.0};
    const int index = static_cast<int> (t / 0.4);
    if (index >= 4)
        return 0.0F;
    const double local = t - index * 0.4;
    const double phase = std::fmod (local * notes[index], 1.0);
    return static_cast<float> (0.4 * (2.0 * phase - 1.0) * std::exp (-local * 8.0));
}

void writeWav (const char* path, const std::vector<float>& interleaved)
{
    std::ofstream file (path, std::ios::binary);
    auto u32 = [&] (std::uint32_t v) { file.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&] (std::uint16_t v) { file.write (reinterpret_cast<const char*> (&v), 2); };
    const auto bytes = static_cast<std::uint32_t> (interleaved.size() * 2);
    file.write ("RIFF", 4); u32 (36 + bytes); file.write ("WAVEfmt ", 8);
    u32 (16); u16 (1); u16 (2); u32 (kRate); u32 (kRate * 4); u16 (4); u16 (16);
    file.write ("data", 4); u32 (bytes);
    for (float s : interleaved)
    {
        const auto v = static_cast<std::int16_t> (std::lround (std::clamp (s, -1.0F, 1.0F) * 32767.0F));
        file.write (reinterpret_cast<const char*> (&v), 2);
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: %s <out.wav>\n", argv[0]);
        return 2;
    }
    constexpr int kSamplesPerPreset = kRate * 3;
    std::vector<float> out;
    for (const auto& preset : plugin::kPresets)
    {
        StarterEffectEngine engine;
        engine.prepare ({kRate, kBlock});
        engine.setParameters (presetParameters (preset));
        std::vector<float> left (kBlock), right (kBlock);
        for (int start = 0; start < kSamplesPerPreset; start += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                left[static_cast<std::size_t> (i)] = right[static_cast<std::size_t> (i)] = source (start + i);
            engine.process ({left.data(), right.data(), kBlock});
            for (int i = 0; i < kBlock; ++i)
                out.insert (out.end(), {left[static_cast<std::size_t> (i)], right[static_cast<std::size_t> (i)]});
        }
    }
    float peak = 0.0F;
    double sum = 0.0;
    for (float s : out)
    {
        peak = std::max (peak, std::abs (s));
        sum += static_cast<double> (s) * s;
    }
    writeWav (argv[1], out);
    std::printf ("%s: %zu presets, %.1f s, peak %.1f dBFS, RMS %.1f dBFS\n", argv[1], std::size (plugin::kPresets),
                 static_cast<double> (out.size()) / 2.0 / kRate, 20.0 * std::log10 (peak),
                 10.0 * std::log10 (sum / static_cast<double> (out.size())));
    return 0;
}
