// SPDX-License-Identifier: MIT
#pragma once

// Shared helpers for the piano module tests: a scripted renderer and a tiny
// synthetic grand sample set (harmonic tones in the "grand" folder layout),
// so the sampled path runs without the real recordings.

#include <kraftonic/piano/PianoLayer.h>
#include <kraftonic/sampler/WavFile.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace kraftonic::piano::test
{
inline constexpr int kRate = 48000;
inline constexpr int kBlock = 256;
inline constexpr double kPi = 3.141592653589793;

struct Hit
{
    enum class Kind
    {
        on,
        off,
        pedal,
        allNotesOff,
        reset,
        params
    };
    int at = 0;
    Kind kind = Kind::on;
    int key = 0;
    float value = 0.0f;
    Parameters params {};
};

inline Hit on (int key, float velocity, int at = 0) { return { at, Hit::Kind::on, key, velocity, {} }; }
inline Hit off (int key, int at = 0) { return { at, Hit::Kind::off, key, 0.0f, {} }; }
inline Hit pedal (float value, int at = 0) { return { at, Hit::Kind::pedal, 0, value, {} }; }
inline Hit allNotesOff (int at = 0) { return { at, Hit::Kind::allNotesOff, 0, 0.0f, {} }; }
inline Hit resetAt (int at = 0) { return { at, Hit::Kind::reset, 0, 0.0f, {} }; }
inline Hit change (const Parameters& p, int at = 0) { return { at, Hit::Kind::params, 0, 0.0f, p }; }

struct Out
{
    std::vector<float> l, r;

    float peak (int begin = 0, int end = -1) const
    {
        end = end < 0 ? static_cast<int> (l.size()) : end;
        float p = 0.0f;
        for (int i = begin; i < end; ++i)
            p = std::max ({ p, std::abs (l[static_cast<std::size_t> (i)]), std::abs (r[static_cast<std::size_t> (i)]) });
        return p;
    }

    double rms (int begin = 0, int end = -1) const
    {
        end = end < 0 ? static_cast<int> (l.size()) : end;
        double sum = 0.0;
        for (int i = begin; i < end; ++i)
        {
            const double a = l[static_cast<std::size_t> (i)];
            const double b = r[static_cast<std::size_t> (i)];
            sum += a * a + b * b;
        }
        return std::sqrt (sum / std::max (1, 2 * (end - begin)));
    }

    bool finite() const
    {
        for (std::size_t i = 0; i < l.size(); ++i)
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                return false;
        return true;
    }

    /** Largest sample-to-sample step in [begin, end). */
    float maxStep (int begin = 1, int end = -1) const
    {
        end = end < 0 ? static_cast<int> (l.size()) : end;
        float step = 0.0f;
        for (int i = std::max (1, begin); i < end; ++i)
            step = std::max ({ step, std::abs (l[static_cast<std::size_t> (i)] - l[static_cast<std::size_t> (i - 1)]),
                               std::abs (r[static_cast<std::size_t> (i)] - r[static_cast<std::size_t> (i - 1)]) });
        return step;
    }

    /** Share of first-difference energy in the left channel: higher is brighter. */
    double brightness (int begin, int end) const
    {
        double hi = 0.0, all = 0.0;
        for (int i = std::max (1, begin); i < end; ++i)
        {
            const double x = l[static_cast<std::size_t> (i)];
            const double d = x - l[static_cast<std::size_t> (i - 1)];
            hi += d * d;
            all += x * x;
        }
        return all > 0.0 ? hi / all : 0.0;
    }
};

/** Renders `samples` frames, applying hits sample-accurately (the layer is
    split at each hit) and in blocks of `block`. */
inline Out run (PianoLayer& layer, int samples, std::vector<Hit> hits, int block = kBlock)
{
    std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.at < b.at; });
    Out o;
    o.l.assign (static_cast<std::size_t> (samples), 1.0f);
    o.r.assign (static_cast<std::size_t> (samples), 1.0f);
    std::size_t next = 0;
    for (int pos = 0; pos < samples; pos += block)
    {
        const int end = std::min (pos + block, samples);
        int seg = pos;
        while (true)
        {
            while (next < hits.size() && hits[next].at <= seg)
            {
                const auto& h = hits[next++];
                switch (h.kind)
                {
                    case Hit::Kind::on: layer.noteOn (h.key, h.value); break;
                    case Hit::Kind::off: layer.noteOff (h.key); break;
                    case Hit::Kind::pedal: layer.sustain (h.value); break;
                    case Hit::Kind::allNotesOff: layer.allNotesOff(); break;
                    case Hit::Kind::reset: layer.reset(); break;
                    case Hit::Kind::params: layer.setParameters (h.params); break;
                }
            }
            const int stop = next < hits.size() ? std::min (end, hits[next].at) : end;
            if (stop > seg)
                layer.process (o.l.data() + seg, o.r.data() + seg, stop - seg);
            seg = stop;
            if (seg >= end)
                break;
        }
    }
    return o;
}

inline Parameters sound (int index, Parameters p = {})
{
    p.type = kModels[static_cast<std::size_t> (index)].type;
    p.model = modelWithinType (index);
    return p;
}

/** Harmonic tone at C4, stereo (right 20% quieter), decaying. */
inline std::vector<std::vector<float>> tone (double seconds, double decay)
{
    const auto frames = static_cast<std::size_t> (seconds * kRate);
    std::vector<std::vector<float>> audio (2, std::vector<float> (frames));
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double> (i) / kRate;
        const double envelope = 0.4 * std::exp (-t / decay) * std::min (1.0, t * 2000.0);
        double l = 0.0, r = 0.0;
        for (int n = 1; n <= 10; ++n)
        {
            const double phase = 2.0 * kPi * 261.6255653 * n * t;
            l += std::sin (phase) / n;
            r += 0.8 * std::sin (phase) / n;
        }
        audio[0][i] = static_cast<float> (envelope * l);
        audio[1][i] = static_cast<float> (envelope * r);
    }
    return audio;
}

/** A temporary sample root holding a synthetic instrument, by default the
    "grand" (or another sampled model's folder, PianoLayer::modelFolder). */
struct GrandFolder
{
    std::filesystem::path root;

    explicit GrandFolder (const char* folder = PianoLayer::kGrandFolder)
    {
        std::random_device device;
        root = std::filesystem::temp_directory_path() / ("kraftonic_piano_samples_" + std::to_string (device()));
        const auto dir = root / folder;
        std::filesystem::create_directories (dir / "samples");
        std::string error;
        sampler::writeWav ((dir / "samples" / "c4.wav").string(), tone (2.0, 0.8), kRate,
                           sampler::WavFormat::float32, error);
        std::ofstream ini (dir / "instrument.ini");
        ini << "[instrument]\nformat = 1\nname = Test grand\nrelease_ms = 200\n\n"
            << "[zone]\nsample = samples/c4.wav\nroot = 60\nkey = 0-127\nvelocity = 1-127\n";
    }
    ~GrandFolder()
    {
        std::error_code ec;
        std::filesystem::remove_all (root, ec);
    }
    GrandFolder (const GrandFolder&) = delete;
    GrandFolder& operator= (const GrandFolder&) = delete;

    std::string str() const { return root.string(); }
};
} // namespace kraftonic::piano::test
