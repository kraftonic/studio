// SPDX-License-Identifier: MIT
#include "TestHelpers.h"

#include "kraftonic/sampler/SampleLocations.h"
#include "kraftonic/sampler/WavFile.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdlib>
#include <fstream>
#include <random>

using namespace kraftonic::sampler;
using namespace kraftonic::sampler::test;
using Catch::Matchers::WithinAbs;

namespace
{
std::vector<std::uint8_t> fileBytes (const std::filesystem::path& path)
{
    std::ifstream file (path, std::ios::binary);
    return { std::istreambuf_iterator<char> (file), std::istreambuf_iterator<char>() };
}
} // namespace

TEST_CASE ("WAV round trip in every supported format, with a loop", "[wav]")
{
    TempDir dir;
    std::vector<std::vector<float>> audio (2, std::vector<float> (1000));
    for (int i = 0; i < 1000; ++i)
    {
        audio[0][static_cast<std::size_t> (i)] = 0.5f * std::sin (0.05f * static_cast<float> (i));
        audio[1][static_cast<std::size_t> (i)] = -0.25f * std::sin (0.031f * static_cast<float> (i));
    }
    for (auto [format, tolerance] : { std::pair { WavFormat::pcm16, 1.0 / 32768.0 },
                                      std::pair { WavFormat::pcm24, 1.0 / 8388608.0 },
                                      std::pair { WavFormat::float32, 0.0 } })
    {
        const std::string path = (dir.path / "rt.wav").string();
        std::string error;
        REQUIRE (writeWav (path, audio, 44100, format, error, 100, 900));
        WavData read;
        REQUIRE (readWav (path, read, error));
        CHECK (read.channels == 2);
        CHECK (read.sampleRate == 44100);
        CHECK (read.frames() == 1000);
        CHECK (read.loopStart == 100);
        CHECK (read.loopEnd == 900);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 1000; i += 37)
                CHECK_THAT (read.audio[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)],
                            WithinAbs (audio[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)], tolerance + 1e-7));
    }
}

TEST_CASE ("corrupt, truncated and unsupported WAV files fail cleanly", "[wav][robustness]")
{
    TempDir dir;
    std::string error;
    REQUIRE (writeWav ((dir.path / "good.wav").string(), constant (0.25f, 4000, 2), 48000, WavFormat::pcm16, error));
    const auto good = fileBytes (dir.path / "good.wav");
    WavData out;

    SECTION ("missing file")
    {
        CHECK_FALSE (readWav ((dir.path / "nope.wav").string(), out, error));
    }
    SECTION ("empty and tiny inputs")
    {
        CHECK_FALSE (parseWav (nullptr, 0, out, error));
        CHECK_FALSE (parseWav (good.data(), 11, out, error));
        CHECK_FALSE (parseWav (good.data(), 30, out, error)); // fmt chunk cut
    }
    SECTION ("truncated data chunk keeps what is there")
    {
        REQUIRE (parseWav (good.data(), good.size() - 1001, out, error));
        CHECK (out.frames() < 4000);
        CHECK (out.frames() > 3000);
    }
    SECTION ("garbage never crashes")
    {
        std::mt19937 random (1234);
        for (int trial = 0; trial < 300; ++trial)
        {
            auto bytes = good;
            const int flips = 1 + static_cast<int> (random() % 16);
            for (int f = 0; f < flips; ++f)
                bytes[random() % 64] = static_cast<std::uint8_t> (random()); // header region
            bytes.resize (random() % bytes.size());
            WavData parsed;
            const bool ok = parseWav (bytes.data(), bytes.size(), parsed, error);
            if (ok)
            {
                CHECK ((parsed.channels == 1 || parsed.channels == 2));
                for (const auto& channel : parsed.audio)
                    for (float v : channel)
                        REQUIRE (std::isfinite (v));
            }
        }
    }
    SECTION ("unsupported layouts are refused")
    {
        auto bytes = good;
        bytes[22] = 6; // six channels
        CHECK_FALSE (parseWav (bytes.data(), bytes.size(), out, error));
        bytes = good;
        bytes[34] = 8; // 8-bit
        CHECK_FALSE (parseWav (bytes.data(), bytes.size(), out, error));
    }
}

TEST_CASE ("manifest: an instrument folder loads with all its fields", "[bank]")
{
    TempDir dir;
    std::string error;
    std::filesystem::create_directories (dir.path / "samples");
    REQUIRE (writeWav ((dir.path / "samples/a.wav").string(), constant (0.1f, 2000), 44100, WavFormat::pcm16, error));
    REQUIRE (writeWav ((dir.path / "samples/b#2.wav").string(), constant (0.2f, 3000, 2), 48000, WavFormat::pcm24, error));
    dir.write ("instrument.ini", R"(# test
[instrument]
format = 1
name = Test Grand
gain_db = -3
velocity_range_db = 9
velocity_crossfade = 6
velocity_crossfade_curve = power
attack_ms = 2
release_ms = 300
retrigger_release_ms = 120
pedal_resonance = 0.25
release_trigger_gain_db = -4
polyphony = 40

[zone]
sample = samples/a.wav
root = 60
key = 55-64
velocity = 1-80
tune_cents = -12.5
gain_db = 1.5

[zone]
sample = samples/b#2.wav ; comment
root = 62
key = 55-64
velocity = 81-127
loop = 100-2900
release_ms = 900
damped = false

[zone]
sample = samples/a.wav
key = 60
trigger = release
key_tracking = 0
release_decay_db_per_s = 3
round_robin = 2

[zone]
sample = samples/a.wav
trigger = pedal_down
)");
    LoadReport report;
    const auto bank = SampleBank::load (dir.str(), report);
    REQUIRE (bank != nullptr);
    CHECK (report.ok);
    CHECK (report.warnings.empty());
    CHECK (report.zones == 4);
    CHECK (report.samples == 2); // a.wav is shared by three zones
    const auto& s = bank->settings();
    CHECK (s.name == "Test Grand");
    CHECK_THAT (s.gainDb, WithinAbs (-3.0, 1e-6));
    CHECK_THAT (s.velocityCrossfade, WithinAbs (6.0, 1e-6));
    CHECK (s.equalPowerCrossfade);
    CHECK (s.polyphony == 40);
    CHECK_THAT (s.pedalResonance, WithinAbs (0.25, 1e-6));
    const auto& z = bank->zones();
    CHECK (z[0].keyLow == 55);
    CHECK (z[0].velocityHigh == 80);
    CHECK_THAT (z[0].tuneCents, WithinAbs (-12.5, 1e-6));
    CHECK_FALSE (z[1].damped);
    CHECK (bank->sample (z[1].sample).isLooped());
    CHECK (bank->sample (z[1].sample).loopEnd() == 2900);
    CHECK (bank->sample (z[1].sample).channels() == 2);
    CHECK (bank->sample (z[1].sample).sampleRate() == 48000);
    CHECK (z[2].trigger == Trigger::release);
    CHECK (z[2].keyLow == 60);
    CHECK (z[2].keyHigh == 60);
    CHECK (z[2].roundRobinSet == 2);
    CHECK (z[3].trigger == Trigger::pedalDown);
    CHECK (bank->zonesFor (Trigger::attack, 60).size() == 2);
    CHECK (bank->zonesFor (Trigger::attack, 66).empty());
    CHECK (bank->zonesFor (Trigger::release, 60).size() == 1);
    CHECK (bank->zonesFor (Trigger::pedalDown, 99).size() == 1);
    CHECK (bank->hasReleaseZones());
    CHECK (bank->memoryBytes() == report.memoryBytes);
    CHECK (bank->memoryBytes() > 0);
}

TEST_CASE ("manifest: missing and corrupt files are skipped, never fatal unless nothing plays", "[bank][robustness]")
{
    TempDir dir;
    std::string error;
    std::filesystem::create_directories (dir.path / "samples");
    REQUIRE (writeWav ((dir.path / "samples/ok.wav").string(), constant (0.1f, 500), 44100, WavFormat::pcm16, error));
    dir.write ("samples/corrupt.wav", "RIFF\x10\0\0\0WAVEjunkjunkjunk");
    const std::string zones = R"(
[zone]
sample = samples/missing.wav
key = 0-127
[zone]
sample = samples/corrupt.wav
key = 0-127
[zone]
sample = ../escape.wav
[zone]
sample = /etc/passwd
)";
    LoadReport report;

    SECTION ("one good zone among bad ones still loads")
    {
        dir.write ("instrument.ini", "[instrument]\nformat = 1\nbogus = 3\nrelease_ms = abc\n" + zones
                                         + "[zone]\nsample = samples/ok.wav\nkey = 0-127\nvelocity = 90-10\n"
                                           "[zone]\nsample = samples/ok.wav\nkey = 40-50\n[weird]\nx = 1\n");
        const auto bank = SampleBank::load (dir.str(), report);
        REQUIRE (bank != nullptr);
        CHECK (bank->zones().size() == 1); // the inverted velocity range is dropped too
        CHECK (report.warnings.size() >= 7);
    }
    SECTION ("nothing playable fails with an error")
    {
        dir.write ("instrument.ini", "[instrument]\nformat = 1\n" + zones);
        CHECK (SampleBank::load (dir.str(), report) == nullptr);
        CHECK_FALSE (report.ok);
        CHECK_FALSE (report.error.empty());
    }
    SECTION ("missing folder, missing manifest, wrong format")
    {
        CHECK (SampleBank::load ((dir.path / "does-not-exist").string(), report) == nullptr);
        CHECK_FALSE (report.error.empty());
        CHECK (SampleBank::load (dir.str(), report) == nullptr); // no instrument.ini yet
        dir.write ("instrument.ini", "[instrument]\nformat = 7\n[zone]\nsample = samples/ok.wav\n");
        CHECK (SampleBank::load (dir.str(), report) == nullptr);
        dir.write ("instrument.ini", std::string ("\0\xff\xfe garbage \n[[[", 15));
        CHECK (SampleBank::load (dir.str(), report) == nullptr);
    }
    SECTION ("a loop outside the sample falls back to one-shot")
    {
        dir.write ("instrument.ini", "[instrument]\nformat = 1\n[zone]\nsample = samples/ok.wav\nloop = 100-99999\n");
        const auto bank = SampleBank::load (dir.str(), report);
        REQUIRE (bank != nullptr);
        CHECK_FALSE (bank->sample (0).isLooped());
        CHECK (report.warnings.size() == 1);
    }
}

TEST_CASE ("a sampler with a missing bank stays silent and safe", "[robustness]")
{
    Sampler sampler;
    sampler.prepare ({ 48000.0, 256 });
    sampler.setBank (nullptr);
    const auto out = render (sampler, { noteOn (0, 60, 1.0f), pedal (10, true), noteOff (20, 60), pedal (30, false) }, 1024);
    CHECK (rms (out.left, 0, 1024) == 0.0f);
    CHECK (sampler.activeVoiceCount() == 0);
}

TEST_CASE ("sample locations: environment override and platform default", "[locations]")
{
    constexpr const char* variable = "KRAFTONIC_TEST_SAMPLES";
    const std::string fallback = platformDefaultSampleDirectory ("Vendor", "My Synth");
    CHECK_FALSE (fallback.empty());
#if defined(__APPLE__)
    CHECK (fallback == "/Library/Application Support/Vendor/My Synth/Samples");
#elif defined(_WIN32)
    CHECK (fallback.find ("\\Vendor\\My Synth\\Samples") != std::string::npos);
#else
    CHECK (fallback.find ("vendor/my synth/samples") != std::string::npos);
#endif
    CHECK (instrumentDirectory ("root", "grand").find ("grand") != std::string::npos);
    CHECK (sampleDirectory ("Vendor", "My Synth", nullptr) == fallback);
#if ! defined(_WIN32)
    ::setenv (variable, "/tmp/kraftonic-override", 1);
    CHECK (sampleDirectory ("Vendor", "My Synth", variable) == "/tmp/kraftonic-override");
    ::unsetenv (variable);
    CHECK (sampleDirectory ("Vendor", "My Synth", variable) == fallback);
#endif
}
