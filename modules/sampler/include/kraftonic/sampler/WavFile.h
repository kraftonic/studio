// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic sampler: minimal RIFF/WAVE reader and writer (NOT real-time).
//
//  Reads PCM 16/24/32-bit and IEEE float 32-bit, mono or stereo, including
//  WAVE_FORMAT_EXTENSIBLE, and the first loop of a `smpl` chunk. Every size
//  and offset is bounds-checked: a truncated or corrupt file returns an error
//  instead of crashing.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace kraftonic::sampler
{
struct WavData
{
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;               // as stored in the file
    std::vector<std::vector<float>> audio; // [channel][frame], range [-1, 1]
    std::int64_t loopStart = -1;         // from a smpl chunk, -1 = none
    std::int64_t loopEnd = -1;           // exclusive

    std::int64_t frames() const noexcept
    {
        return audio.empty() ? 0 : static_cast<std::int64_t> (audio[0].size());
    }
};

/** Reads `pathUtf8`. Returns false and sets `error` on failure. */
bool readWav (const std::string& pathUtf8, WavData& out, std::string& error);

/** Parses an in-memory WAV image (used by readWav and the tests). */
bool parseWav (const std::uint8_t* bytes, std::size_t size, WavData& out, std::string& error);

enum class WavFormat
{
    pcm16,
    pcm24,
    float32
};

/** Writes `channels` x `frames` planar audio. A loop (start < end) is stored
    in a smpl chunk. 16/24-bit output is rounded and clipped. */
bool writeWav (const std::string& pathUtf8, const std::vector<std::vector<float>>& audio,
               int sampleRate, WavFormat format, std::string& error,
               std::int64_t loopStart = -1, std::int64_t loopEnd = -1);
} // namespace kraftonic::sampler
