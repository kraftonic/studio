// SPDX-License-Identifier: MIT
#include "kraftonic/synth/Filters.h"
#include "kraftonic/synth/Modulation.h"
#include "kraftonic/synth/Noise.h"
#include "kraftonic/synth/NotePriority.h"
#include "kraftonic/synth/Oscillator.h"

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

using namespace kraftonic::synth;
namespace
{
constexpr double pi = 3.14159265358979323846;

void fft (std::vector<std::complex<double>> &samples)
{
    const int sampleCount = static_cast<int> (samples.size ());
    for (int i = 1, j = 0; i < sampleCount; ++i)
    {
        int bit = sampleCount >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (samples[static_cast<std::size_t> (i)], samples[static_cast<std::size_t> (j)]);
    }
    for (int length = 2; length <= sampleCount; length <<= 1)
    {
        const double angle = -2.0 * pi / length;
        const std::complex<double> root (std::cos (angle), std::sin (angle));
        for (int offset = 0; offset < sampleCount; offset += length)
        {
            std::complex<double> rotation (1.0, 0.0);
            for (int j = 0; j < length / 2; ++j)
            {
                const auto even = samples[static_cast<std::size_t> (offset + j)];
                const auto odd = samples[static_cast<std::size_t> (offset + j + length / 2)] * rotation;
                samples[static_cast<std::size_t> (offset + j)] = even + odd;
                samples[static_cast<std::size_t> (offset + j + length / 2)] = even - odd;
                rotation *= root;
            }
        }
    }
}

template <typename Next> double fftRelativeError (Next &&next, int sampleCount = 4096)
{
    std::vector<std::complex<double>> error (static_cast<std::size_t> (sampleCount));
    std::vector<std::complex<double>> reference (static_cast<std::size_t> (sampleCount));
    for (int i = 0; i < sampleCount; ++i)
    {
        const auto [actual, ideal] = next ();
        const double window = 0.5 - 0.5 * std::cos (2.0 * pi * i / (sampleCount - 1));
        error[static_cast<std::size_t> (i)] = (actual - ideal) * window;
        reference[static_cast<std::size_t> (i)] = ideal * window;
    }
    fft (error);
    fft (reference);
    double errorEnergy = 0.0;
    double referenceEnergy = 0.0;
    for (int i = 1; i < sampleCount / 2; ++i)
    {
        errorEnergy += std::norm (error[static_cast<std::size_t> (i)]);
        referenceEnergy += std::norm (reference[static_cast<std::size_t> (i)]);
    }
    return errorEnergy / referenceEnergy;
}

double sawSeries (double phase, double frequency, double sampleRate)
{
    double value = 0.0;
    const int harmonics = static_cast<int> (sampleRate * 0.5 / frequency);
    for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
        value -= 2.0 / (pi * harmonic) * std::sin (2.0 * pi * harmonic * phase);
    return value;
}

double pulseSeries (double phase, double duty, double frequency, double sampleRate)
{
    double value = 2.0 * duty - 1.0;
    const int harmonics = static_cast<int> (sampleRate * 0.5 / frequency);
    for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
        value += 4.0 / (pi * harmonic) * std::sin (pi * harmonic * duty) *
                 std::cos (2.0 * pi * harmonic * (phase - duty * 0.5));
    return value;
}

double triangleSeries (double phase, double frequency, double sampleRate)
{
    double value = 0.0;
    const int harmonics = static_cast<int> (sampleRate * 0.5 / frequency);
    for (int index = 0; 2 * index + 1 <= harmonics; ++index)
    {
        const int harmonic = 2 * index + 1;
        value -= 8.0 / (pi * pi * harmonic * harmonic) * std::cos (2.0 * pi * harmonic * phase);
    }
    return value;
}

// Share of energy, in dB, that is not at a multiple of `fundamental` (aliasing), up to `bandLimit` Hz.
double offHarmonicDb (const std::vector<double> &signal, double fundamental, double sampleRate, double bandLimit)
{
    const std::size_t count = signal.size ();
    std::vector<std::complex<double>> spectrum (count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const double x = 2.0 * pi * static_cast<double> (i) / static_cast<double> (count);
        const double window =
            0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) - 0.01168 * std::cos (3.0 * x);
        spectrum[i] = signal[i] * window;
    }
    fft (spectrum);
    const double binsPerHarmonic = fundamental * static_cast<double> (count) / sampleRate;
    double harmonicEnergy = 0.0;
    double offEnergy = 0.0;
    for (std::size_t bin = 5; bin < count / 2; ++bin)
    {
        const double binIndex = static_cast<double> (bin);
        if (binIndex * sampleRate / static_cast<double> (count) > bandLimit)
            break;
        const double harmonic = std::round (binIndex / binsPerHarmonic);
        const double energy = std::norm (spectrum[bin]);
        if (harmonic >= 1.0 && std::abs (binIndex - harmonic * binsPerHarmonic) <= 5.0)
            harmonicEnergy += energy;
        else
            offEnergy += energy;
    }
    return 10.0 * std::log10 (offEnergy / (harmonicEnergy + offEnergy));
}

// A slave 7 semitones above `master`, hard-synced to it. Naive renders a plain ramp/pulse
// with a plain phase reset, as a baseline.
std::vector<double> renderSync (Waveform waveform, double master, double sampleRate, bool naive)
{
    constexpr int settle = 4096;
    constexpr int count = 65536;
    const double slave = master * std::exp2 (7.0 / 12.0);
    Oscillator masterOscillator, slaveOscillator;
    masterOscillator.prepare (sampleRate);
    masterOscillator.setFrequency (master);
    slaveOscillator.prepare (sampleRate);
    slaveOscillator.setFrequency (slave);
    const double increment = slave / sampleRate;
    double naivePhase = 0.0;
    std::vector<double> output;
    output.reserve (count);
    for (int i = 0; i < settle + count; ++i)
    {
        masterOscillator.process (Waveform::sine);
        const bool sync = masterOscillator.wrapped ();
        const float fraction = masterOscillator.wrapFraction ();
        double value = 0.0;
        if (naive)
        {
            value = waveform == Waveform::saw ? 2.0 * naivePhase - 1.0 : (naivePhase < 0.25 ? 1.0 : -1.0);
            naivePhase = sync ? increment * (1.0 - fraction) : naivePhase + increment;
            naivePhase -= std::floor (naivePhase);
        }
        else
        {
            value = slaveOscillator.process (waveform, 0.25f, 0.0f, sync, fraction);
        }
        if (i >= settle)
            output.push_back (value);
    }
    return output;
}

double ladderGain (double sampleRate, float cutoff, double frequency, bool compensation = false, float resonance = 0.0f)
{
    LadderFilter filter;
    filter.prepare (sampleRate);
    filter.setCutoff (cutoff);
    filter.setResonance (resonance);
    filter.setGainCompensation (compensation);
    const int count = static_cast<int> (sampleRate * 0.35);
    double sum = 0.0;
    int measured = 0;
    for (int i = 0; i < count; ++i)
    {
        const double input = 0.01 * std::sin (2.0 * pi * frequency * i / sampleRate);
        const double output = filter.process (static_cast<float> (input));
        if (i > count / 2)
        {
            sum += output * output;
            ++measured;
        }
    }
    return std::sqrt (sum / measured) / (0.01 / std::sqrt (2.0));
}

double svfGain (SVFMode mode, double sampleRate, float cutoff, double frequency, bool twelveDb = true)
{
    StateVariableFilter filter;
    filter.prepare (sampleRate);
    filter.setCutoff (cutoff);
    filter.setResonance (0.0f);
    const int count = static_cast<int> (sampleRate * 0.12);
    double sum = 0.0;
    int measured = 0;
    for (int i = 0; i < count; ++i)
    {
        const float input = static_cast<float> (std::sin (2.0 * pi * frequency * i / sampleRate));
        const float output = filter.process (input, mode, twelveDb);
        if (i > count / 2)
        {
            sum += static_cast<double> (output) * output;
            ++measured;
        }
    }
    return std::sqrt (sum / measured) / (1.0 / std::sqrt (2.0));
}
} // namespace

TEST_CASE ("oscillator reports phase wrap and fractional sample position")
{
    Oscillator oscillator;
    oscillator.prepare (44100.0);
    oscillator.setFrequency (441.0);
    int wraps = 0;
    float reportedFraction = 0.0f;
    for (int i = 0; i < 1000; ++i)
    {
        oscillator.process (Waveform::sine);
        if (oscillator.wrapped ())
        {
            ++wraps;
            reportedFraction = oscillator.wrapFraction ();
            CHECK (reportedFraction >= 0.0f);
            CHECK (reportedFraction <= 1.0f);
        }
    }
    CHECK (wraps == 10);
    CHECK (reportedFraction == Catch::Approx (1.0f).margin (0.0001f));
}

TEST_CASE ("saw, pulse, triangle and hard sync reduce alias error")
{
    constexpr double sampleRate = 44100.0;
    for (const double frequency : {2500.0, 2093.0})
    {
        Oscillator oscillator;
        oscillator.prepare (sampleRate);
        oscillator.setFrequency (frequency);
        double phase = 0.0;
        const double bandlimitedError = fftRelativeError (
            [&]
            {
                const double output = oscillator.process (Waveform::saw);
                const double ideal = sawSeries (phase, frequency, sampleRate);
                phase += frequency / sampleRate;
                if (phase >= 1.0)
                    phase -= 1.0;
                return std::pair{output, ideal};
            });
        phase = 0.0;
        const double naiveError = fftRelativeError (
            [&]
            {
                const double output = 2.0 * phase - 1.0;
                const double ideal = sawSeries (phase, frequency, sampleRate);
                phase += frequency / sampleRate;
                if (phase >= 1.0)
                    phase -= 1.0;
                return std::pair{output, ideal};
            });
        const double reductionDb = 10.0 * std::log10 (naiveError / bandlimitedError);
        INFO ("saw frequency=" << frequency << " Hz, FFT alias error reduction=" << reductionDb << " dB");
        CHECK (reductionDb >= (frequency > 2200.0 ? 20.0 : 12.0));
    }

    for (const Waveform waveform : {Waveform::pulse, Waveform::triangle})
    {
        Oscillator oscillator;
        oscillator.prepare (sampleRate);
        oscillator.setFrequency (2500.0);
        double phase = 0.0;
        const auto reference = [&] (double p)
        {
            return waveform == Waveform::pulse ? pulseSeries (p, 0.25, 2500.0, sampleRate)
                                               : triangleSeries (p, 2500.0, sampleRate);
        };
        const double bandlimitedError = fftRelativeError (
            [&]
            {
                const double output = oscillator.process (waveform, 0.25f);
                const double ideal = reference (phase);
                phase += 2500.0 / sampleRate;
                if (phase >= 1.0)
                    phase -= 1.0;
                return std::pair{output, ideal};
            });
        phase = 0.0;
        const double naiveError = fftRelativeError (
            [&]
            {
                const double output = waveform == Waveform::pulse
                                          ? (phase < 0.25 ? 1.0 : -1.0)
                                          : (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
                const double ideal = reference (phase);
                phase += 2500.0 / sampleRate;
                if (phase >= 1.0)
                    phase -= 1.0;
                return std::pair{output, ideal};
            });
        const double reductionDb = 10.0 * std::log10 (naiveError / bandlimitedError);
        INFO ("waveform=" << static_cast<int> (waveform) << ", FFT alias error reduction=" << reductionDb << " dB");
        CHECK (reductionDb >= 20.0);
    }

    constexpr int syncSamples = 100;
    constexpr int oversample = 64;
    constexpr int oversampledPeriod = syncSamples * oversample;
    constexpr int retainedHarmonics = 49;
    std::array<std::complex<double>, 2 * retainedHarmonics + 1> coefficients{};
    for (int harmonic = -retainedHarmonics; harmonic <= retainedHarmonics; ++harmonic)
    {
        std::complex<double> coefficient{};
        for (int sample = 0; sample < oversampledPeriod; ++sample)
        {
            const double timeSamples = static_cast<double> (sample) / oversample;
            const double syncPeriodSamples = sampleRate / 441.0;
            const double timeWithinSyncPeriod = std::fmod (timeSamples, syncPeriodSamples);
            const double phase = std::fmod (1950.0 * timeWithinSyncPeriod / sampleRate, 1.0);
            const double value = 2.0 * phase - 1.0;
            coefficient += value * std::polar (1.0, -2.0 * pi * harmonic * timeSamples / syncSamples);
        }
        coefficients[static_cast<std::size_t> (harmonic + retainedHarmonics)] =
            coefficient / static_cast<double> (oversampledPeriod);
    }
    std::array<double, syncSamples> idealSync{};
    for (int sample = 0; sample < syncSamples; ++sample)
    {
        std::complex<double> value{};
        for (int harmonic = -retainedHarmonics; harmonic <= retainedHarmonics; ++harmonic)
            value += coefficients[static_cast<std::size_t> (harmonic + retainedHarmonics)] *
                     std::polar (1.0, 2.0 * pi * harmonic * sample / syncSamples);
        idealSync[static_cast<std::size_t> (sample)] = value.real ();
    }
    Oscillator master, synced;
    master.prepare (sampleRate);
    master.setFrequency (441.0);
    synced.prepare (sampleRate);
    synced.setFrequency (1950.0);
    // Run one master period first so the captured period starts just after a real sync,
    // as the periodic reference does.
    for (int sample = 0; sample < syncSamples; ++sample)
    {
        master.process (Waveform::sine);
        synced.process (Waveform::saw, 0.5f, 0.0f, master.wrapped (), master.wrapFraction ());
    }
    std::array<double, syncSamples> correctedSync{};
    for (int sample = 0; sample < syncSamples; ++sample)
    {
        master.process (Waveform::sine);
        correctedSync[static_cast<std::size_t> (sample)] =
            synced.process (Waveform::saw, 0.5f, 0.0f, master.wrapped (), master.wrapFraction ());
    }
    Oscillator naiveMaster;
    naiveMaster.prepare (sampleRate);
    naiveMaster.setFrequency (441.0);
    double naiveSlavePhase = 0.0;
    const double slaveIncrement = 1950.0 / sampleRate;
    std::array<double, syncSamples> naiveSync{};
    for (int sample = 0; sample < 2 * syncSamples; ++sample)
    {
        naiveMaster.process (Waveform::sine);
        if (sample >= syncSamples)
            naiveSync[static_cast<std::size_t> (sample - syncSamples)] = 2.0 * naiveSlavePhase - 1.0;
        if (naiveMaster.wrapped ())
            naiveSlavePhase = slaveIncrement * (1.0 - naiveMaster.wrapFraction ());
        else
            naiveSlavePhase += slaveIncrement;
        if (naiveSlavePhase >= 1.0)
            naiveSlavePhase -= 1.0;
    }
    auto spectrumError = [&] (const std::array<double, syncSamples> &signal)
    {
        double errorEnergy = 0.0;
        double referenceEnergy = 0.0;
        for (int bin = 0; bin < syncSamples; ++bin)
        {
            std::complex<double> errorBin{}, referenceBin{};
            for (int sample = 0; sample < syncSamples; ++sample)
            {
                const double angle = -2.0 * pi * bin * sample / syncSamples;
                errorBin += (signal[static_cast<std::size_t> (sample)] - idealSync[static_cast<std::size_t> (sample)]) *
                            std::polar (1.0, angle);
                referenceBin += idealSync[static_cast<std::size_t> (sample)] * std::polar (1.0, angle);
            }
            errorEnergy += std::norm (errorBin);
            referenceEnergy += std::norm (referenceBin);
        }
        return errorEnergy / referenceEnergy;
    };
    const double correctedSyncError = spectrumError (correctedSync);
    const double naiveSyncError = spectrumError (naiveSync);
    const double syncReductionDb = 10.0 * std::log10 (naiveSyncError / correctedSyncError);
    INFO ("hard-sync FFT error reduction=" << syncReductionDb << " dB; corrected=" << correctedSyncError
                                           << ", naive=" << naiveSyncError);
    // This total-error metric also counts PolyBLEP's in-band roll-off: a free-running PolyBLEP
    // saw scores about 9.9 dB here. The 12 dB sync target is checked on alias energy in
    // "hard-synced saw and pulse alias like a free-running PolyBLEP oscillator". This guards regressions.
    CHECK (syncReductionDb >= 6.0);

    Oscillator voiceA, voiceB;
    voiceA.prepare (sampleRate);
    voiceB.prepare (sampleRate);
    voiceA.setFrequency (440.0);
    voiceB.setFrequency (440.0);
    voiceA.setDriftCents (30.0f);
    voiceB.setDriftCents (30.0f);
    voiceA.reset ();
    voiceB.reset ();
    double driftDifference = 0.0;
    for (int i = 0; i < 44100; ++i)
        driftDifference += std::abs (voiceA.process (Waveform::sine) - voiceB.process (Waveform::sine));
    INFO ("distinct voice drift accumulated difference=" << driftDifference);
    CHECK (driftDifference > 0.01);
}

TEST_CASE ("hard-synced saw and pulse alias like a free-running PolyBLEP oscillator")
{
    // 48 kHz, no oversampling, slave = master + 7 semitones, pulse width 0.25. Off-harmonic
    // energy is measured in the audible band (to 20 kHz) and over the full band (to 24 kHz).
    constexpr double sampleRate = 48000.0;
    struct Case
    {
        Waveform waveform;
        double master;
        double audibleCeilingDb;
    };
    for (const Case test : {Case{Waveform::saw, 880.0, -35.0}, Case{Waveform::saw, 2637.02, -28.0},
                            Case{Waveform::pulse, 880.0, -33.0}, Case{Waveform::pulse, 2637.02, -28.0}})
    {
        const auto corrected = renderSync (test.waveform, test.master, sampleRate, false);
        const auto naive = renderSync (test.waveform, test.master, sampleRate, true);
        const double audible = offHarmonicDb (corrected, test.master, sampleRate, 20000.0);
        const double audibleNaive = offHarmonicDb (naive, test.master, sampleRate, 20000.0);
        const double full = offHarmonicDb (corrected, test.master, sampleRate, sampleRate);
        const double fullNaive = offHarmonicDb (naive, test.master, sampleRate, sampleRate);
        INFO ((test.waveform == Waveform::saw ? "saw" : "pulse")
              << " master=" << test.master << " Hz: off-harmonic to 20 kHz " << audible << " dB (naive "
              << audibleNaive << "), to 24 kHz " << full << " dB (naive " << fullNaive << ")");
        CHECK (audible <= test.audibleCeilingDb);
        CHECK (audibleNaive - audible >= 12.0);
        CHECK (fullNaive - full >= 12.0);
    }
}

TEST_CASE ("hard sync stays bounded for edge-case fractions, widths and phase modulation")
{
    Oscillator oscillator;
    oscillator.prepare (48000.0);
    oscillator.setFrequency (9000.0);
    std::uint32_t random = 0x2468aceu;
    float peak = 0.0f;
    for (int i = 0; i < 20000; ++i)
    {
        random = random * 1664525u + 1013904223u;
        const bool sync = (random >> 28) < 5u;
        const float fraction =
            (i % 3 == 0) ? static_cast<float> (i % 2) : static_cast<float> (random >> 8) / 16777216.0f;
        const float width = (i / 1000) % 2 == 0 ? 0.02f : 0.98f;
        const float phaseMod = 0.3f * std::sin (0.01f * static_cast<float> (i));
        for (const Waveform waveform : {Waveform::saw, Waveform::pulse})
        {
            Oscillator copy = oscillator;
            const float value = copy.process (waveform, width, phaseMod, sync, fraction);
            REQUIRE (std::isfinite (value));
            peak = std::max (peak, std::abs (value));
        }
        oscillator.process (Waveform::saw, width, phaseMod, sync, fraction);
    }
    INFO ("peak=" << peak);
    // A free-running oscillator peaks at about 1.05 with these widths and phase modulation.
    CHECK (peak < 1.25f);
}

TEST_CASE ("ladder filter has a fourth-order slope across rates and cutoffs")
{
    for (const double sampleRate : {44100.0, 96000.0, 192000.0})
    {
        for (const float cutoff : {100.0f, 200.0f})
        {
            const double oneOctave = ladderGain (sampleRate, cutoff, cutoff * 4.0);
            const double twoOctaves = ladderGain (sampleRate, cutoff, cutoff * 8.0);
            const double threeOctaves = ladderGain (sampleRate, cutoff, cutoff * 16.0);
            const double slopeOne = 20.0 * std::log10 (twoOctaves / oneOctave);
            const double slopeTwo = 20.0 * std::log10 (threeOctaves / twoOctaves);
            INFO ("sampleRate=" << sampleRate << ", cutoff=" << cutoff << ", slope1=" << slopeOne
                                << " dB/oct, slope2=" << slopeTwo << " dB/oct");
            CHECK (slopeOne == Catch::Approx (-24.0).margin (1.5));
            CHECK (slopeTwo == Catch::Approx (-24.0).margin (1.5));
        }
    }
}

TEST_CASE ("ladder and SVF cutoff smoothing time matches across sample rates")
{
    for (const double sampleRate : {44100.0, 192000.0})
    {
        LadderFilter ladder;
        ladder.prepare (sampleRate);
        ladder.setCutoffImmediate (500.0f);
        CHECK (ladder.cutoffHz () == 500.0f);
        ladder.setCutoff (8000.0f);
        int ladder10 = -1, ladder90 = -1;
        for (int i = 0; i < static_cast<int> (sampleRate * 0.01); ++i)
        {
            ladder.process (0.0f);
            const float fraction = (ladder.cutoffHz () - 500.0f) / 7500.0f;
            if (ladder10 < 0 && fraction >= 0.1f)
                ladder10 = i;
            if (ladder90 < 0 && fraction >= 0.9f)
            {
                ladder90 = i;
                break;
            }
        }
        const double ladderResponse = static_cast<double> (ladder90 - ladder10) / sampleRate;
        INFO ("ladder sample rate=" << sampleRate << ", 10-90 time=" << ladderResponse * 1000.0 << " ms");
        CHECK (ladderResponse == Catch::Approx (0.002197).margin (0.00008));

        StateVariableFilter svf;
        svf.prepare (sampleRate);
        svf.setCutoffImmediate (500.0f);
        CHECK (svf.cutoffHz () == 500.0f);
        svf.setCutoff (8000.0f);
        int svf10 = -1, svf90 = -1;
        for (int i = 0; i < static_cast<int> (sampleRate * 0.01); ++i)
        {
            float low, high, band, notch;
            svf.process (0.0f, low, high, band, notch);
            const float fraction = (svf.cutoffHz () - 500.0f) / 7500.0f;
            if (svf10 < 0 && fraction >= 0.1f)
                svf10 = i;
            if (svf90 < 0 && fraction >= 0.9f)
            {
                svf90 = i;
                break;
            }
        }
        const double svfResponse = static_cast<double> (svf90 - svf10) / sampleRate;
        INFO ("SVF sample rate=" << sampleRate << ", 10-90 time=" << svfResponse * 1000.0 << " ms");
        CHECK (svfResponse == Catch::Approx (0.002197).margin (0.00008));

        ladder.setSmoothingTime (0.0f);
        ladder.setCutoff (7000.0f);
        ladder.process (0.0f);
        CHECK (ladder.cutoffHz () == 7000.0f);
        svf.setSmoothingTime (0.0f);
        svf.setCutoff (7000.0f);
        float low, high, band, notch;
        svf.process (0.0f, low, high, band, notch);
        CHECK (svf.cutoffHz () == 7000.0f);
    }
}

TEST_CASE ("ladder self oscillates at cutoff and passband compensation is controlled")
{
    for (const double sampleRate : {48000.0, 192000.0})
    {
        for (const float cutoff : {65.41f, 100.0f, 500.0f, 1200.0f, 4000.0f, 8000.0f})
        {
            // Zero input from reset: the oscillation must start by itself and sustain.
            LadderFilter filter;
            filter.prepare (sampleRate);
            filter.setCutoffImmediate (cutoff);
            filter.setResonance (1.0f);
            const int totalSamples = static_cast<int> (sampleRate * 3.0);
            float earlyPeak = 0.0f;
            float latePeak = 0.0f;
            float previous = 0.0f;
            double firstCrossing = -1.0;
            double lastCrossing = -1.0;
            int cycles = 0;
            for (int i = 0; i < totalSamples; ++i)
            {
                const float output = filter.process (0.0f);
                const double time = static_cast<double> (i) / sampleRate;
                if (time >= 0.5 && time < 1.0)
                    earlyPeak = std::max (earlyPeak, std::abs (output));
                if (time >= 2.5)
                    latePeak = std::max (latePeak, std::abs (output));
                if (time >= 1.0 && previous < 0.0f && output >= 0.0f)
                {
                    const double crossing = static_cast<double> (i - 1) + previous / (previous - output);
                    if (firstCrossing < 0.0)
                        firstCrossing = crossing;
                    else
                        ++cycles;
                    lastCrossing = crossing;
                }
                previous = output;
            }
            REQUIRE (cycles >= 100);
            const double frequency = sampleRate * cycles / (lastCrossing - firstCrossing);
            const double sustainDb = 20.0 * std::log10 (latePeak / earlyPeak);
            INFO ("sampleRate=" << sampleRate << ", cutoff=" << cutoff << ", self oscillation=" << frequency
                                << " Hz, peak 0.5-1 s=" << earlyPeak << ", 2.5-3 s=" << latePeak);
            CHECK (frequency == Catch::Approx (cutoff).margin (cutoff * 0.03));
            CHECK (std::abs (sustainDb) <= 3.0);
            CHECK (latePeak >= 0.2f);
            CHECK (latePeak <= 0.5f);
        }
    }

    // Full-scale input at full resonance and drive stays bounded.
    for (const float drive : {1.0f, 16.0f})
    {
        LadderFilter filter;
        filter.prepare (48000.0);
        filter.setCutoffImmediate (300.0f);
        filter.setResonance (1.0f);
        filter.setDrive (drive);
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            const float input = (i / 80) % 2 == 0 ? 1.0f : -1.0f;
            const float output = filter.process (i < 24000 ? input : std::sin (0.05f * static_cast<float> (i)));
            REQUIRE (std::isfinite (output));
            peak = std::max (peak, std::abs (output));
        }
        INFO ("drive=" << drive << ", full-scale peak=" << peak);
        CHECK (peak < 1.5f);
    }

    const double uncompensated = ladderGain (48000.0, 2000.0f, 100.0, false, 0.8f);
    const double compensated = ladderGain (48000.0, 2000.0f, 100.0, true, 0.8f);
    CHECK (compensated > uncompensated);
}

TEST_CASE ("SVF outputs have expected low, high, band and notch responses")
{
    constexpr double sampleRate = 48000.0;
    constexpr float cutoff = 1000.0f;
    const double lpLow = svfGain (SVFMode::lowPass, sampleRate, cutoff, cutoff * 0.5);
    const double lpAt = svfGain (SVFMode::lowPass, sampleRate, cutoff, cutoff);
    const double lpHigh = svfGain (SVFMode::lowPass, sampleRate, cutoff, cutoff * 2.0);
    const double hpLow = svfGain (SVFMode::highPass, sampleRate, cutoff, cutoff * 0.5);
    const double hpAt = svfGain (SVFMode::highPass, sampleRate, cutoff, cutoff);
    const double hpHigh = svfGain (SVFMode::highPass, sampleRate, cutoff, cutoff * 2.0);
    const double bpLow = svfGain (SVFMode::bandPass, sampleRate, cutoff, cutoff * 0.5);
    const double bpAt = svfGain (SVFMode::bandPass, sampleRate, cutoff, cutoff);
    const double bpHigh = svfGain (SVFMode::bandPass, sampleRate, cutoff, cutoff * 2.0);
    const double notchLow = svfGain (SVFMode::notch, sampleRate, cutoff, cutoff * 0.5);
    const double notchAt = svfGain (SVFMode::notch, sampleRate, cutoff, cutoff);
    const double notchHigh = svfGain (SVFMode::notch, sampleRate, cutoff, cutoff * 2.0);

    INFO ("SVF LP low/at/high=" << lpLow << "/" << lpAt << "/" << lpHigh << "; HP=" << hpLow << "/" << hpAt << "/"
                                << hpHigh << "; BP=" << bpLow << "/" << bpAt << "/" << bpHigh << "; notch=" << notchLow
                                << "/" << notchAt << "/" << notchHigh);
    CHECK (lpLow > lpAt);
    CHECK (lpAt > lpHigh);
    CHECK (hpLow < hpAt);
    CHECK (hpAt < hpHigh);
    CHECK (bpAt > bpLow);
    CHECK (bpAt > bpHigh);
    CHECK (notchAt < notchLow);
    CHECK (notchAt < notchHigh);

    const double lpTwoOctave = svfGain (SVFMode::lowPass, sampleRate, cutoff, cutoff * 4.0);
    const double lpFourOctave = svfGain (SVFMode::lowPass, sampleRate, cutoff, cutoff * 8.0);
    const double lpSlope = 20.0 * std::log10 (lpFourOctave / lpTwoOctave);
    INFO ("SVF 12 dB LP two-octave slope=" << lpSlope << " dB/octave");
    CHECK (lpSlope == Catch::Approx (-12.0).margin (2.0));
    const double lp24At2 = svfGain (SVFMode::lowPass, sampleRate, cutoff, cutoff * 4.0, false);
    const double lp24At4 = svfGain (SVFMode::lowPass, sampleRate, cutoff, cutoff * 8.0, false);
    const double lp24Slope = 20.0 * std::log10 (lp24At4 / lp24At2);
    INFO ("SVF 24 dB LP slope=" << lp24Slope << " dB/octave");
    CHECK (lp24Slope == Catch::Approx (-24.0).margin (3.0));
}

TEST_CASE ("LFO frequency and sample-and-hold steps are measured")
{
    Lfo lfo;
    lfo.prepare (1000.0);
    lfo.setRateHz (10.0f);
    int crossings = 0;
    float previous = lfo.process (LfoWave::sawUp);
    for (int i = 0; i < 1000; ++i)
    {
        const float current = lfo.process (LfoWave::sawUp);
        if (current < previous)
            ++crossings;
        previous = current;
    }
    CHECK (crossings == 10);

    lfo.reset ();
    lfo.setRateHz (10.0f);
    std::array<float, 300> held{};
    for (auto &value : held)
        value = lfo.process (LfoWave::sampleHold);
    int changes = 0;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        const std::size_t start = static_cast<std::size_t> (cycle * 100);
        for (std::size_t offset = 1; offset < 99; ++offset)
            CHECK (held[start + offset] == held[start]);
        if (cycle < 2)
        {
            CHECK (held[start + 99] != held[start]);
            ++changes;
        }
    }
    CHECK (changes == 2);
}

TEST_CASE ("glide, note priority and noise retain their basic contracts")
{
    Glide glide;
    glide.prepare (1000.0);
    glide.reset (0.0f);
    glide.setTime (0.1f);
    glide.setTarget (1.0f);
    for (int i = 0; i < 100; ++i)
        glide.process ();
    CHECK (glide.value () == Catch::Approx (1.0f).margin (0.0001f));

    NotePriority<4> priority;
    priority.setPriority (Priority::low);
    CHECK (priority.noteOn (64).note == 64);
    CHECK (priority.noteOn (60).note == 60);
    CHECK (priority.noteOff (60).note == 64);

    Noise noise;
    noise.reset ();
    for (int i = 0; i < 1000; ++i)
        CHECK (std::isfinite (noise.pink ()));
}

TEST_CASE ("oscillator, ladder, SVF and LFO do not depend on block splits")
{
    constexpr int count = 512;
    std::array<float, count> input{};
    for (int i = 0; i < count; ++i)
        input[static_cast<std::size_t> (i)] = 0.2f * std::sin (0.071f * static_cast<float> (i));
    std::array<float, count> whole{}, split{};

    Oscillator oscillatorA, oscillatorB;
    oscillatorA.prepare (48000.0);
    oscillatorB.prepare (48000.0);
    oscillatorA.setFrequency (440.0);
    oscillatorB.setFrequency (440.0);
    LadderFilter ladderA, ladderB;
    ladderA.prepare (48000.0);
    ladderB.prepare (48000.0);
    ladderA.setCutoff (2400.0f);
    ladderB.setCutoff (2400.0f);
    Lfo lfoA, lfoB;
    lfoA.prepare (48000.0);
    lfoB.prepare (48000.0);
    lfoA.setRateHz (3.2f);
    lfoB.setRateHz (3.2f);
    for (int i = 0; i < count; ++i)
        whole[static_cast<std::size_t> (i)] =
            ladderA.process (input[static_cast<std::size_t> (i)] + 0.1f * oscillatorA.process (Waveform::saw) +
                             0.1f * lfoA.process (LfoWave::triangle));

    std::uint32_t random = 0x1234567u;
    int offset = 0;
    while (offset < count)
    {
        random = random * 1664525u + 1013904223u;
        const int blockSize = std::min (1 + static_cast<int> (random % 37u), count - offset);
        for (int j = 0; j < blockSize; ++j)
        {
            const int i = offset + j;
            split[static_cast<std::size_t> (i)] =
                ladderB.process (input[static_cast<std::size_t> (i)] + 0.1f * oscillatorB.process (Waveform::saw) +
                                 0.1f * lfoB.process (LfoWave::triangle));
        }
        offset += blockSize;
    }
    for (int i = 0; i < count; ++i)
        CHECK (split[static_cast<std::size_t> (i)] == whole[static_cast<std::size_t> (i)]);
}

TEST_CASE ("synth classes recover from non-finite values and flush denormals")
{
    const float nan = std::numeric_limits<float>::quiet_NaN ();
    const float infinity = std::numeric_limits<float>::infinity ();
    Oscillator oscillator;
    oscillator.prepare (48000.0);
    oscillator.setFrequency (nan);
    CHECK (std::isfinite (oscillator.process (Waveform::pulse, nan, nan)));
    oscillator.setFrequency (440.0);
    CHECK (std::isfinite (oscillator.process (Waveform::saw)));

    OnePoleLowPass low;
    low.prepare (48000.0);
    low.setCutoff (infinity);
    CHECK (std::isfinite (low.process (nan)));
    CHECK (std::isfinite (low.process (0.25f)));
    OnePoleHighPass high;
    high.prepare (48000.0);
    high.setCutoff (1000.0f);
    CHECK (std::isfinite (high.process (infinity)));
    CHECK (std::isfinite (high.process (0.25f)));

    StateVariableFilter svf;
    svf.prepare (48000.0);
    svf.setCutoff (nan);
    CHECK (std::isfinite (svf.process (infinity, SVFMode::lowPass)));
    CHECK (std::isfinite (svf.process (0.25f, SVFMode::lowPass)));
    LadderFilter ladder;
    ladder.prepare (48000.0);
    ladder.setCutoff (nan);
    ladder.setResonance (infinity);
    ladder.setDrive (nan);
    CHECK (std::isfinite (ladder.process (nan)));
    CHECK (std::isfinite (ladder.process (0.25f)));

    Lfo lfo;
    lfo.prepare (48000.0);
    lfo.setRateHz (infinity);
    CHECK (std::isfinite (lfo.process (LfoWave::sampleHold)));
    lfo.setRateHz (1.0f);
    CHECK (std::isfinite (lfo.process (LfoWave::sine)));
    Glide glide;
    glide.prepare (48000.0);
    glide.setTarget (nan);
    CHECK (std::isfinite (glide.process ()));
    glide.setTarget (1.0f);
    CHECK (std::isfinite (glide.process ()));
    Noise noise;
    for (int i = 0; i < 100; ++i)
        CHECK (std::isfinite (noise.white () + noise.pink ()));
    NotePriority<> notes;
    CHECK (notes.noteOn (60).note == 60);
    CHECK (notes.noteOff (60).note == -1);

    OnePoleLowPass denormalFilter;
    denormalFilter.prepare (48000.0);
    float sample = std::numeric_limits<float>::denorm_min ();
    for (int i = 0; i < 10000; ++i)
        sample = denormalFilter.process (sample);
    CHECK (sample == 0.0f);
    OnePoleHighPass denormalHighPass;
    denormalHighPass.prepare (48000.0);
    CHECK (denormalHighPass.process (std::numeric_limits<float>::denorm_min ()) == 0.0f);
    StateVariableFilter denormalSvf;
    denormalSvf.prepare (48000.0);
    CHECK (denormalSvf.process (std::numeric_limits<float>::denorm_min (), SVFMode::lowPass) == 0.0f);
    LadderFilter denormalLadder;
    denormalLadder.prepare (48000.0);
    CHECK (denormalLadder.process (std::numeric_limits<float>::denorm_min ()) == 0.0f);
    Glide denormalGlide;
    denormalGlide.reset (std::numeric_limits<float>::denorm_min ());
    CHECK (denormalGlide.process () == 0.0f);
}
