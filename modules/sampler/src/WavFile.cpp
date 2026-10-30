// SPDX-License-Identifier: MIT
#include "kraftonic/sampler/WavFile.h"

#include "PathUtil.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace kraftonic::sampler
{
namespace
{
constexpr std::uint64_t kMaxFileBytes = 1ull << 31; // 2 GiB: far above any sample we ship

std::uint16_t le16 (const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t> (p[0] | (p[1] << 8));
}
std::uint32_t le32 (const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8)
        | (static_cast<std::uint32_t> (p[2]) << 16) | (static_cast<std::uint32_t> (p[3]) << 24);
}

void put16 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back (static_cast<std::uint8_t> (v & 255u));
    out.push_back (static_cast<std::uint8_t> ((v >> 8) & 255u));
}
void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    put16 (out, v & 0xffffu);
    put16 (out, v >> 16);
}
void putTag (std::vector<std::uint8_t>& out, const char* tag)
{
    out.insert (out.end(), tag, tag + 4);
}
} // namespace

bool parseWav (const std::uint8_t* bytes, std::size_t size, WavData& out, std::string& error)
{
    out = WavData {};
    if (bytes == nullptr || size < 12 || std::memcmp (bytes, "RIFF", 4) != 0
        || std::memcmp (bytes + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    int format = 0, channels = 0, bits = 0, blockAlign = 0;
    std::uint32_t rate = 0;
    const std::uint8_t* data = nullptr;
    std::size_t dataBytes = 0;
    bool haveFmt = false;

    std::size_t at = 12;
    while (at + 8 <= size)
    {
        const std::uint8_t* chunk = bytes + at;
        const std::size_t chunkSize = le32 (chunk + 4);
        const std::size_t bodyStart = at + 8;
        // A truncated last chunk is clamped (common for the data chunk of a cut file).
        const std::size_t available = std::min (chunkSize, size - bodyStart);
        const std::uint8_t* body = bytes + bodyStart;

        if (std::memcmp (chunk, "fmt ", 4) == 0)
        {
            if (available < 16)
            {
                error = "fmt chunk too short";
                return false;
            }
            format = le16 (body);
            channels = le16 (body + 2);
            rate = le32 (body + 4);
            blockAlign = le16 (body + 12);
            bits = le16 (body + 14);
            if (format == 0xFFFE && available >= 26)
                format = le16 (body + 24); // sub-format GUID starts with the format tag
            haveFmt = true;
        }
        else if (std::memcmp (chunk, "data", 4) == 0)
        {
            data = body;
            dataBytes = available;
        }
        else if (std::memcmp (chunk, "smpl", 4) == 0 && available >= 36)
        {
            const std::uint32_t loops = le32 (body + 28);
            if (loops > 0 && available >= 36 + 24)
            {
                const std::uint8_t* loop = body + 36;
                out.loopStart = static_cast<std::int64_t> (le32 (loop + 8));
                out.loopEnd = static_cast<std::int64_t> (le32 (loop + 12)) + 1; // smpl end is inclusive
            }
        }

        if (chunkSize > size - bodyStart)
            break;
        at = bodyStart + chunkSize + (chunkSize & 1u);
    }

    if (! haveFmt || data == nullptr)
    {
        error = haveFmt ? "no data chunk" : "no fmt chunk";
        return false;
    }
    if (channels < 1 || channels > 2)
    {
        error = "unsupported channel count " + std::to_string (channels) + " (mono or stereo only)";
        return false;
    }
    if (rate < 8000 || rate > 768000)
    {
        error = "unsupported sample rate " + std::to_string (rate);
        return false;
    }
    const bool isFloat = format == 3;
    if (! ((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (isFloat && bits == 32)))
    {
        error = "unsupported sample format (tag " + std::to_string (format) + ", "
            + std::to_string (bits) + " bit)";
        return false;
    }
    const int bytesPerSample = bits / 8;
    if (blockAlign != bytesPerSample * channels)
    {
        error = "inconsistent block alignment";
        return false;
    }

    const std::size_t frames = dataBytes / static_cast<std::size_t> (blockAlign);
    out.channels = channels;
    out.sampleRate = static_cast<int> (rate);
    out.bitsPerSample = bits;
    out.audio.assign (static_cast<std::size_t> (channels), std::vector<float> (frames));
    for (std::size_t f = 0; f < frames; ++f)
    {
        const std::uint8_t* frame = data + f * static_cast<std::size_t> (blockAlign);
        for (int c = 0; c < channels; ++c)
        {
            const std::uint8_t* s = frame + c * bytesPerSample;
            float v = 0.0f;
            if (isFloat)
            {
                const std::uint32_t raw = le32 (s);
                std::memcpy (&v, &raw, sizeof v);
                if (! std::isfinite (v))
                    v = 0.0f;
                v = std::clamp (v, -1.0f, 1.0f);
            }
            else if (bits == 16)
                v = static_cast<float> (static_cast<std::int16_t> (le16 (s))) / 32768.0f;
            else if (bits == 24)
            {
                const std::int32_t raw = static_cast<std::int32_t> (
                    static_cast<std::uint32_t> (s[0] << 8) | static_cast<std::uint32_t> (s[1] << 16)
                    | static_cast<std::uint32_t> (s[2]) << 24);
                v = static_cast<float> (raw >> 8) / 8388608.0f;
            }
            else
                v = static_cast<float> (static_cast<double> (static_cast<std::int32_t> (le32 (s)))
                                        / 2147483648.0);
            out.audio[static_cast<std::size_t> (c)][f] = v;
        }
    }
    if (out.loopStart < 0 || out.loopEnd <= out.loopStart
        || out.loopEnd > static_cast<std::int64_t> (frames))
        out.loopStart = out.loopEnd = -1;
    return true;
}

bool readWav (const std::string& pathUtf8, WavData& out, std::string& error)
{
    std::ifstream file (detail::pathFromUtf8 (pathUtf8), std::ios::binary | std::ios::ate);
    if (! file)
    {
        error = "cannot open file";
        return false;
    }
    const auto end = file.tellg();
    if (end < 0 || static_cast<std::uint64_t> (end) > kMaxFileBytes)
    {
        error = "file too large or unreadable";
        return false;
    }
    std::vector<std::uint8_t> bytes (static_cast<std::size_t> (end));
    file.seekg (0);
    if (! bytes.empty() && ! file.read (reinterpret_cast<char*> (bytes.data()),
                                        static_cast<std::streamsize> (bytes.size())))
    {
        error = "read error";
        return false;
    }
    return parseWav (bytes.data(), bytes.size(), out, error);
}

bool writeWav (const std::string& pathUtf8, const std::vector<std::vector<float>>& audio,
               int sampleRate, WavFormat format, std::string& error,
               std::int64_t loopStart, std::int64_t loopEnd)
{
    if (audio.empty() || audio.size() > 2)
    {
        error = "need 1 or 2 channels";
        return false;
    }
    const std::size_t frames = audio[0].size();
    for (const auto& channel : audio)
        if (channel.size() != frames)
        {
            error = "channels differ in length";
            return false;
        }
    const auto channels = static_cast<std::uint32_t> (audio.size());
    const std::uint32_t bytesPerSample = format == WavFormat::pcm16 ? 2u : (format == WavFormat::pcm24 ? 3u : 4u);
    const std::uint64_t dataBytes64 = static_cast<std::uint64_t> (frames) * channels * bytesPerSample;
    if (dataBytes64 > 0xFFFF0000ull)
    {
        error = "too long for a WAV file";
        return false;
    }
    const auto dataBytes = static_cast<std::uint32_t> (dataBytes64);
    const bool hasLoop = loopStart >= 0 && loopEnd > loopStart && loopEnd <= static_cast<std::int64_t> (frames);

    std::vector<std::uint8_t> out;
    out.reserve (dataBytes + 128);
    putTag (out, "RIFF");
    put32 (out, 0); // patched below
    putTag (out, "WAVE");
    putTag (out, "fmt ");
    put32 (out, 16);
    put16 (out, format == WavFormat::float32 ? 3u : 1u);
    put16 (out, channels);
    put32 (out, static_cast<std::uint32_t> (sampleRate));
    put32 (out, static_cast<std::uint32_t> (sampleRate) * channels * bytesPerSample);
    put16 (out, channels * bytesPerSample);
    put16 (out, bytesPerSample * 8u);
    if (hasLoop)
    {
        putTag (out, "smpl");
        put32 (out, 36 + 24);
        for (int i = 0; i < 7; ++i)
            put32 (out, 0);
        put32 (out, 1); // one loop
        put32 (out, 0);
        put32 (out, 0);                                          // cue id
        put32 (out, 0);                                          // forward loop
        put32 (out, static_cast<std::uint32_t> (loopStart));
        put32 (out, static_cast<std::uint32_t> (loopEnd - 1)); // inclusive
        put32 (out, 0);
        put32 (out, 0);
    }
    putTag (out, "data");
    put32 (out, dataBytes);
    for (std::size_t f = 0; f < frames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c)
        {
            float v = audio[c][f];
            if (! std::isfinite (v))
                v = 0.0f;
            if (format == WavFormat::float32)
            {
                std::uint32_t raw = 0;
                std::memcpy (&raw, &v, sizeof v);
                put32 (out, raw);
            }
            else if (format == WavFormat::pcm16)
            {
                const auto s = static_cast<std::int32_t> (std::lround (std::clamp (v * 32768.0f, -32768.0f, 32767.0f)));
                put16 (out, static_cast<std::uint32_t> (s) & 0xffffu);
            }
            else
            {
                const auto s = static_cast<std::int32_t> (
                    std::lround (std::clamp (static_cast<double> (v) * 8388608.0, -8388608.0, 8388607.0)));
                const auto u = static_cast<std::uint32_t> (s);
                out.push_back (static_cast<std::uint8_t> (u & 255u));
                out.push_back (static_cast<std::uint8_t> ((u >> 8) & 255u));
                out.push_back (static_cast<std::uint8_t> ((u >> 16) & 255u));
            }
        }
    const auto riffSize = static_cast<std::uint32_t> (out.size() - 8);
    for (int i = 0; i < 4; ++i)
        out[static_cast<std::size_t> (4 + i)] = static_cast<std::uint8_t> ((riffSize >> (8 * i)) & 255u);

    std::ofstream file (detail::pathFromUtf8 (pathUtf8), std::ios::binary | std::ios::trunc);
    if (! file || ! file.write (reinterpret_cast<const char*> (out.data()), static_cast<std::streamsize> (out.size())))
    {
        error = "cannot write file";
        return false;
    }
    return true;
}
} // namespace kraftonic::sampler
