// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Shared VST3 EFFECT processor.
//
//  EffectProcessor<Traits> turns host audio and automation into the SDK-free
//  effect engine contract for any product whose traits satisfy
//  effect::EffectTraits:
//    - one main audio input + one main audio output, no event bus;
//      stereo->stereo and mono->stereo (BusLayout.h);
//    - sample-accurate parameter changes: the block is split at automation
//      points (ParamSchedule.h) and the engine sees each new value from the
//      right sample; factory program changes form the base of the block;
//    - latency from Engine::latencySamples() via getLatencySamples(), and
//      a bypass whose dry path is delayed by the same latency and crossfaded
//      so toggling never clicks or shifts the audio (LatencyBypass.h);
//    - tail from Traits::tailSeconds() via getTailSamples();
//    - 32- and 64-bit samples, hosts that exceed maxSamplesPerBlock (we
//      process in chunks), in-place host buffers, missing input buffers;
//    - state get/set through the shared codec (same as instruments).
//
//  Real-time: no allocation, locks, I/O, logging or exceptions in process();
//  everything is sized in setupProcessing().
// ============================================================================

#include "kraftonic/core/Instrument.h"
#include "kraftonic/formats/LatencyBypass.h"
#include "kraftonic/formats/ParamSchedule.h"
#include "kraftonic/formats/Preset.h"
#include "kraftonic/formats/StateCodec.h"
#include "kraftonic/formats/vst3/Vst3SdkIncludes.h"
#include "kraftonic/formats/vst3/effect/BusLayout.h"
#include "kraftonic/formats/vst3/effect/EffectTraits.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kraftonic::formats::vst3::effect
{

static_assert (kSpeakerMono == Steinberg::Vst::SpeakerArr::kMono, "mono mask mirrors the SDK");
static_assert (kSpeakerStereo == Steinberg::Vst::SpeakerArr::kStereo, "stereo mask mirrors the SDK");

template <EffectTraits Traits>
class EffectProcessor : public Steinberg::Vst::AudioEffect
{
public:
    using ParamId = typename Traits::ParamId;
    using Params = typename Traits::Params;
    using Engine = typename Traits::Engine;

    EffectProcessor()
    {
        currentParams_ = Params::defaults();
        const Steinberg::FUID controllerId (Traits::kControllerFuid[0], Traits::kControllerFuid[1],
                                            Traits::kControllerFuid[2], Traits::kControllerFuid[3]);
        setControllerClass (controllerId);
        updateTail();
    }

    ~EffectProcessor() override = default;

    static Steinberg::FUnknown* createInstance (void* /*context*/)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new EffectProcessor());
    }

    static Steinberg::FUID cid;

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE
    {
        const Steinberg::tresult result = AudioEffect::initialize (context);
        if (result != Steinberg::kResultOk)
            return result;
        addAudioInput (STR16 ("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
        addAudioOutput (STR16 ("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);
        inputLayout_ = InputLayout::stereo;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API setBusArrangements (
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE
    {
        static_assert (sizeof (Steinberg::Vst::SpeakerArrangement) == sizeof (SpeakerArrangement));
        if (!acceptsBusArrangement (inputs, static_cast<int> (numIns), outputs,
                                    static_cast<int> (numOuts)))
        {
            return Steinberg::kResultFalse; // keep the current (supported) layout
        }
        if (audioInputs.empty() || audioOutputs.empty())
            return Steinberg::kResultFalse;
        auto* inBus = static_cast<Steinberg::Vst::AudioBus*> (audioInputs.at (0).get());
        auto* outBus = static_cast<Steinberg::Vst::AudioBus*> (audioOutputs.at (0).get());
        if (inBus == nullptr || outBus == nullptr)
            return Steinberg::kResultFalse;
        inBus->setArrangement (inputs[0]);
        inBus->setName (inputs[0] == Steinberg::Vst::SpeakerArr::kMono ? STR16 ("Mono In")
                                                                     : STR16 ("Stereo In"));
        outBus->setArrangement (outputs[0]);
        inputLayout_ = inputLayoutFor (inputs[0]);
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        return symbolicSampleSize == Steinberg::Vst::kSample32
                || symbolicSampleSize == Steinberg::Vst::kSample64
            ? Steinberg::kResultTrue
            : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE
    {
        const Steinberg::tresult result = AudioEffect::setupProcessing (newSetup);
        if (result != Steinberg::kResultOk)
            return result;

        double rate = newSetup.sampleRate;
        if (!(rate >= 22050.0))
            rate = 22050.0;
        if (rate > 384000.0)
            rate = 384000.0;
        sampleRate_ = rate;

        // Off the audio thread: allocation is fine here.
        Steinberg::int32 maxBlock = newSetup.maxSamplesPerBlock;
        if (maxBlock < 1)
            maxBlock = 1;
        if (maxBlock > 65536)
            maxBlock = 65536;
        maxBlock_ = static_cast<int> (maxBlock);
        for (auto* buffer : {&dryLeft_, &dryRight_, &wetLeft_, &wetRight_})
            buffer->assign (static_cast<std::size_t> (maxBlock), 0.0F);

        core::ProcessSetup setup {};
        setup.sampleRate = sampleRate_;
        setup.maxBlockSize = maxBlock_;
        engine_.prepare (setup);
        engine_.setParameters (currentParams_);
        latency_ = engine_.latencySamples();
        if (latency_ < 0)
            latency_ = 0;
        bypass_.prepare (sampleRate_, latency_);
        bypass_.setBypassed (bypassed_);
        bypass_.snapToTarget();
        engineReady_ = true;
        engineDirty_ = false;
        updateTail();
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE
    {
        if (state && engineReady_)
        {
            engine_.reset();
            bypass_.reset();
        }
        return AudioEffect::setActive (state);
    }

    Steinberg::tresult PLUGIN_API setProcessing (Steinberg::TBool state) SMTG_OVERRIDE
    {
        if (!state && engineReady_)
        {
            engine_.reset();
            bypass_.reset();
        }
        AudioEffect::setProcessing (state); // SDK default reports kNotImplemented
        return Steinberg::kResultOk;
    }

    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE
    {
        return static_cast<Steinberg::uint32> (latency_);
    }

    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE
    {
        return tailSamples_.load (std::memory_order_relaxed);
    }

    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        const int numSamples = data.numSamples > 0 ? static_cast<int> (data.numSamples) : 0;
        collectParameterChanges (data, numSamples);

        if (numSamples == 0 || data.numOutputs < 1 || data.outputs == nullptr)
        {
            // Parameter flush (no audio): adopt every change now.
            applyAllPending();
            return kResultOk;
        }
        AudioBusBuffers& out = data.outputs[0];
        out.silenceFlags = 0;
        if (out.numChannels < 1 || !engineReady_)
        {
            applyAllPending();
            clearOutputs (out, data.symbolicSampleSize, numSamples);
            return kResultOk;
        }

        bypass_.setBypassed (bypassed_);
        const AudioBusBuffers* in = data.numInputs > 0 && data.inputs != nullptr ? &data.inputs[0] : nullptr;
        const bool is64 = data.symbolicSampleSize == kSample64;

        for (int chunkStart = 0; chunkStart < numSamples; chunkStart += maxBlock_)
        {
            const int remaining = numSamples - chunkStart;
            const int chunk = remaining < maxBlock_ ? remaining : maxBlock_;
            readInput (in, is64, chunkStart, chunk);
            for (int i = 0; i < chunk; ++i)
            {
                wetLeft_[static_cast<std::size_t> (i)] = dryLeft_[static_cast<std::size_t> (i)];
                wetRight_[static_cast<std::size_t> (i)] = dryRight_[static_cast<std::size_t> (i)];
            }
            runEngine (chunkStart, chunk);
            bypass_.process (dryLeft_.data(), dryRight_.data(), wetLeft_.data(), wetRight_.data(), chunk);
            writeOutput (out, is64, chunkStart, chunk);
        }
        applyAllPending(); // points past the last sub-block (none in practice)
        updateTail();
        return kResultOk;
    }

    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if (state == nullptr)
            return kResultFalse;
        std::uint8_t bytes[kraftonic::formats::kMaxStateBytes] {};
        const std::size_t total = drain (state, bytes);
        Params params = Params::defaults();
        const bool ok = kraftonic::formats::decodeState<Traits> (bytes, total, params);
        currentParams_ = ok ? params : Params::defaults();
        appliedProgram_ = kAdoptNextProgram; // the loaded values win over a re-sent program
        engineDirty_ = true; // the audio thread pushes it to the engine next block
        updateTail();
        return ok ? kResultOk : kResultFalse;
    }

    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if (state == nullptr)
            return kResultFalse;
        std::uint8_t bytes[kraftonic::formats::kMaxStateBytes] {};
        const std::size_t used
            = kraftonic::formats::encodeState<Traits> (currentParams_, bytes, kraftonic::formats::kMaxStateBytes);
        if (used == 0)
            return kResultFalse;
        IBStreamer streamer (state, kLittleEndian);
        const TSize wrote = streamer.writeRaw (bytes, static_cast<TSize> (used));
        return wrote == static_cast<TSize> (used) ? kResultOk : kResultFalse;
    }

    // --- Test/inspection hooks (UI-free, used by plug-in tests) ---------------
    const Params& currentParameters() const noexcept { return currentParams_; }
    bool isBypassed() const noexcept { return bypassed_; }

private:
    static std::size_t drain (Steinberg::IBStream* state, std::uint8_t* bytes) noexcept
    {
        using namespace Steinberg;
        std::size_t total = 0;
        IBStreamer streamer (state, kLittleEndian);
        while (total < kraftonic::formats::kMaxStateBytes)
        {
            const auto space = static_cast<TSize> (kraftonic::formats::kMaxStateBytes - total);
            const TSize got = streamer.readRaw (bytes + total, space);
            if (got <= 0)
                break;
            total += static_cast<std::size_t> (got);
            if (got < space)
                break;
        }
        return total;
    }

    /** Reads host automation into the schedule (and the program/bypass switches). */
    void collectParameterChanges (Steinberg::Vst::ProcessData& data, int numSamples) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        schedule_.clear();
        if (engineDirty_)
        {
            // State loaded off the audio thread: hand it to the engine here.
            pushParams_ = true;
            engineDirty_ = false;
        }
        IParameterChanges* changes = data.inputParameterChanges;
        if (changes == nullptr)
            return;
        const int32 numQueues = changes->getParameterCount();
        const int span = numSamples > 0 ? numSamples : 1;

        // Pass 1: a factory program change replaces the block's base values,
        // so explicit automation (pass 2) always wins over the preset.
        for (int32 q = 0; q < numQueues; ++q)
        {
            IParamValueQueue* queue = changes->getParameterData (q);
            if (queue == nullptr || queue->getParameterId() != static_cast<ParamID> (Traits::kProgramParamId))
                continue;
            int32 offset = 0;
            ParamValue value = 0.0;
            const int32 points = queue->getPointCount();
            if (points <= 0 || Traits::numPresets() == 0
                || queue->getPoint (points - 1, offset, value) != kResultTrue)
            {
                continue;
            }
            const auto last = static_cast<int> (Traits::numPresets() - 1);
            int index = static_cast<int> (value * static_cast<double> (last) + 0.5);
            index = index < 0 ? 0 : (index > last ? last : index);
            // Apply a preset only when the selection really changes. Hosts may
            // re-send the current program (e.g. REAPER when a control is
            // touched); re-applying it would wipe the user's knob changes.
            if (index == appliedProgram_)
                continue;
            if (appliedProgram_ == kAdoptNextProgram)
            {
                appliedProgram_ = index; // first value after a state load: keep the loaded values
                continue;
            }
            appliedProgram_ = index;
            if (const auto* preset = kraftonic::formats::presetAt<Traits> (index))
            {
                currentParams_ = kraftonic::formats::presetToParameters<Traits> (*preset);
                pushParams_ = true;
            }
        }

        // Pass 2: contract parameters go into the sample-accurate schedule;
        // the latched bypass switch takes its last value for the block.
        for (int32 q = 0; q < numQueues; ++q)
        {
            IParamValueQueue* queue = changes->getParameterData (q);
            if (queue == nullptr)
                continue;
            const ParamID tag = queue->getParameterId();
            const int32 points = queue->getPointCount();
            if (points <= 0)
                continue;
            if (tag == static_cast<ParamID> (Traits::kBypassParamId))
            {
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (points - 1, offset, value) == kResultTrue)
                    bypassed_ = value >= 0.5;
                continue;
            }
            if (tag >= static_cast<ParamID> (Traits::kNumParams))
                continue; // program (handled above) and hidden adapter params
            const auto id = static_cast<ParamId> (tag);
            if (!schedule_.beginLane())
            {
                // Out of lanes (never with today's parameter counts): last value now.
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (points - 1, offset, value) == kResultTrue)
                {
                    currentParams_.set (id, Traits::toPlain (id, value));
                    pushParams_ = true;
                }
                continue;
            }
            for (int32 p = 0; p < points; ++p)
            {
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (p, offset, value) != kResultTrue)
                    continue;
                schedule_.push (offset, static_cast<std::uint32_t> (tag), Traits::toPlain (id, value), span);
            }
        }
    }

    /** Applies scheduled points up to `limit` (absolute block offset). */
    bool applyUpTo (int limit) noexcept
    {
        return schedule_.applyUpTo (limit, [this] (std::uint32_t id, float plain) noexcept {
            currentParams_.set (static_cast<ParamId> (id), plain);
        });
    }

    void applyAllPending() noexcept
    {
        if (applyUpTo (ParamSchedule<>::kNoMorePoints) || pushParams_)
        {
            if (engineReady_)
                engine_.setParameters (currentParams_);
            pushParams_ = false;
        }
    }

    /** Processes wet_[0, chunk) for absolute block samples [chunkStart, chunkStart + chunk). */
    void runEngine (int chunkStart, int chunk) noexcept
    {
        const int chunkEnd = chunkStart + chunk;
        if (bypass_.fullyBypassed())
        {
            // Engine idle while bypassed: keep tracking automation, and reset
            // once so un-bypassing starts clean (no stale tails or envelopes).
            if (applyUpTo (chunkEnd - 1) || pushParams_)
            {
                engine_.setParameters (currentParams_);
                pushParams_ = false;
            }
            if (!engineIdle_)
            {
                engine_.reset();
                engineIdle_ = true;
            }
            return;
        }
        if (engineIdle_)
        {
            // Restarting from reset: keep the output dry until the engine's
            // latency line has refilled, then crossfade.
            bypass_.holdMix (latency_);
            engineIdle_ = false;
        }

        int pos = chunkStart;
        while (pos < chunkEnd)
        {
            if (applyUpTo (pos) || pushParams_)
            {
                engine_.setParameters (currentParams_);
                pushParams_ = false;
            }
            const int end = schedule_.subBlockEnd (pos, chunkEnd);
            const auto local = static_cast<std::size_t> (pos - chunkStart);
            const core::AudioBlock block {wetLeft_.data() + local, wetRight_.data() + local, end - pos};
            engine_.process (block);
            pos = end;
        }
    }

    void readInput (const Steinberg::Vst::AudioBusBuffers* in, bool is64, int start, int count) noexcept
    {
        const auto n = static_cast<std::size_t> (count);
        const int channels = in != nullptr ? static_cast<int> (in->numChannels) : 0;
        const void* const* buffers = nullptr;
        if (in != nullptr)
        {
            buffers = is64 ? reinterpret_cast<const void* const*> (in->channelBuffers64)
                           : reinterpret_cast<const void* const*> (in->channelBuffers32);
        }
        auto channelPtr = [&] (int c) -> const void* {
            if (buffers == nullptr || c >= channels)
                return nullptr;
            return buffers[c];
        };
        const void* left = channelPtr (0);
        // Mono input (or a host passing one channel): both sides get the same signal.
        const void* right = inputLayout_ == InputLayout::mono || channels < 2 ? left : channelPtr (1);
        copyIn (left, is64, start, dryLeft_.data(), n);
        copyIn (right, is64, start, dryRight_.data(), n);
    }

    static void copyIn (const void* source, bool is64, int start, float* dest, std::size_t n) noexcept
    {
        if (source == nullptr)
        {
            for (std::size_t i = 0; i < n; ++i)
                dest[i] = 0.0F;
            return;
        }
        if (is64)
        {
            const double* src = static_cast<const double*> (source) + start;
            for (std::size_t i = 0; i < n; ++i)
                dest[i] = static_cast<float> (src[i]);
        }
        else
        {
            const float* src = static_cast<const float*> (source) + start;
            for (std::size_t i = 0; i < n; ++i)
                dest[i] = src[i];
        }
    }

    void writeOutput (Steinberg::Vst::AudioBusBuffers& out, bool is64, int start, int count) noexcept
    {
        const auto n = static_cast<std::size_t> (count);
        for (Steinberg::int32 c = 0; c < out.numChannels; ++c)
        {
            const float* src = c == 0 ? wetLeft_.data() : wetRight_.data();
            if (is64)
            {
                if (out.channelBuffers64 == nullptr || out.channelBuffers64[c] == nullptr)
                    continue;
                double* dst = out.channelBuffers64[c] + start;
                for (std::size_t i = 0; i < n; ++i)
                    dst[i] = static_cast<double> (src[i]);
            }
            else
            {
                if (out.channelBuffers32 == nullptr || out.channelBuffers32[c] == nullptr)
                    continue;
                float* dst = out.channelBuffers32[c] + start;
                for (std::size_t i = 0; i < n; ++i)
                    dst[i] = src[i];
            }
        }
    }

    static void clearOutputs (Steinberg::Vst::AudioBusBuffers& out, Steinberg::int32 sampleSize,
                              int numSamples) noexcept
    {
        for (Steinberg::int32 c = 0; c < out.numChannels; ++c)
        {
            if (sampleSize == Steinberg::Vst::kSample64)
            {
                if (out.channelBuffers64 != nullptr && out.channelBuffers64[c] != nullptr)
                    for (int s = 0; s < numSamples; ++s)
                        out.channelBuffers64[c][s] = 0.0;
            }
            else if (out.channelBuffers32 != nullptr && out.channelBuffers32[c] != nullptr)
            {
                for (int s = 0; s < numSamples; ++s)
                    out.channelBuffers32[c][s] = 0.0F;
            }
        }
    }

    void updateTail() noexcept
    {
        const double tail = Traits::tailSeconds (currentParams_) * sampleRate_;
        Steinberg::uint32 samples = 0;
        if (tail >= static_cast<double> (0xFFFFFFFEu))
            samples = 0xFFFFFFFEu; // never kInfiniteTail: hosts may sleep the plug-in
        else if (tail > 0.0)
            samples = static_cast<Steinberg::uint32> (tail);
        tailSamples_.store (samples, std::memory_order_relaxed);
    }

    // Parameter state in plain units. Written on the audio thread during
    // processing and by setState (which hosts never call concurrently with
    // process for the same instance).
    Params currentParams_;
    Engine engine_;
    bool engineReady_ = false;
    bool engineIdle_ = false;
    bool pushParams_ = true;
    static constexpr int kAdoptNextProgram = -2;
    int appliedProgram_ = -1; // factory program last applied (-1 none yet)
    std::atomic<bool> engineDirty_ {false};
    double sampleRate_ = 48000.0;
    int maxBlock_ = 1;
    int latency_ = 0;
    bool bypassed_ = false;
    InputLayout inputLayout_ = InputLayout::stereo;
    std::atomic<Steinberg::uint32> tailSamples_ {0};

    ParamSchedule<> schedule_;
    LatencyBypass bypass_;
    std::vector<float> dryLeft_;
    std::vector<float> dryRight_;
    std::vector<float> wetLeft_;
    std::vector<float> wetRight_;
};

template <EffectTraits Traits>
Steinberg::FUID EffectProcessor<Traits>::cid (Traits::kProcessorFuid[0], Traits::kProcessorFuid[1],
                                              Traits::kProcessorFuid[2], Traits::kProcessorFuid[3]);

} // namespace kraftonic::formats::vst3::effect
