// SPDX-License-Identifier: MIT
#pragma once

#include "kraftonic/sampler/SampleBank.h"
#include "kraftonic/sampler/Sampler.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace kraftonic::sampler::test
{
using core::Event;

inline constexpr double kPi = 3.14159265358979323846;

inline std::vector<std::vector<float>> constant (float value, int frames, int channels = 1)
{
    return std::vector<std::vector<float>> (static_cast<std::size_t> (channels),
                                            std::vector<float> (static_cast<std::size_t> (frames), value));
}

inline std::vector<std::vector<float>> sine (double hz, double rate, int frames, float amplitude = 0.5f)
{
    std::vector<std::vector<float>> audio (1, std::vector<float> (static_cast<std::size_t> (frames)));
    for (int i = 0; i < frames; ++i)
        audio[0][static_cast<std::size_t> (i)] = amplitude * static_cast<float> (std::sin (2.0 * kPi * hz * i / rate));
    return audio;
}

/** A bank-building shortcut: zone fields default to "plays at root, any velocity". */
inline Zone zone (int sample, int keyLow, int keyHigh, int root, int velLow = 1, int velHigh = 127)
{
    Zone z;
    z.sample = sample;
    z.keyLow = keyLow;
    z.keyHigh = keyHigh;
    z.rootKey = root;
    z.velocityLow = velLow;
    z.velocityHigh = velHigh;
    return z;
}

/** Instrument settings that make levels easy to reason about. */
inline void plainSettings (SampleBank& bank)
{
    auto& s = bank.settings();
    s.velocityRangeDb = 0.0f;
    s.velocityCrossfade = 0.0f;
    s.attackMs = 0.0f;
    s.releaseMs = 50.0f;
    s.polyphony = 32;
}

inline std::shared_ptr<const SampleBank> finish (std::shared_ptr<SampleBank> bank)
{
    bank->finalize();
    return bank;
}

inline Event noteOn (int offset, int note, float velocity)
{
    return { Event::Type::noteOn, offset, note, velocity };
}
inline Event noteOff (int offset, int note)
{
    return { Event::Type::noteOff, offset, note, 0.0f };
}
inline Event pedal (int offset, bool down)
{
    return { Event::Type::sustainPedal, offset, 0, down ? 1.0f : 0.0f };
}

/** Renders `frames` samples in blocks of `block`, delivering `events` (offsets
    are absolute sample positions here) in the right block. */
struct Render
{
    std::vector<float> left, right;
};

inline Render render (Sampler& sampler, std::vector<Event> events, int frames, int block = 256)
{
    Render out;
    out.left.assign (static_cast<std::size_t> (frames), 0.0f);
    out.right.assign (static_cast<std::size_t> (frames), 0.0f);
    std::size_t next = 0;
    std::vector<Event> local;
    for (int at = 0; at < frames; at += block)
    {
        const int n = std::min (block, frames - at);
        local.clear();
        while (next < events.size() && events[next].sampleOffset < at + n)
        {
            Event e = events[next++];
            e.sampleOffset -= at;
            local.push_back (e);
        }
        core::AudioBlock audio { out.left.data() + at, out.right.data() + at, n };
        sampler.process (audio, local.data(), static_cast<int> (local.size()));
    }
    return out;
}

inline float rms (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to && i < x.size(); ++i)
        sum += static_cast<double> (x[i]) * x[i];
    return static_cast<float> (std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, to - from))));
}

/** A unique temporary folder, removed on destruction. */
struct TempDir
{
    std::filesystem::path path;
    TempDir()
    {
        std::random_device device;
        path = std::filesystem::temp_directory_path() / ("kraftonic_sampler_test_" + std::to_string (device()));
        std::filesystem::create_directories (path);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all (path, ec);
    }
    void write (const std::string& name, const std::string& text) const
    {
        std::filesystem::create_directories ((path / name).parent_path());
        std::ofstream file (path / name, std::ios::binary);
        file << text;
    }
    std::string str() const { return path.string(); }
};
} // namespace kraftonic::sampler::test
