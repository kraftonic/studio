// SPDX-License-Identifier: MIT
// Renders a short phrase per factory preset to one 16-bit stereo WAV and
// prints its peak and RMS. Not a test: a listening aid for pull requests.
//
// Usage: startersynth_render <out.wav>
#include "StarterSynthPresets.h"

#include <kraftonic/formats/Preset.h>
#include "StarterSynthEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

using namespace startersynth;

namespace
{
constexpr int kRate = 48000;
constexpr int kBlock = 256;

/** Applies a preset without the plug-in traits (the engine needs only the table). */
StarterSynthParameters presetParameters (const kraftonic::formats::FactoryPreset& preset)
{
    auto params = StarterSynthParameters::defaults();
    for (std::size_t i = 0; i < preset.count; ++i)
        for (const auto& info : kParamInfo)
            if (std::string_view (info.key) == preset.values[i].key)
                params.set (info.id, preset.values[i].plain);
    return params;
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
    // Per preset: a C minor chord, then a short melody, 2.5 s in all.
    struct Note { double start, length; int note; };
    const Note phrase[] {{0.0, 0.9, 48}, {0.0, 0.9, 55}, {0.0, 0.9, 63}, {1.0, 0.2, 60}, {1.25, 0.2, 63},
                         {1.5, 0.2, 67}, {1.75, 0.5, 72}};
    constexpr int kSamplesPerPreset = kRate * 5 / 2;

    std::vector<float> out;
    for (const auto& preset : plugin::kPresets)
    {
        StarterSynthEngine engine;
        engine.prepare ({kRate, kBlock});
        engine.setParameters (presetParameters (preset));
        std::vector<float> left (kBlock), right (kBlock);
        for (int start = 0; start < kSamplesPerPreset; start += kBlock)
        {
            std::vector<Event> events;
            for (const auto& n : phrase)
                for (const auto& [time, type] : {std::pair {n.start, Event::Type::noteOn}, std::pair {n.start + n.length, Event::Type::noteOff}})
                {
                    const int at = static_cast<int> (time * kRate);
                    if (at >= start && at < start + kBlock)
                        events.push_back ({type, at - start, n.note, type == Event::Type::noteOn ? 0.8F : 0.0F});
                }
            std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sampleOffset < b.sampleOffset; });
            engine.process ({left.data(), right.data(), kBlock}, events.data(), static_cast<int> (events.size()));
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
