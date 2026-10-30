// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — shared VST3 processor.
//
//  `Processor<Traits>` turns host audio/note/automation data into the
//  SDK-free engine contract for any product: MIDI/event translation to
//  core::Event, sample-accurate parameter changes, a preallocated sorted
//  event array, sustain CC64 via IMidiMapping onto a hidden parameter,
//  all-notes/sound-off, bypass, and state get/set through the shared codec.
//
//  The audio thread never allocates, locks, does I/O, logs, or throws;
//  everything is preallocated in setupProcessing() and merged into a fixed
//  event array per block. Products use this class directly (its
//  createInstance/cid satisfy the factory); subclass only to add behaviour.
// ============================================================================

#include "kraftonic/core/Instrument.h"
#include "kraftonic/formats/EventHelpers.h"
#include "kraftonic/formats/HeldNotes.h"
#include "kraftonic/formats/OutputMeter.h"
#include "kraftonic/formats/Preset.h"
#include "kraftonic/formats/StateCodec.h"
#include "kraftonic/formats/UiNoteQueue.h"
#include "kraftonic/formats/vst3/AdapterIds.h"
#include "kraftonic/formats/vst3/Vst3SdkIncludes.h"
#include "kraftonic/formats/vst3/Vst3Traits.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace kraftonic::formats::vst3
{

template <PluginTraits Traits>
class Processor : public Steinberg::Vst::AudioEffect
{
public:
    using ParamId = typename Traits::ParamId;
    using Params = typename Traits::Params;
    using Engine = typename Traits::Engine;

    Processor()
    {
        currentParams_ = Params::defaults();
        const Steinberg::FUID controllerId (Traits::kControllerFuid[0],
                                            Traits::kControllerFuid[1],
                                            Traits::kControllerFuid[2],
                                            Traits::kControllerFuid[3]);
        setControllerClass (controllerId);
    }

    ~Processor() override = default;

    static Steinberg::FUnknown* createInstance (void* /*context*/)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor());
    }

    static Steinberg::FUID cid;

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE
    {
        const Steinberg::tresult result = AudioEffect::initialize (context);
        if (result != Steinberg::kResultOk)
            return result;
        // No audio input: this is an instrument. One stereo output, one MIDI input.
        addAudioOutput (STR16 ("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);
        addEventInput (STR16 ("MIDI In"), 1);
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        return AudioEffect::terminate();
    }

    Steinberg::tresult PLUGIN_API setBusArrangements (
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE
    {
        if (numIns == 0 && numOuts == 1
            && outputs[0] == Steinberg::Vst::SpeakerArr::kStereo)
        {
            return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
        }
        return Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool /*state*/) SMTG_OVERRIDE
    {
        return AudioEffect::setActive (false);
    }

    Steinberg::tresult PLUGIN_API setupProcessing (
        Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE
    {
        const Steinberg::tresult result = AudioEffect::setupProcessing (newSetup);
        if (result != Steinberg::kResultOk)
            return result;

        double rate = newSetup.sampleRate;
        if (rate < 22050.0)
            rate = 22050.0;
        if (rate > 384000.0)
            rate = 384000.0;
        sampleRate_ = rate;

        // setupProcessing runs off the audio thread, so allocating here is fine.
        Steinberg::int32 maxBlock = newSetup.maxSamplesPerBlock;
        if (maxBlock < 1)
            maxBlock = 1;
        if (maxBlock > 65536)
            maxBlock = 65536;
        scratchLeft_.assign (static_cast<std::size_t> (maxBlock), 0.0F);
        scratchRight_.assign (static_cast<std::size_t> (maxBlock), 0.0F);

        core::ProcessSetup setup {};
        setup.sampleRate = sampleRate_;
        setup.maxBlockSize = static_cast<int> (maxBlock);
        engine_.prepare (setup);
        engineReady_ = true;
        meterFeed_.setSampleRate (sampleRate_);
        heldFeed_.setSampleRate (sampleRate_);
        heldNotes_.clear();
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API setProcessing (Steinberg::TBool state) SMTG_OVERRIDE
    {
        if (!state && engineReady_)
            engine_.reset();
        if (!state)
            heldNotes_.clear(); // reset voices: no key can stay lit
        return AudioEffect::setProcessing (state);
    }

    Steinberg::tresult PLUGIN_API canProcessSampleSize (
        Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        if (symbolicSampleSize == Steinberg::Vst::kSample32
            || symbolicSampleSize == Steinberg::Vst::kSample64)
        {
            return Steinberg::kResultTrue;
        }
        return Steinberg::kResultFalse;
    }

    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE
    {
        // Never infinite so hosts can sleep.
        const double tail = Traits::tailSeconds (currentParams_) * sampleRate_;
        if (tail > static_cast<double> (0xFFFFFFFEu))
            return 0xFFFFFFFEu;
        if (tail < 0.0)
            return 0;
        return static_cast<Steinberg::uint32> (tail);
    }

    /**
     * On-screen keyboard notes from the controller (HasUiKeyboard traits).
     * Runs on the host's message thread (never the audio thread): the note
     * goes into the lock-free SPSC queue that process() drains at the next
     * block start. Everything else goes to the SDK base.
     */
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE
    {
        if constexpr (HasUiKeyboard<Traits>)
        {
            if (message != nullptr && message->getMessageID() != nullptr
                && std::strcmp (message->getMessageID(), kUiNoteMessageId) == 0)
            {
                Steinberg::Vst::IAttributeList* attributes = message->getAttributes();
                if (attributes == nullptr)
                    return Steinberg::kResultFalse;
                Steinberg::int64 note = -1;
                Steinberg::int64 on = 0;
                double velocity = 0.0;
                if (attributes->getInt ("note", note) != Steinberg::kResultOk
                    || attributes->getInt ("on", on) != Steinberg::kResultOk)
                {
                    return Steinberg::kResultFalse;
                }
                if (attributes->getFloat ("velocity", velocity) != Steinberg::kResultOk)
                    velocity = on != 0 ? 0.8 : 0.0;
                if (note < 0 || note > 127)
                    return Steinberg::kResultFalse;
                UiNote queued {};
                queued.note = static_cast<std::int32_t> (note);
                queued.velocity = velocity >= 0.0 && velocity <= 1.0
                    ? static_cast<float> (velocity)
                    : (velocity > 1.0 ? 1.0F : 0.0F);
                queued.on = on != 0;
                return uiNotes_.push (queued) ? Steinberg::kResultOk : Steinberg::kResultFalse;
            }
        }
        return AudioEffect::notify (message);
    }

    /** Test hook: the queue notify() feeds (producer side only). */
    UiNoteQueue& uiNoteQueue() noexcept { return uiNotes_; }

    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        const int numSamples = static_cast<int> (data.numSamples);
        if (numSamples <= 0 || data.numOutputs < 1)
            return kResultOk;
        AudioBusBuffers& out = data.outputs[0];
        if (out.numChannels < 1)
            return kResultOk;

        // Merge automation, pedal/utility changes and note events, then sort
        // by sample offset (stable insertion sort over the preallocated buffer).
        Params params = currentParams_;
        int eventCount = 0;
        collectUiNotes (eventCount, numSamples);
        applyParameterChanges (data, params, eventCount);
        collectNoteEvents (data, numSamples, eventCount);
        for (int i = 1; i < eventCount; ++i)
        {
            const core::Event key = eventBuffer_[static_cast<std::size_t> (i)];
            int j = i - 1;
            while (j >= 0
                   && eventBuffer_[static_cast<std::size_t> (j)].sampleOffset > key.sampleOffset)
            {
                eventBuffer_[static_cast<std::size_t> (j + 1)]
                    = eventBuffer_[static_cast<std::size_t> (j)];
                --j;
            }
            eventBuffer_[static_cast<std::size_t> (j + 1)] = key;
        }

        const int automationBoundaries
            = collectAutomationBoundaries (data, numSamples);
        noteHeldNotes (data, eventCount, numSamples);

        // Bypass freezes the engine (voices reset on entry so no note can
        // stick) and emits silence; automation is still tracked above so
        // un-bypassing resumes with current values.
        if (bypassed_)
        {
            applyScheduledAutomation (data, params, automationBoundaries);
            currentParams_ = params;
            if (!bypassSilenced_)
            {
                engine_.reset();
                bypassSilenced_ = true;
            }
            clearOutputs (data, numSamples);
            noteOutputLevels (data, 0.0F, 0.0F, numSamples);
            return kResultOk;
        }
        bypassSilenced_ = false;

        if (!engineReady_)
        {
            applyScheduledAutomation (data, params, automationBoundaries);
            currentParams_ = params;
            // Should not happen (setupProcessing always precedes process),
            // but never emit garbage: output silence.
            clearOutputs (data, numSamples);
            noteOutputLevels (data, 0.0F, 0.0F, numSamples);
            return kResultOk;
        }

        if constexpr (HasEngineTempo<Engine>)
        {
            if (data.processContext != nullptr
                && (data.processContext->state & ProcessContext::kTempoValid) != 0)
            {
                engine_.setTempo (data.processContext->tempo);
            }
        }
        if constexpr (HasEngineTransport<Engine>)
        {
            const bool hasContext = data.processContext != nullptr;
            const auto state = hasContext ? data.processContext->state : 0;
            const bool positionValid = hasContext && (state & ProcessContext::kProjectTimeMusicValid) != 0;
            engine_.setTransport (hasContext && (state & ProcessContext::kPlaying) != 0,
                                  positionValid,
                                  positionValid ? data.processContext->projectTimeMusic : 0.0);
        }

        const core::Event* events = eventCount > 0 ? eventBuffer_.data() : nullptr;
        if (data.symbolicSampleSize == Vst::kSample32)
        {
            if (out.channelBuffers32 == nullptr || out.channelBuffers32[0] == nullptr)
                return kResultOk;
            float* left = out.channelBuffers32[0];
            float* right = out.numChannels > 1 && out.channelBuffers32[1] != nullptr
                ? out.channelBuffers32[1]
                : left;
            processScheduled (data, left, right, numSamples, params, events, eventCount,
                              automationBoundaries);
            currentParams_ = params;
            noteOutputLevels (data, blockPeak (left, numSamples),
                              blockPeak (right, numSamples), numSamples);
        }
        else
        {
            // 64-bit host: render into the preallocated float scratch, then copy.
            if (out.channelBuffers64 == nullptr || out.channelBuffers64[0] == nullptr)
                return kResultOk;
            if (scratchLeft_.size() < static_cast<std::size_t> (numSamples)
                || scratchRight_.size() < static_cast<std::size_t> (numSamples))
            {
                clearOutputs (data, numSamples);
                noteOutputLevels (data, 0.0F, 0.0F, numSamples);
                return kResultOk; // host exceeded setupProcessing's max: stay silent
            }
            float* left = scratchLeft_.data();
            float* right = scratchRight_.data();
            processScheduled (data, left, right, numSamples, params, events, eventCount,
                              automationBoundaries);
            currentParams_ = params;
            noteOutputLevels (data, blockPeak (left, numSamples),
                              blockPeak (right, numSamples), numSamples);
            double* hostLeft = out.channelBuffers64[0];
            double* hostRight
                = out.numChannels > 1 && out.channelBuffers64[1] != nullptr
                ? out.channelBuffers64[1]
                : hostLeft;
            for (int s = 0; s < numSamples; ++s)
            {
                hostLeft[s] = static_cast<double> (left[s]);
                hostRight[s] = static_cast<double> (right[s]);
            }
        }
        return kResultOk;
    }

    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if (state == nullptr)
            return kResultFalse;
        // Drain the stream into a bounded stack buffer (never trusts its
        // size), then decode with the shared codec: corrupt data keeps the
        // defaults.
        std::uint8_t bytes[kMaxStateBytes] {};
        std::size_t total = 0;
        IBStreamer streamer (state, kLittleEndian);
        for (;;)
        {
            if (total >= kMaxStateBytes)
                break;
            const auto space = static_cast<Steinberg::TSize> (kMaxStateBytes - total);
            const Steinberg::TSize got = streamer.readRaw (bytes + total, space);
            if (got <= 0)
                break;
            total += static_cast<std::size_t> (got);
            if (got < space)
                break;
        }
        Params params = Params::defaults();
        const bool ok = decodeState<Traits> (bytes, total, params);
        if (ok)
            currentParams_ = params;
        else
            currentParams_ = Params::defaults();
        appliedProgram_ = kAdoptNextProgram; // loaded values win over a re-sent program
        return ok ? kResultOk : kResultFalse;
    }

    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if (state == nullptr)
            return kResultFalse;
        std::uint8_t bytes[kMaxStateBytes] {};
        const std::size_t used = encodeState<Traits> (currentParams_, bytes, kMaxStateBytes);
        if (used == 0)
            return kResultFalse;
        IBStreamer streamer (state, kLittleEndian);
        const Steinberg::TSize wrote
            = streamer.writeRaw (bytes, static_cast<Steinberg::TSize> (used));
        return wrote == static_cast<Steinberg::TSize> (used) ? kResultOk : kResultFalse;
    }

private:
    static float blockPeak (const float* samples, int numSamples) noexcept
    {
        float peak = 0.0F;
        if (samples == nullptr)
            return peak;
        for (int s = 0; s < numSamples; ++s)
        {
            const float magnitude = samples[s] < 0.0F ? -samples[s] : samples[s];
            if (magnitude > peak)
                peak = magnitude;
        }
        return peak;
    }

    static void sendOutputLevel (Steinberg::Vst::IParameterChanges& changes,
                                 Steinberg::Vst::ParamID tag, float peak) noexcept
    {
        Steinberg::int32 queueIndex = 0;
        Steinberg::Vst::IParamValueQueue* queue = changes.addParameterData (tag, queueIndex);
        if (queue == nullptr)
            return;
        double clamped = static_cast<double> (peak);
        if (!(clamped >= 0.0)) // catches negatives and NaN: meters rest at 0
            clamped = 0.0;
        else if (clamped > 1.0)
            clamped = 1.0;
        Steinberg::int32 pointIndex = 0;
        queue->addPoint (0, clamped, pointIndex);
    }

    // Folds this block's rendered peaks into the ~30 Hz meter feed and
    // flushes one stereo pair into the host's output queue per tick.
    // Real-time safe: float/int arithmetic plus two host queue writes (the
    // host owns that queue; no allocation, locks or I/O on our side).
    // Products whose traits lack the meter IDs compile this to nothing.
    void noteOutputLevels (Steinberg::Vst::ProcessData& data, float peakL, float peakR,
                            int numSamples) noexcept
    {
        if constexpr (HasOutputMeterIds<Traits>)
        {
            meterFeed_.observe (peakL, peakR, numSamples);
            if (!meterFeed_.ready())
                return;
            float emitL = 0.0F;
            float emitR = 0.0F;
            meterFeed_.take (emitL, emitR); // always restart the tick, even queue-less
            if (data.outputParameterChanges == nullptr)
                return;
            sendOutputLevel (*data.outputParameterChanges,
                             static_cast<Steinberg::Vst::ParamID> (Traits::kMeterLevelParamIdL),
                             emitL);
            sendOutputLevel (*data.outputParameterChanges,
                             static_cast<Steinberg::Vst::ParamID> (Traits::kMeterLevelParamIdR),
                             emitR);
        }
    }
    static constexpr int kAdoptNextProgram = -2;
    int appliedProgram_ = -1; // factory program last applied (-1 none yet)

    // Drains on-screen keyboard notes into the block at offset 0 (they were
    // played "now"). Real-time safe: SPSC pops into the fixed event array.
    void collectUiNotes (int& eventCount, int numSamples) noexcept
    {
        if constexpr (HasUiKeyboard<Traits>)
        {
            UiNote note {};
            while (eventCount < core::kMaxEventsPerBlock && uiNotes_.pop (note))
            {
                const core::Event e = note.on
                    ? makeNoteEvent (note.note, static_cast<double> (note.velocity), 0)
                    : makeNoteOffEvent (note.note, static_cast<double> (note.velocity), 0);
                pushEvent (eventCount, e, numSamples);
            }
        }
        else
        {
            (void)eventCount;
            (void)numSamples;
        }
    }

    // Folds this block's merged events into the held-note set and, at
    // ~30 Hz, sends the changed 16-note words as hidden output parameters.
    // Real-time safe: integer work plus host-owned queue writes.
    void noteHeldNotes (Steinberg::Vst::ProcessData& data, int eventCount,
                        int numSamples) noexcept
    {
        if constexpr (HasUiKeyboard<Traits>)
        {
            for (int i = 0; i < eventCount; ++i)
                heldNotes_.apply (eventBuffer_[static_cast<std::size_t> (i)]);
            if (!heldFeed_.advance (numSamples))
                return;
            if (data.outputParameterChanges == nullptr)
                return;
            for (int w = 0; w < kHeldNoteWords; ++w)
            {
                if (!heldFeed_.needsSend (heldNotes_, w))
                    continue;
                Steinberg::int32 queueIndex = 0;
                Steinberg::Vst::IParamValueQueue* queue
                    = data.outputParameterChanges->addParameterData (
                        static_cast<Steinberg::Vst::ParamID> (Traits::kHeldNotesParamIdBase
                                                              + static_cast<std::uint32_t> (w)),
                        queueIndex);
                if (queue == nullptr)
                    continue;
                Steinberg::int32 pointIndex = 0;
                queue->addPoint (0, HeldNotes::wordToNormalized (heldNotes_.word (w)),
                                 pointIndex);
            }
            heldFeed_.markSent (heldNotes_);
        }
        else
        {
            (void)data;
            (void)eventCount;
            (void)numSamples;
        }
    }

    void clearOutputs (Steinberg::Vst::ProcessData& data, int numSamples) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        AudioBusBuffers& out = data.outputs[0];
        if (data.symbolicSampleSize == Vst::kSample32)
        {
            if (out.channelBuffers32 == nullptr)
                return;
            for (int32 c = 0; c < out.numChannels; ++c)
            {
                if (out.channelBuffers32[c] == nullptr)
                    continue;
                for (int s = 0; s < numSamples; ++s)
                    out.channelBuffers32[c][s] = 0.0F;
            }
        }
        else
        {
            if (out.channelBuffers64 == nullptr)
                return;
            for (int32 c = 0; c < out.numChannels; ++c)
            {
                if (out.channelBuffers64[c] == nullptr)
                    continue;
                for (int s = 0; s < numSamples; ++s)
                    out.channelBuffers64[c][s] = 0.0;
            }
        }
    }

    bool pushEvent (int& eventCount, core::Event e, int numSamples) noexcept
    {
        if (eventCount >= core::kMaxEventsPerBlock)
            return false; // drop overflow without allocating
        e.sampleOffset = clampOffset (static_cast<std::int32_t> (e.sampleOffset), numSamples);
        eventBuffer_[static_cast<std::size_t> (eventCount)] = e;
        ++eventCount;
        return true;
    }

    static constexpr int kMaxAutomationBoundaries = 64;

    void insertAutomationBoundary (int offset, int& count, bool& overflowed) noexcept
    {
        int insert = 0;
        while (insert < count && automationBoundaries_[static_cast<std::size_t> (insert)] < offset)
            ++insert;
        if (insert < count && automationBoundaries_[static_cast<std::size_t> (insert)] == offset)
            return;

        if (count < kMaxAutomationBoundaries)
        {
            for (int i = count; i > insert; --i)
                automationBoundaries_[static_cast<std::size_t> (i)]
                    = automationBoundaries_[static_cast<std::size_t> (i - 1)];
            automationBoundaries_[static_cast<std::size_t> (insert)] = offset;
            ++count;
            return;
        }

        overflowed = true;
        // Retain the earliest boundaries. All later points are merged at the
        // final retained boundary below, keeping the work bounded on the audio thread.
        if (insert >= count)
            return;
        for (int i = count - 1; i > insert; --i)
            automationBoundaries_[static_cast<std::size_t> (i)]
                = automationBoundaries_[static_cast<std::size_t> (i - 1)];
        automationBoundaries_[static_cast<std::size_t> (insert)] = offset;
    }

    int collectAutomationBoundaries (Steinberg::Vst::ProcessData& data,
                                     int numSamples) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        automationOverflowed_ = false;
        if (data.inputParameterChanges == nullptr)
            return 0;

        int count = 0;
        const int32 queues = data.inputParameterChanges->getParameterCount();
        for (int32 q = 0; q < queues; ++q)
        {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData (q);
            if (queue == nullptr)
                continue;
            const std::uint32_t id = static_cast<std::uint32_t> (queue->getParameterId());
            if (id >= static_cast<std::uint32_t> (Traits::kNumParams))
                continue;
            const int32 points = queue->getPointCount();
            for (int32 p = 0; p < points; ++p)
            {
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (p, offset, value) != kResultTrue)
                    continue;
                insertAutomationBoundary (clampOffset (offset, numSamples), count,
                                          automationOverflowed_);
            }
        }
        return count;
    }

    void applyAutomationAt (Steinberg::Vst::ProcessData& data, Params& params,
                            int offset, bool mergeRemaining) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        if (data.inputParameterChanges == nullptr)
            return;
        const int32 queues = data.inputParameterChanges->getParameterCount();
        for (int32 q = 0; q < queues; ++q)
        {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData (q);
            if (queue == nullptr)
                continue;
            const std::uint32_t id = static_cast<std::uint32_t> (queue->getParameterId());
            if (id >= static_cast<std::uint32_t> (Traits::kNumParams))
                continue;
            const int32 points = queue->getPointCount();
            for (int32 p = 0; p < points; ++p)
            {
                int32 pointOffset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (p, pointOffset, value) != kResultTrue)
                    continue;
                pointOffset = clampOffset (pointOffset, data.numSamples);
                if (pointOffset != offset && (!mergeRemaining || pointOffset < offset))
                    continue;
                const auto paramId = static_cast<ParamId> (id);
                params.set (paramId, Traits::toPlain (paramId, value));
            }
        }
    }

    void applyScheduledAutomation (Steinberg::Vst::ProcessData& data, Params& params,
                                   int boundaries) noexcept
    {
        for (int i = 0; i < boundaries; ++i)
        {
            const bool mergeRemaining = automationOverflowed_ && i == boundaries - 1;
            applyAutomationAt (data, params,
                               automationBoundaries_[static_cast<std::size_t> (i)],
                               mergeRemaining);
        }
    }

    void processScheduled (Steinberg::Vst::ProcessData& data, float* left, float* right,
                           int numSamples, Params& params, const core::Event* events,
                           int eventCount, int boundaries) noexcept
    {
        int start = 0;
        int eventIndex = 0;
        for (int i = 0; i < boundaries; ++i)
        {
            const int offset = automationBoundaries_[static_cast<std::size_t> (i)];
            if (offset > start)
            {
                processRange (left, right, start, offset, params, events, eventCount,
                              eventIndex);
                start = offset;
            }
            applyAutomationAt (data, params, offset,
                               automationOverflowed_ && i == boundaries - 1);
        }
        if (start < numSamples)
            processRange (left, right, start, numSamples, params, events, eventCount,
                          eventIndex);
    }

    void processRange (float* left, float* right, int start, int end, Params& params,
                       const core::Event* events, int eventCount, int& eventIndex) noexcept
    {
        int segmentEvents = 0;
        while (eventIndex < eventCount
               && events[static_cast<std::size_t> (eventIndex)].sampleOffset < end)
        {
            core::Event event = events[static_cast<std::size_t> (eventIndex)];
            event.sampleOffset -= start;
            segmentEvents_[static_cast<std::size_t> (segmentEvents)] = event;
            ++segmentEvents;
            ++eventIndex;
        }
        engine_.setParameters (params);
        const core::AudioBlock block {left + start, right + start, end - start};
        engine_.process (block, segmentEvents > 0 ? segmentEvents_.data() : nullptr,
                         segmentEvents);
    }

    void applyParameterChanges (Steinberg::Vst::ProcessData& data, Params& params,
                               int& eventCount) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        if (data.inputParameterChanges == nullptr)
            return;
        const int32 numQueues = data.inputParameterChanges->getParameterCount();

        // Pass 1: factory program selection becomes the parameter base, so
        // that explicit per-parameter automation (pass 2) always wins over
        // the preset.
        if (Traits::numPresets() > 0)
        {
            for (int32 q = 0; q < numQueues; ++q)
            {
                IParamValueQueue* queue = data.inputParameterChanges->getParameterData (q);
                if (queue == nullptr
                    || queue->getParameterId()
                        != static_cast<ParamID> (Traits::kProgramParamId))
                {
                    continue;
                }
                const int32 points = queue->getPointCount();
                if (points <= 0)
                    continue;
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (points - 1, offset, value) != kResultTrue)
                    continue;
                const auto last = static_cast<int> (Traits::numPresets() - 1);
                int index = static_cast<int> (value * static_cast<double> (last) + 0.5);
                if (index < 0)
                    index = 0;
                if (index > last)
                    index = last;
                // Only a real change of selection applies a preset: hosts may
                // re-send the current program, which must not wipe edits.
                if (index == appliedProgram_)
                    continue;
                if (appliedProgram_ == kAdoptNextProgram)
                {
                    appliedProgram_ = index; // first value after a state load
                    continue;
                }
                appliedProgram_ = index;
                if (const FactoryPreset* preset = presetAt<Traits> (index))
                    params = presetToParameters<Traits> (*preset);
            }
        }

        // Pass 2: hidden sample-offset pedal/utility events and the latched
        // bypass switch. Contract parameter automation is scheduled in
        // processScheduled() so each point reaches the engine at its offset.
        for (int32 q = 0; q < numQueues; ++q)
        {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData (q);
            if (queue == nullptr)
                continue;
            const ParamID tag = queue->getParameterId();
            const int32 points = queue->getPointCount();
            if (points <= 0)
                continue;

            const std::uint32_t id = static_cast<std::uint32_t> (tag);
            if (id < static_cast<std::uint32_t> (Traits::kNumParams))
                continue;
            if (tag == static_cast<ParamID> (Traits::kBypassParamId))
            {
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (points - 1, offset, value) == kResultTrue)
                    bypassed_ = value >= 0.5;
                continue;
            }
            if (tag != static_cast<ParamID> (Traits::kSustainParamId)
                && tag != static_cast<ParamID> (Traits::kAllNotesOffParamId)
                && tag != static_cast<ParamID> (Traits::kAllSoundOffParamId)
                && tag != static_cast<ParamID> (Traits::kModWheelParamId)
                && tag != static_cast<ParamID> (Traits::kPitchBendParamId))
            {
                continue;
            }
            for (int32 p = 0; p < points; ++p)
            {
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (p, offset, value) != kResultTrue)
                    continue;
                core::Event e {};
                e.sampleOffset = offset;
                if (tag == static_cast<ParamID> (Traits::kSustainParamId))
                {
                    e = makeSustainEvent (value, e.sampleOffset);
                }
                else if (tag == static_cast<ParamID> (Traits::kModWheelParamId))
                {
                    e = makeModWheelEvent (value, e.sampleOffset);
                }
                else if (tag == static_cast<ParamID> (Traits::kPitchBendParamId))
                {
                    e = makePitchBendEvent (pitchBendFromNormalized (value),
                                            e.sampleOffset);
                }
                else if (tag == static_cast<ParamID> (Traits::kAllNotesOffParamId))
                {
                    if (!isMomentaryTrigger (value))
                        continue;
                    e.type = core::Event::Type::allNotesOff;
                }
                else
                {
                    if (!isMomentaryTrigger (value))
                        continue;
                    e.type = core::Event::Type::allSoundOff;
                }
                pushEvent (eventCount, e, data.numSamples);
            }
        }
    }

    void collectNoteEvents (Steinberg::Vst::ProcessData& data, int numSamples,
                            int& eventCount) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        if (data.inputEvents == nullptr)
            return;
        const int32 numEvents = data.inputEvents->getEventCount();
        for (int32 i = 0; i < numEvents; ++i)
        {
            Vst::Event e {};
            if (data.inputEvents->getEvent (i, e) != kResultTrue)
                continue;
            if (e.type == Vst::Event::kNoteOnEvent)
            {
                pushEvent (eventCount,
                           makeNoteEvent (static_cast<int> (e.noteOn.pitch),
                                          static_cast<double> (e.noteOn.velocity),
                                          clampOffset (e.sampleOffset, numSamples)),
                           numSamples);
            }
            else if (e.type == Vst::Event::kNoteOffEvent)
            {
                core::Event out = makeNoteOffEvent (
                    static_cast<int> (e.noteOff.pitch),
                    static_cast<double> (e.noteOff.velocity),
                    clampOffset (e.sampleOffset, numSamples));
                pushEvent (eventCount, out, numSamples);
            }
            else if (e.type == Vst::Event::kPolyPressureEvent)
            {
                pushEvent (eventCount,
                           makePolyPressureEvent (static_cast<int> (e.polyPressure.pitch),
                                                  static_cast<double> (e.polyPressure.pressure),
                                                  clampOffset (e.sampleOffset, numSamples)),
                           numSamples);
            }
            else if (e.type == Vst::Event::kLegacyMIDICCOutEvent)
            {
                // Hosts that deliver MIDI CCs as legacy events instead of
                // IMidiMapping parameter changes (mod wheel CC1, pitch bend).
                const int controlNumber
                    = static_cast<int> (e.midiCCOut.controlNumber);
                const int offset = clampOffset (e.sampleOffset, numSamples);
                if (controlNumber == Vst::kCtrlModWheel)
                {
                    const double normalized
                        = static_cast<double> (e.midiCCOut.value) / 127.0;
                    pushEvent (eventCount, makeModWheelEvent (normalized, offset),
                               numSamples);
                }
                else if (controlNumber == Vst::kPitchBend)
                {
                    pushEvent (eventCount,
                               makePitchBendEvent (
                                   pitchBendFromMidi14 (
                                       static_cast<int> (e.midiCCOut.value),
                                       static_cast<int> (e.midiCCOut.value2)),
                                   offset),
                               numSamples);
                }
                else if constexpr (HasChannelPressure<Traits>)
                {
                    if (controlNumber == Vst::kAfterTouch)
                    {
                        const double normalized
                            = static_cast<double> (e.midiCCOut.value) / 127.0;
                        pushEvent (eventCount, makeChannelPressureEvent (normalized, offset),
                                   numSamples);
                    }
                }
            }
        }
    }

    // Latest automation state in plain units; refreshed once per block.
    Params currentParams_;
    Engine engine_;
    bool engineReady_ = false;
    double sampleRate_ = 48000.0;
    bool bypassed_ = false;
    bool bypassSilenced_ = true;

    // Preallocated (never grown on the audio thread): merged event list and
    // float scratch for 64-bit hosts. setupProcessing() is the only writer.
    std::array<core::Event, core::kMaxEventsPerBlock> eventBuffer_ {};
    std::array<core::Event, core::kMaxEventsPerBlock> segmentEvents_ {};
    std::array<int, kMaxAutomationBoundaries> automationBoundaries_ {};
    bool automationOverflowed_ = false;
    std::vector<float> scratchLeft_;
    std::vector<float> scratchRight_;
    // Stereo peak accumulator; the tick rate follows setupProcessing's rate.
    kraftonic::formats::OutputMeterFeed meterFeed_ {};
    // On-screen keyboard path (HasUiKeyboard): UI notes in, held notes out.
    UiNoteQueue uiNotes_ {};
    HeldNotes heldNotes_ {};
    HeldNotesFeed heldFeed_ {};
};

template <PluginTraits Traits>
Steinberg::FUID Processor<Traits>::cid (Traits::kProcessorFuid[0], Traits::kProcessorFuid[1],
                                        Traits::kProcessorFuid[2], Traits::kProcessorFuid[3]);

} // namespace kraftonic::formats::vst3
