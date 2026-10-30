// SPDX-License-Identifier: MIT
//
// kraftonic_sample_prep: turns a preparation plan into a Kraftonic sampler
// instrument folder (instrument.ini + 16-bit WAVs). An offline tool; never
// shipped.
//
//   kraftonic_sample_prep <plan.ini> <output-folder>
//
// The plan uses the manifest syntax (see modules/sampler/README.md). Keys a
// manifest understands are copied to the output; these extra keys steer the
// preparation:
//   [instrument] normalize_peak_db = -6   set gain_db so the loudest attack zone peaks here
//   [zone] source = <absolute path to the source WAV>
//          max_seconds = 12               truncate (with fade_ms fade-out)
//          fade_ms = 300
//          trim_db = -60                  drop leading audio quieter than this (0 = keep)
//          auto_loop = 1                  find a sustain loop and bake a crossfade into it
//          loop_min_seconds = 1.5
//          loop_xfade_ms = 250
//          mono = 1                       sum to mono
//          normalize_rms_db = -20         set this zone's gain_db so its RMS (loop, or first rms_seconds) is here
//          rms_seconds = 2
//          balance_db = 1.5               rescale L/R (opposite directions) so L is this many dB above R
//   [instrument] normalize_follow = 1     zones without normalize_rms_db move by the attack zones' average shift

#include "kraftonic/sampler/SampleBank.h"
#include "kraftonic/sampler/WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace kraftonic::sampler;

struct Section
{
    std::string name;
    std::vector<std::pair<std::string, std::string>> entries;

    std::string get (const std::string& key, const std::string& fallback = {}) const
    {
        for (const auto& [k, v] : entries)
            if (k == key)
                return v;
        return fallback;
    }
    double number (const std::string& key, double fallback) const
    {
        const std::string v = get (key);
        if (v.empty())
            return fallback;
        try
        {
            return std::stod (v);
        }
        catch (...)
        {
            return fallback;
        }
    }
};

std::string trim (const std::string& s)
{
    const auto first = s.find_first_not_of (" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of (" \t\r\n");
    return s.substr (first, last - first + 1);
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

std::vector<Section> parsePlan (const std::string& path)
{
    std::ifstream file (path);
    std::vector<Section> sections;
    std::string line;
    while (std::getline (file, line))
    {
        line.erase (commentStart (line));
        line = trim (line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']')
        {
            sections.push_back ({ line.substr (1, line.size() - 2), {} });
            continue;
        }
        const auto eq = line.find ('=');
        if (eq == std::string::npos || sections.empty())
            continue;
        sections.back().entries.emplace_back (trim (line.substr (0, eq)), trim (line.substr (eq + 1)));
    }
    return sections;
}

const char* const kPlanOnlyKeys[] = { "source", "max_seconds", "fade_ms", "trim_db", "auto_loop",
                                      "loop_min_seconds", "loop_xfade_ms", "mono", "normalize_peak_db",
                                      "normalize_rms_db", "rms_seconds", "normalize_follow",
                                      "balance_db" };

bool isPlanOnly (const std::string& key)
{
    for (const char* k : kPlanOnlyKeys)
        if (key == k)
            return true;
    return false;
}

using Audio = std::vector<std::vector<float>>;

std::vector<float> mixdown (const Audio& audio)
{
    std::vector<float> mono (audio[0].size(), 0.0f);
    for (const auto& channel : audio)
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] += channel[i] / static_cast<float> (audio.size());
    return mono;
}

void trimLeading (Audio& audio, int rate, double thresholdDb)
{
    if (thresholdDb >= 0.0)
        return;
    const float threshold = static_cast<float> (std::pow (10.0, thresholdDb / 20.0));
    std::size_t first = audio[0].size();
    for (const auto& channel : audio)
        for (std::size_t i = 0; i < channel.size() && i < first; ++i)
            if (std::abs (channel[i]) > threshold)
            {
                first = i;
                break;
            }
    const auto preRoll = static_cast<std::size_t> (0.002 * rate);
    const std::size_t start = first > preRoll ? first - preRoll : 0;
    if (start == 0 || start >= audio[0].size())
        return;
    for (auto& channel : audio)
        channel.erase (channel.begin(), channel.begin() + static_cast<std::ptrdiff_t> (start));
}

void truncateWithFade (Audio& audio, int rate, double maxSeconds, double fadeMs)
{
    auto frames = audio[0].size();
    if (maxSeconds > 0.0)
        frames = std::min (frames, static_cast<std::size_t> (maxSeconds * rate));
    for (auto& channel : audio)
        channel.resize (frames);
    const auto fade = std::min (frames, static_cast<std::size_t> (std::max (5.0, fadeMs) * 0.001 * rate));
    for (auto& channel : audio)
        for (std::size_t i = 0; i < fade; ++i)
        {
            const double t = static_cast<double> (i + 1) / static_cast<double> (fade); // 1/fade .. 1
            const double gain = 0.5 * (1.0 + std::cos (3.14159265358979 * t));         // ends at 0
            channel[frames - fade + i] *= static_cast<float> (gain);
        }
}

/** Normalized cross-correlation of the `window` frames ending at a and b. */
double similarity (const std::vector<float>& x, std::size_t a, std::size_t b, std::size_t window, std::size_t step)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t k = 0; k < window; k += step)
    {
        const double va = x[a - window + k], vb = x[b - window + k];
        ab += va * vb;
        aa += va * va;
        bb += vb * vb;
    }
    if (aa <= 1e-12 || bb <= 1e-12)
        return -1.0;
    const double levelMatch = std::min (aa, bb) / std::max (aa, bb); // penalise level jumps
    return ab / std::sqrt (aa * bb) * std::sqrt (levelMatch);
}

/** Finds loop points in the sustained middle of the sample and bakes an
    equal-power crossfade so the jump from loopEnd to loopStart is seamless.
    Returns false if the sample is too short to loop. */
bool autoLoop (Audio& audio, int rate, double minSeconds, double xfadeMs,
               std::int64_t& loopStart, std::int64_t& loopEnd, double& score)
{
    const std::vector<float> mono = mixdown (audio);
    const std::size_t n = mono.size();
    const auto minLength = static_cast<std::size_t> (minSeconds * rate);
    const auto xfade = static_cast<std::size_t> (xfadeMs * 0.001 * rate);
    const std::size_t window = std::max<std::size_t> (2048, xfade);
    if (n < minLength + 2 * window + static_cast<std::size_t> (0.6 * rate))
        return false;

    const std::size_t startLow = std::max (static_cast<std::size_t> (0.25 * static_cast<double> (n)), window + xfade);
    const std::size_t startHigh = static_cast<std::size_t> (0.45 * static_cast<double> (n));
    const std::size_t endHigh = static_cast<std::size_t> (0.88 * static_cast<double> (n));
    double best = -2.0;
    std::size_t bestStart = 0, bestEnd = 0;
    // Coarse search on a sparse grid, then refine at single-frame resolution.
    for (std::size_t s = startLow; s <= startHigh; s += std::max<std::size_t> (1, (startHigh - startLow) / 6))
    {
        for (std::size_t e = s + minLength; e <= endHigh; e += 16)
        {
            const double v = similarity (mono, s, e, window, 8);
            if (v > best)
            {
                best = v;
                bestStart = s;
                bestEnd = e;
            }
        }
        if (startHigh == startLow)
            break;
    }
    if (bestEnd == 0)
        return false;
    const std::size_t coarseEnd = bestEnd;
    best = -2.0;
    const std::size_t refineLow = coarseEnd > bestStart + minLength + 24 ? coarseEnd - 24 : coarseEnd;
    for (std::size_t e = refineLow; e <= std::min (endHigh, coarseEnd + 24); ++e)
    {
        const double v = similarity (mono, bestStart, e, window, 1);
        if (v > best)
        {
            best = v;
            bestEnd = e;
        }
    }

    // Bake: the last `xfade` frames before loopEnd blend into the frames before loopStart.
    const std::size_t length = std::min (xfade, bestStart);
    for (auto& channel : audio)
    {
        for (std::size_t k = 0; k < length; ++k)
        {
            const double t = static_cast<double> (k + 1) / static_cast<double> (length);
            const double fadeOut = std::cos (0.5 * 3.14159265358979 * t);
            const double fadeIn = std::sin (0.5 * 3.14159265358979 * t);
            const std::size_t atEnd = bestEnd - length + k, atStart = bestStart - length + k;
            channel[atEnd] = static_cast<float> (channel[atEnd] * fadeOut + channel[atStart] * fadeIn);
        }
        channel.resize (bestEnd);
    }
    loopStart = static_cast<std::int64_t> (bestStart);
    loopEnd = static_cast<std::int64_t> (bestEnd);
    score = best;
    return true;
}

float peakOf (const Audio& audio)
{
    float peak = 0.0f;
    for (const auto& channel : audio)
        for (float v : channel)
            peak = std::max (peak, std::abs (v));
    return peak;
}

std::string formatNumber (double v)
{
    char text[64];
    std::snprintf (text, sizeof text, "%.2f", v);
    return text;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: kraftonic_sample_prep <plan.ini> <output-folder>\n");
        return 2;
    }
    const std::vector<Section> plan = parsePlan (argv[1]);
    const std::filesystem::path outDir (argv[2]);
    std::error_code ec;
    std::filesystem::create_directories (outDir / "samples", ec);
    if (ec)
    {
        std::fprintf (stderr, "cannot create %s\n", outDir.string().c_str());
        return 1;
    }

    const Section* instrument = nullptr;
    for (const auto& s : plan)
        if (s.name == "instrument")
            instrument = &s;
    if (instrument == nullptr)
    {
        std::fprintf (stderr, "plan has no [instrument]\n");
        return 1;
    }

    struct Done
    {
        const Section* zone;
        std::string file;
        std::int64_t loopStart = -1, loopEnd = -1;
        float peak = 0.0f;
        double gainDb = 0.0; // the zone's gain_db (possibly from normalize_rms_db)
    };
    std::vector<Done> done;
    std::map<std::string, std::size_t> written; // sample name -> index in done
    std::size_t diskBytes = 0;
    double totalSeconds = 0.0;
    int failures = 0, loops = 0;
    double worstLoop = 1.0;

    for (const auto& zone : plan)
    {
        if (zone.name != "zone")
            continue;
        const std::string name = zone.get ("sample");
        const std::string source = zone.get ("source");
        if (name.empty() || source.empty())
        {
            std::fprintf (stderr, "zone without sample/source skipped\n");
            ++failures;
            continue;
        }
        if (const auto found = written.find (name); found != written.end())
        {
            Done copy = done[found->second];
            copy.zone = &zone;
            done.push_back (copy);
            continue;
        }
        WavData wav;
        std::string error;
        if (! readWav (source, wav, error))
        {
            std::fprintf (stderr, "%s: %s\n", source.c_str(), error.c_str());
            ++failures;
            continue;
        }
        Audio audio = std::move (wav.audio);
        if (zone.number ("mono", 0.0) > 0.5 && audio.size() == 2)
            audio = { mixdown (audio) };
        trimLeading (audio, wav.sampleRate, zone.number ("trim_db", -60.0));
        if (const std::string balance = zone.get ("balance_db"); ! balance.empty() && audio.size() == 2)
        {
            const auto window = std::min (audio[0].size(), static_cast<std::size_t> (0.5 * wav.sampleRate));
            double l = 0.0, r = 0.0;
            for (std::size_t i = 0; i < window; ++i)
            {
                l += static_cast<double> (audio[0][i]) * audio[0][i];
                r += static_cast<double> (audio[1][i]) * audio[1][i];
            }
            if (l > 1e-12 && r > 1e-12)
            {
                const double correction = std::stod (balance) - 10.0 * std::log10 (l / r);
                const auto gl = static_cast<float> (std::pow (10.0, correction / 40.0));
                for (float& v : audio[0])
                    v *= gl;
                for (float& v : audio[1])
                    v /= gl;
            }
        }

        Done d;
        d.zone = &zone;
        d.file = "samples/" + name;
        bool looped = false;
        if (zone.number ("auto_loop", 0.0) > 0.5)
        {
            double score = 0.0;
            looped = autoLoop (audio, wav.sampleRate, zone.number ("loop_min_seconds", 1.5),
                               zone.number ("loop_xfade_ms", 250.0), d.loopStart, d.loopEnd, score);
            if (looped)
            {
                ++loops;
                worstLoop = std::min (worstLoop, score);
            }
            else
                std::fprintf (stderr, "%s: too short to loop, kept one-shot\n", name.c_str());
        }
        if (! looped)
            truncateWithFade (audio, wav.sampleRate, zone.number ("max_seconds", 0.0), zone.number ("fade_ms", 20.0));
        d.peak = peakOf (audio);
        d.gainDb = zone.number ("gain_db", 0.0);
        if (const std::string target = zone.get ("normalize_rms_db"); ! target.empty())
        {
            const auto from = static_cast<std::size_t> (looped ? d.loopStart : 0);
            const auto to = looped ? static_cast<std::size_t> (d.loopEnd)
                                   : std::min (audio[0].size(), static_cast<std::size_t> (zone.number ("rms_seconds", 2.0) * wav.sampleRate));
            double sum = 0.0;
            for (const auto& channel : audio)
                for (std::size_t i = from; i < to; ++i)
                    sum += static_cast<double> (channel[i]) * channel[i];
            const double rmsValue = std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, (to - from) * audio.size())));
            if (rmsValue > 1e-6)
                d.gainDb = std::stod (target) - 20.0 * std::log10 (rmsValue);
        }

        const auto path = outDir / "samples" / name;
        if (! writeWav (path.string(), audio, wav.sampleRate, WavFormat::pcm16, error, d.loopStart, d.loopEnd))
        {
            std::fprintf (stderr, "%s: %s\n", path.string().c_str(), error.c_str());
            ++failures;
            continue;
        }
        const auto frames = audio[0].size();
        diskBytes += static_cast<std::size_t> (std::filesystem::file_size (path, ec));
        totalSeconds += static_cast<double> (frames) / wav.sampleRate;
        written.emplace (name, done.size());
        done.push_back (d);
    }

    // Zones that were not level-normalized keep their level relative to the
    // attack zones' original recordings (release noises, resonance, pedal).
    if (instrument->number ("normalize_follow", 0.0) > 0.5)
    {
        double shift = 0.0;
        int count = 0;
        for (const auto& d : done)
            if (! d.zone->get ("normalize_rms_db").empty() && d.zone->get ("trigger", "attack") == "attack")
            {
                shift += d.gainDb - d.zone->number ("gain_db", 0.0);
                ++count;
            }
        if (count > 0)
            for (auto& d : done)
                if (d.zone->get ("normalize_rms_db").empty())
                    d.gainDb += shift / count;
    }

    // Gain staging: the loudest attack zone at full velocity peaks at normalize_peak_db.
    double gainDb = instrument->number ("gain_db", 0.0);
    const std::string target = instrument->get ("normalize_peak_db");
    if (! target.empty())
    {
        double loudest = -200.0;
        for (const auto& d : done)
            if (d.zone->get ("trigger", "attack") == "attack" && d.peak > 0.0f)
                loudest = std::max (loudest, 20.0 * std::log10 (d.peak) + d.gainDb);
        if (loudest > -200.0)
            gainDb = std::stod (target) - loudest;
    }

    std::ostringstream manifest;
    manifest << "# Kraftonic sampler instrument (generated by kraftonic_sample_prep; edit the plan instead)\n";
    manifest << "[instrument]\nformat = 1\n";
    for (const auto& [k, v] : instrument->entries)
        if (! isPlanOnly (k) && k != "format" && k != "gain_db")
            manifest << k << " = " << v << "\n";
    manifest << "gain_db = " << formatNumber (gainDb) << "\n";
    for (const auto& d : done)
    {
        manifest << "\n[zone]\nsample = " << d.file << "\n";
        for (const auto& [k, v] : d.zone->entries)
            if (! isPlanOnly (k) && k != "sample" && k != "loop" && k != "gain_db")
                manifest << k << " = " << v << "\n";
        if (d.gainDb != 0.0)
            manifest << "gain_db = " << formatNumber (d.gainDb) << "\n";
        if (d.loopEnd > d.loopStart)
            manifest << "loop = " << d.loopStart << "-" << d.loopEnd << "\n";
    }
    {
        std::ofstream out (outDir / "instrument.ini", std::ios::trunc);
        out << manifest.str();
        out.close();
        if (! out)
        {
            std::fprintf (stderr, "cannot write instrument.ini\n");
            return 1;
        }
    }

    // Verify the result loads exactly as the plug-in will load it.
    LoadReport report;
    const auto bank = SampleBank::load (outDir.string(), report);
    if (! bank)
    {
        std::fprintf (stderr, "generated instrument does not load: %s\n", report.error.c_str());
        return 1;
    }
    std::printf ("%s: %d zones, %d samples, %.1f s audio, disk %.1f MB, RAM %.1f MB, gain %.1f dB, "
                 "%d loops (worst match %.3f), %d failures, %zu warnings\n",
                 outDir.filename().string().c_str(), report.zones, report.samples, totalSeconds,
                 static_cast<double> (diskBytes) / 1.0e6, static_cast<double> (bank->memoryBytes()) / 1.0e6,
                 gainDb, loops, loops > 0 ? worstLoop : 0.0, failures, report.warnings.size());
    for (const auto& w : report.warnings)
        std::fprintf (stderr, "warning: %s\n", w.c_str());
    return failures == 0 ? 0 : 1;
}
