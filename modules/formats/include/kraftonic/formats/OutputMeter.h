// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — output-level meter feed.
//
//  SDK-free peak accumulator the shared VST3 base uses to drive panel
//  meters: the processor observes each rendered block's stereo peaks and
//  the feed releases one stereo pair at ~30 Hz for delivery to the
//  controller (as read-only output parameters; see vst3/AdapterIds.h and
//  the meter section of modules/formats/README.md).
//
//  Real-time safe: only floats, ints and max() — no allocation, no locks,
//  no I/O, no exceptions. The UI side polls the controller's cached levels
//  on a ~30 Hz timer and drives KraftLedMeter::setLevel/setPeak (with its
//  own peak-hold/decay), so nothing here touches the UI toolkit.
// ============================================================================

namespace kraftonic::formats
{

class OutputMeterFeed
{
public:
    /** Tick rate target in Hz (30: smooth meters, cheap automation). */
    static constexpr double kTickRate = 30.0;

    /** Sets the emit interval from the sample rate. Not real-time. */
    void setSampleRate (double sampleRate) noexcept
    {
        if (sampleRate < 1.0)
            sampleRate = 1.0;
        interval_ = static_cast<int> (sampleRate / kTickRate + 0.5);
        if (interval_ < 1)
            interval_ = 1;
        reset();
    }

    void reset() noexcept
    {
        accumulatedL_ = 0.0F;
        accumulatedR_ = 0.0F;
        sinceEmit_ = 0;
    }

    /**
     * Folds one rendered block's peaks in (linear amplitude, >= 0).
     * Silence/bypass blocks feed zeros so held meters fall.
     */
    void observe (float peakL, float peakR, int numSamples) noexcept
    {
        if (peakL > accumulatedL_)
            accumulatedL_ = peakL;
        if (peakR > accumulatedR_)
            accumulatedR_ = peakR;
        sinceEmit_ += numSamples < 0 ? 0 : numSamples;
    }

    /** True once a tick's worth of samples has accumulated. */
    [[nodiscard]] bool ready() const noexcept { return sinceEmit_ >= interval_; }

    /** Takes the accumulated peaks (0..1 clamped) and restarts the tick. */
    void take (float& peakL, float& peakR) noexcept
    {
        peakL = accumulatedL_ > 1.0F ? 1.0F : accumulatedL_;
        peakR = accumulatedR_ > 1.0F ? 1.0F : accumulatedR_;
        accumulatedL_ = 0.0F;
        accumulatedR_ = 0.0F;
        sinceEmit_ = 0;
    }

    int intervalSamples() const noexcept { return interval_; }

private:
    int interval_ = 1600; // samples per tick at 48 kHz before setSampleRate
    float accumulatedL_ = 0.0F;
    float accumulatedR_ = 0.0F;
    int sinceEmit_ = 0;
};

} // namespace kraftonic::formats
