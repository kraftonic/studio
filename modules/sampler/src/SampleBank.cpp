// SPDX-License-Identifier: MIT
#include "kraftonic/sampler/SampleBank.h"

#include "kraftonic/sampler/WavFile.h"

#include "PathUtil.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace kraftonic::sampler
{
// ---------------------------------------------------------------------------
//  Sample
// ---------------------------------------------------------------------------
Sample Sample::fromFloat (const std::vector<std::vector<float>>& audio, int sampleRate,
                          std::int64_t loopStart, std::int64_t loopEnd)
{
    Sample s;
    s.rate = sampleRate;
    s.numChannels = static_cast<int> (std::min<std::size_t> (audio.size(), 2));
    if (s.numChannels == 0)
    {
        s.numChannels = 1;
        s.data.assign (2u * kSamplePadding, 0);
        return s;
    }
    std::int64_t frameCount = static_cast<std::int64_t> (audio[0].size());
    for (int c = 1; c < s.numChannels; ++c)
        frameCount = std::min (frameCount, static_cast<std::int64_t> (audio[static_cast<std::size_t> (c)].size()));

    const bool looped = loopStart >= 0 && loopEnd > loopStart && loopEnd <= frameCount && loopEnd - loopStart >= 2;
    if (looped)
    {
        frameCount = loopEnd; // audio after the loop is never heard
        s.loopStartFrame = loopStart;
        s.loopEndFrame = loopEnd;
    }
    s.numFrames = frameCount;
    s.data.assign (static_cast<std::size_t> (s.numChannels) * s.stride(), 0);

    float peakAbs = 0.0f;
    for (int c = 0; c < s.numChannels; ++c)
    {
        const auto& in = audio[static_cast<std::size_t> (c)];
        std::int16_t* out = s.data.data() + static_cast<std::size_t> (c) * s.stride() + kSamplePadding;
        auto convert = [] (float v) noexcept
        {
            if (! std::isfinite (v))
                v = 0.0f;
            return static_cast<std::int16_t> (std::lround (std::clamp (v * 32768.0f, -32768.0f, 32767.0f)));
        };
        for (std::int64_t f = 0; f < frameCount; ++f)
        {
            const float v = in[static_cast<std::size_t> (f)];
            out[f] = convert (v);
            peakAbs = std::max (peakAbs, std::abs (v));
        }
        if (looped)
        {
            // Guard frames after the loop end continue from the loop start.
            for (int p = 0; p < kSamplePadding; ++p)
            {
                const std::int64_t src = loopStart + (p % (loopEnd - loopStart));
                out[frameCount + p] = out[src];
            }
        }
    }
    s.peakValue = std::min (peakAbs, 1.0f);
    return s;
}

// ---------------------------------------------------------------------------
//  SampleBank: building and queries
// ---------------------------------------------------------------------------
int SampleBank::addSample (Sample sample)
{
    samples.push_back (std::move (sample));
    return static_cast<int> (samples.size()) - 1;
}

void SampleBank::addZone (const Zone& zone)
{
    zoneList.push_back (zone);
}

bool SampleBank::finalize (std::vector<std::string>* warnings)
{
    auto warn = [warnings] (std::string text)
    {
        if (warnings != nullptr)
            warnings->push_back (std::move (text));
    };

    std::vector<Zone> valid;
    valid.reserve (zoneList.size());
    for (std::size_t i = 0; i < zoneList.size(); ++i)
    {
        Zone z = zoneList[i];
        if (z.sample < 0 || z.sample >= numSamples() || sample (z.sample).frames() < 1)
        {
            warn ("zone " + std::to_string (i) + ": no audio, skipped");
            continue;
        }
        z.keyLow = std::clamp (z.keyLow, 0, 127);
        z.keyHigh = std::clamp (z.keyHigh, 0, 127);
        z.rootKey = std::clamp (z.rootKey, 0, 127);
        z.velocityLow = std::clamp (z.velocityLow, 1, 127);
        z.velocityHigh = std::clamp (z.velocityHigh, 1, 127);
        if (z.keyLow > z.keyHigh || z.velocityLow > z.velocityHigh)
        {
            warn ("zone " + std::to_string (i) + ": empty key or velocity range, skipped");
            continue;
        }
        if (! std::isfinite (z.tuneCents) || ! std::isfinite (z.gainDb) || ! std::isfinite (z.keyTracking)
            || ! std::isfinite (z.releaseMs) || ! std::isfinite (z.releaseDecayDbPerSecond))
        {
            warn ("zone " + std::to_string (i) + ": non-finite value, skipped");
            continue;
        }
        z.tuneCents = std::clamp (z.tuneCents, -2400.0f, 2400.0f);
        z.gainDb = std::clamp (z.gainDb, -96.0f, 24.0f);
        z.keyTracking = std::clamp (z.keyTracking, 0.0f, 2.0f);
        z.releaseDecayDbPerSecond = std::clamp (z.releaseDecayDbPerSecond, 0.0f, 200.0f);
        valid.push_back (z);
    }
    zoneList = std::move (valid);

    auto& s = instrument;
    auto finiteOr = [] (float v, float fallback) { return std::isfinite (v) ? v : fallback; };
    s.gainDb = std::clamp (finiteOr (s.gainDb, 0.0f), -60.0f, 24.0f);
    s.velocityRangeDb = std::clamp (finiteOr (s.velocityRangeDb, 12.0f), 0.0f, 60.0f);
    s.velocityCrossfade = std::clamp (finiteOr (s.velocityCrossfade, 10.0f), 0.0f, 64.0f);
    s.attackMs = std::clamp (finiteOr (s.attackMs, 1.0f), 0.0f, 5000.0f);
    s.releaseMs = std::clamp (finiteOr (s.releaseMs, 250.0f), 5.0f, 30000.0f);
    s.retriggerReleaseMs = std::clamp (finiteOr (s.retriggerReleaseMs, 400.0f), 5.0f, 30000.0f);
    s.pedalResonance = std::clamp (finiteOr (s.pedalResonance, 0.0f), 0.0f, 1.0f);
    s.releaseTriggerGainDb = std::clamp (finiteOr (s.releaseTriggerGainDb, 0.0f), -96.0f, 24.0f);
    s.polyphony = std::clamp (s.polyphony, 1, 256);

    for (auto& trigger : byKey)
        for (auto& list : trigger)
            list.clear();
    anyRelease = false;
    bool anyAttack = false;
    for (std::size_t i = 0; i < zoneList.size(); ++i)
    {
        const Zone& z = zoneList[i];
        const auto t = static_cast<std::size_t> (z.trigger);
        if (z.trigger == Trigger::pedalDown || z.trigger == Trigger::pedalUp)
        {
            byKey[t][0].push_back (static_cast<int> (i));
            continue;
        }
        for (int k = z.keyLow; k <= z.keyHigh; ++k)
            byKey[t][static_cast<std::size_t> (k)].push_back (static_cast<int> (i));
        anyRelease = anyRelease || z.trigger == Trigger::release;
        anyAttack = anyAttack || z.trigger == Trigger::attack;
    }
    return anyAttack;
}

const std::vector<int>& SampleBank::zonesFor (Trigger trigger, int key) const noexcept
{
    const bool pedal = trigger == Trigger::pedalDown || trigger == Trigger::pedalUp;
    const int k = pedal ? 0 : std::clamp (key, 0, 127);
    return byKey[static_cast<std::size_t> (trigger)][static_cast<std::size_t> (k)];
}

std::size_t SampleBank::memoryBytes() const noexcept
{
    std::size_t bytes = 0;
    for (const auto& s : samples)
        bytes += s.memoryBytes();
    return bytes;
}

// ---------------------------------------------------------------------------
//  Manifest parsing
// ---------------------------------------------------------------------------
namespace
{
std::string trim (const std::string& s)
{
    const auto first = s.find_first_not_of (" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of (" \t\r\n");
    return s.substr (first, last - first + 1);
}

bool parseInt (const std::string& text, int& out)
{
    const std::string t = trim (text);
    if (t.empty())
        return false;
    const char* begin = t.data();
    const char* end = t.data() + t.size();
    if (*begin == '+')
        ++begin;
    long long v = 0;
    const auto result = std::from_chars (begin, end, v);
    if (result.ec != std::errc() || result.ptr != end || v < -1000000000LL || v > 1000000000LL)
        return false;
    out = static_cast<int> (v);
    return true;
}

bool parseInt64 (const std::string& text, std::int64_t& out)
{
    const std::string t = trim (text);
    if (t.empty())
        return false;
    long long v = 0;
    const auto result = std::from_chars (t.data(), t.data() + t.size(), v);
    if (result.ec != std::errc() || result.ptr != t.data() + t.size())
        return false;
    out = static_cast<std::int64_t> (v);
    return true;
}

bool parseFloat (const std::string& text, float& out)
{
    // std::from_chars for floating point is missing on older Apple toolchains,
    // so use a classic-locale stream.
    std::istringstream stream (trim (text));
    stream.imbue (std::locale::classic());
    double v = 0.0;
    stream >> v;
    if (stream.fail() || ! stream.eof() || ! std::isfinite (v))
        return false;
    out = static_cast<float> (v);
    return true;
}

/** "a-b" or "a" (then low = high = a). Negative numbers are not ranges. */
bool parseRange (const std::string& text, int& low, int& high)
{
    const std::string t = trim (text);
    const auto dash = t.find ('-', 1);
    if (dash == std::string::npos)
    {
        if (! parseInt (t, low))
            return false;
        high = low;
        return true;
    }
    return parseInt (t.substr (0, dash), low) && parseInt (t.substr (dash + 1), high);
}

bool parseRange64 (const std::string& text, std::int64_t& low, std::int64_t& high)
{
    const std::string t = trim (text);
    const auto dash = t.find ('-', 1);
    if (dash == std::string::npos)
        return false;
    return parseInt64 (t.substr (0, dash), low) && parseInt64 (t.substr (dash + 1), high);
}

bool parseBool (const std::string& text, bool& out)
{
    const std::string t = trim (text);
    if (t == "1" || t == "true" || t == "yes")
        out = true;
    else if (t == "0" || t == "false" || t == "no")
        out = false;
    else
        return false;
    return true;
}

bool parseTrigger (const std::string& text, Trigger& out)
{
    const std::string t = trim (text);
    if (t == "attack")
        out = Trigger::attack;
    else if (t == "release")
        out = Trigger::release;
    else if (t == "pedal_down")
        out = Trigger::pedalDown;
    else if (t == "pedal_up")
        out = Trigger::pedalUp;
    else
        return false;
    return true;
}

/** A comment starts with '#' or ';' at the start of a line or after
    whitespace, so file names like "F#3.wav" keep their '#'. */
std::size_t commentStart (const std::string& line)
{
    for (std::size_t i = 0; i < line.size(); ++i)
        if ((line[i] == '#' || line[i] == ';') && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t'))
            return i;
    return line.size();
}

struct ZoneEntry
{
    Zone zone;
    std::string file;
    std::int64_t loopStart = -1;
    std::int64_t loopEnd = -1;
    int line = 0;
};

bool isSafeRelativePath (const std::string& file)
{
    if (file.empty())
        return false;
    const auto path = detail::pathFromUtf8 (file);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return false;
    for (const auto& part : path)
        if (part == "..")
            return false;
    return true;
}
} // namespace

std::shared_ptr<const SampleBank> SampleBank::loadFromManifest (const std::string& manifestText,
                                                                const std::string& directoryUtf8,
                                                                LoadReport& report,
                                                                const LoadOptions& options)
{
    try
    {
        return parseAndLoad (manifestText, directoryUtf8, report, options);
    }
    catch (const std::exception& e)
    {
        // Out of memory or a filesystem error: report it, never crash the host.
        report = LoadReport {};
        report.error = std::string ("loading failed: ") + e.what();
        return nullptr;
    }
}

std::shared_ptr<const SampleBank> SampleBank::parseAndLoad (const std::string& manifestText,
                                                            const std::string& directoryUtf8,
                                                            LoadReport& report, const LoadOptions&)
{
    report = LoadReport {};
    auto bank = std::make_shared<SampleBank>();
    InstrumentSettings& s = bank->instrument;
    std::vector<ZoneEntry> entries;
    int format = 0;

    enum class Section
    {
        none,
        instrument,
        zone,
        unknown
    } section = Section::none;

    std::istringstream lines (manifestText);
    std::string raw;
    int lineNumber = 0;
    auto warn = [&report, &lineNumber] (const std::string& text)
    { report.warnings.push_back ("line " + std::to_string (lineNumber) + ": " + text); };

    while (std::getline (lines, raw))
    {
        ++lineNumber;
        std::string line = raw;
        line.erase (commentStart (line));
        line = trim (line);
        if (line.empty())
            continue;
        if (line.front() == '[')
        {
            if (line == "[instrument]")
                section = Section::instrument;
            else if (line == "[zone]")
            {
                section = Section::zone;
                entries.push_back ({});
                entries.back().line = lineNumber;
            }
            else
            {
                section = Section::unknown;
                warn ("unknown section " + line + " ignored");
            }
            continue;
        }
        const auto equals = line.find ('=');
        if (equals == std::string::npos)
        {
            warn ("expected key = value");
            continue;
        }
        const std::string key = trim (line.substr (0, equals));
        const std::string value = trim (line.substr (equals + 1));
        bool ok = true;

        if (section == Section::instrument)
        {
            if (key == "format")
                ok = parseInt (value, format);
            else if (key == "name")
                s.name = value;
            else if (key == "gain_db")
                ok = parseFloat (value, s.gainDb);
            else if (key == "velocity_range_db")
                ok = parseFloat (value, s.velocityRangeDb);
            else if (key == "velocity_crossfade")
                ok = parseFloat (value, s.velocityCrossfade);
            else if (key == "velocity_crossfade_curve")
            {
                ok = value == "linear" || value == "power";
                s.equalPowerCrossfade = value == "power";
            }
            else if (key == "attack_ms")
                ok = parseFloat (value, s.attackMs);
            else if (key == "release_ms")
                ok = parseFloat (value, s.releaseMs);
            else if (key == "retrigger_release_ms")
                ok = parseFloat (value, s.retriggerReleaseMs);
            else if (key == "pedal_resonance")
                ok = parseFloat (value, s.pedalResonance);
            else if (key == "release_trigger_gain_db")
                ok = parseFloat (value, s.releaseTriggerGainDb);
            else if (key == "polyphony")
                ok = parseInt (value, s.polyphony);
            else
                warn ("unknown instrument key '" + key + "' ignored");
        }
        else if (section == Section::zone)
        {
            ZoneEntry& e = entries.back();
            Zone& z = e.zone;
            if (key == "sample")
                e.file = value;
            else if (key == "root")
                ok = parseInt (value, z.rootKey);
            else if (key == "key")
                ok = parseRange (value, z.keyLow, z.keyHigh);
            else if (key == "velocity")
                ok = parseRange (value, z.velocityLow, z.velocityHigh);
            else if (key == "tune_cents")
                ok = parseFloat (value, z.tuneCents);
            else if (key == "gain_db")
                ok = parseFloat (value, z.gainDb);
            else if (key == "key_tracking")
                ok = parseFloat (value, z.keyTracking);
            else if (key == "trigger")
                ok = parseTrigger (value, z.trigger);
            else if (key == "round_robin")
                ok = parseInt (value, z.roundRobinSet);
            else if (key == "release_ms")
                ok = parseFloat (value, z.releaseMs);
            else if (key == "damped")
                ok = parseBool (value, z.damped);
            else if (key == "release_decay_db_per_s")
                ok = parseFloat (value, z.releaseDecayDbPerSecond);
            else if (key == "loop")
                ok = parseRange64 (value, e.loopStart, e.loopEnd);
            else
                warn ("unknown zone key '" + key + "' ignored");
        }
        else
            warn ("key outside a section ignored");

        if (! ok)
            warn ("bad value for '" + key + "': " + value);
    }

    if (format != 1)
    {
        report.error = format == 0 ? "manifest has no [instrument] format = 1"
                                   : "unsupported manifest format " + std::to_string (format);
        return nullptr;
    }

    // Load each distinct file once; zones that share a file share the sample.
    const auto directory = detail::pathFromUtf8 (directoryUtf8);
    std::map<std::string, int> loaded;
    for (auto& e : entries)
    {
        const std::string where = "zone at line " + std::to_string (e.line);
        if (! isSafeRelativePath (e.file))
        {
            report.warnings.push_back (where + ": missing or unsafe sample path '" + e.file + "', skipped");
            continue;
        }
        const std::string cacheKey = e.file + "|" + std::to_string (e.loopStart) + "|" + std::to_string (e.loopEnd);
        auto found = loaded.find (cacheKey);
        if (found == loaded.end())
        {
            WavData wav;
            std::string error;
            const std::string path = detail::pathToUtf8 (directory / detail::pathFromUtf8 (e.file));
            if (! readWav (path, wav, error))
            {
                report.warnings.push_back (where + ": " + e.file + ": " + error + ", skipped");
                loaded.emplace (cacheKey, -1);
                continue;
            }
            std::int64_t loopStart = e.loopStart, loopEnd = e.loopEnd;
            if (loopStart < 0 && wav.loopStart >= 0)
            {
                loopStart = wav.loopStart;
                loopEnd = wav.loopEnd;
            }
            if (loopStart >= 0 && (loopEnd <= loopStart + 1 || loopEnd > wav.frames()))
            {
                report.warnings.push_back (where + ": loop outside the sample, playing it unlooped");
                loopStart = loopEnd = -1;
            }
            const int index = bank->addSample (Sample::fromFloat (wav.audio, wav.sampleRate, loopStart, loopEnd));
            found = loaded.emplace (cacheKey, index).first;
        }
        if (found->second < 0)
            continue;
        e.zone.sample = found->second;
        bank->addZone (e.zone);
    }

    if (! bank->finalize (&report.warnings))
    {
        report.error = "no playable zone (every sample missing or invalid)";
        return nullptr;
    }
    report.ok = true;
    report.zones = static_cast<int> (bank->zoneList.size());
    report.samples = bank->numSamples();
    report.memoryBytes = bank->memoryBytes();
    return bank;
}

std::shared_ptr<const SampleBank> SampleBank::load (const std::string& directoryUtf8, LoadReport& report,
                                                    const LoadOptions& options)
{
    report = LoadReport {};
    try
    {
        const auto manifestPath = detail::pathFromUtf8 (directoryUtf8) / "instrument.ini";
        std::error_code ec;
        if (! std::filesystem::is_regular_file (manifestPath, ec))
        {
            report.error = "no instrument.ini in " + directoryUtf8;
            return nullptr;
        }
        std::ifstream file (manifestPath, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        if (! file && ! file.eof())
        {
            report.error = "cannot read instrument.ini";
            return nullptr;
        }
        return loadFromManifest (text.str(), directoryUtf8, report, options);
    }
    catch (const std::exception& e)
    {
        // Out of memory or a filesystem error: report it, never crash the host.
        report = LoadReport {};
        report.error = std::string ("loading failed: ") + e.what();
        return nullptr;
    }
}
} // namespace kraftonic::sampler
