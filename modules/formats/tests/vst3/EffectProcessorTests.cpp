// SPDX-License-Identifier: MIT
//
// Shared EffectProcessor tests.
// Built only with KRAFTONIC_BUILD_PLUGINS: drives the promoted
// kraftonic::formats::vst3::effect::EffectProcessor with a fake effect engine
// and fake traits, with no host — bus layouts, latency, latency-compensated
// bypass, sample-accurate automation, mono input, 64-bit samples, oversized
// blocks, tail and state. Mirrors the coverage of the effect adapter
// it was promoted from.

#include "kraftonic/formats/vst3/effect/BusLayout.h"
#include "kraftonic/formats/vst3/effect/EffectTraits.h"
#include "kraftonic/formats/vst3/effect/Vst3EffectProcessor.h"
#include "../FakeEffect.h"
#include "kraftonic/formats/Preset.h"
#include "kraftonic/formats/vst3/AdapterIds.h"

#include "kraftonic/core/Instrument.h"
#include "kraftonic/core/Parameters.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wshadow"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
using namespace fakeeffect;

using FakeProcessor = kraftonic::formats::vst3::effect::EffectProcessor<FakeEffectTraits>;

/** Owns a processor plus host-side buffers for one stereo-out effect. */
struct Harness
{
    Steinberg::IPtr<FakeProcessor> processor = Steinberg::owned (new FakeProcessor());
    int maxBlock = 256;
    std::vector<float> inL, inR, outL, outR;
    std::vector<double> inL64, inR64, outL64, outR64;

    explicit Harness (Steinberg::Vst::SpeakerArrangement input
                      = Steinberg::Vst::SpeakerArr::kStereo,
                      int block = 256)
    : maxBlock (block)
    {
        REQUIRE (processor->initialize (nullptr) == Steinberg::kResultOk);
        Steinberg::Vst::SpeakerArrangement in = input;
        Steinberg::Vst::SpeakerArrangement out = Steinberg::Vst::SpeakerArr::kStereo;
        REQUIRE (processor->setBusArrangements (&in, 1, &out, 1) == Steinberg::kResultTrue);
        Steinberg::Vst::ProcessSetup setup {Steinberg::Vst::kRealtime,
                                            Steinberg::Vst::kSample32, block, 48000.0};
        REQUIRE (processor->setupProcessing (setup) == Steinberg::kResultOk);
        REQUIRE (processor->setActive (true) == Steinberg::kResultOk);
        REQUIRE (processor->setProcessing (true) == Steinberg::kResultOk);
    }

    ~Harness()
    {
        processor->setProcessing (false);
        processor->setActive (false);
        processor->terminate();
    }

    void run (int n, Steinberg::Vst::IParameterChanges* changes = nullptr, int inputChannels = 2,
              bool rampInput = false)
    {
        inL.assign (static_cast<std::size_t> (n), 0.0F);
        inR.assign (static_cast<std::size_t> (n), 0.0F);
        if (rampInput)
        {
            for (int i = 0; i < n; ++i)
            {
                inL[static_cast<std::size_t> (i)] = static_cast<float> (inputOffset_ + i);
                inR[static_cast<std::size_t> (i)] = static_cast<float> (inputOffset_ + i);
            }
            inputOffset_ += n;
        }
        outL.assign (static_cast<std::size_t> (n), 99.0F);
        outR.assign (static_cast<std::size_t> (n), 99.0F);
        float* ins[2] = {inL.data(), inR.data()};
        float* outs[2] = {outL.data(), outR.data()};
        Steinberg::Vst::AudioBusBuffers inBus {};
        inBus.numChannels = inputChannels;
        inBus.channelBuffers32 = ins;
        Steinberg::Vst::AudioBusBuffers outBus {};
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outs;
        Steinberg::Vst::ProcessData data {};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = n;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inBus;
        data.outputs = &outBus;
        data.inputParameterChanges = changes;
        REQUIRE (processor->process (data) == Steinberg::kResultOk);
    }

    Steinberg::int64 inputOffset_ = 0;
};

void addChange (Steinberg::Vst::ParameterChanges& changes, Steinberg::Vst::ParamID id,
                Steinberg::int32 offset, Steinberg::Vst::ParamValue value)
{
    Steinberg::int32 index = 0;
    Steinberg::Vst::IParamValueQueue* queue = changes.addParameterData (id, index);
    REQUIRE (queue != nullptr);
    Steinberg::int32 pointIndex = 0;
    queue->addPoint (offset, value, pointIndex);
}

} // namespace

TEST_CASE ("effect processor accepts stereo and mono layouts", "[effect][vst3]")
{
    Harness stereo {};
    Steinberg::Vst::SpeakerArrangement in = Steinberg::Vst::SpeakerArr::kMono;
    Steinberg::Vst::SpeakerArrangement out = Steinberg::Vst::SpeakerArr::kStereo;
    CHECK (stereo.processor->setBusArrangements (&in, 1, &out, 1) == Steinberg::kResultTrue);
    Steinberg::Vst::SpeakerArrangement bad = Steinberg::Vst::SpeakerArr::kMono;
    CHECK (stereo.processor->setBusArrangements (&in, 1, &bad, 1) == Steinberg::kResultFalse);
}

TEST_CASE ("effect latency comes from the engine", "[effect][vst3]")
{
    Harness harness {};
    CHECK (harness.processor->getLatencySamples() == kFakeLatency);
    CHECK (harness.processor->getTailSamples() == 0); // this engine has no tail
}

TEST_CASE ("effect automation lands on its sample offset", "[effect][vst3]")
{
    Harness harness {};
    harness.run (64); // prime the delay line with silence
    Steinberg::Vst::ParameterChanges changes {};
    // trim 0 dB -> +6 dB at sample 32: DC steps from 0 to 6 there. The mode
    // switch 0 -> 2 at sample 48 triples it (DC = trim * (mode + 1)).
    addChange (changes, static_cast<Steinberg::Vst::ParamID> (ParamId::trim), 32,
               toNormalized (ParamId::trim, 6.0F));
    addChange (changes, static_cast<Steinberg::Vst::ParamID> (ParamId::mode), 48,
               toNormalized (ParamId::mode, 2.0F));
    harness.run (96, &changes);
    // The fake engine adds the parameter as DC, so each step lands exactly
    // on its automation offset (the engine latency only delays the signal).
    for (int i = 0; i < 96; ++i)
    {
        const float expected = i < 32 ? 0.0F : (i < 48 ? 6.0F : 18.0F);
        INFO ("sample " << i);
        CHECK (harness.outL[static_cast<std::size_t> (i)]
               == Catch::Approx (expected).margin (1e-3));
    }
}

TEST_CASE ("effect program change applies the preset; automation wins", "[effect][vst3]")
{
    Harness harness {};
    Steinberg::Vst::ParameterChanges changes {};
    addChange (changes, static_cast<Steinberg::Vst::ParamID> (FakeEffectTraits::kProgramParamId),
               0, 0.0); // the only preset sets trim to +6 dB
    addChange (changes, static_cast<Steinberg::Vst::ParamID> (ParamId::trim), 10,
               toNormalized (ParamId::trim, -6.0F));
    harness.run (64, &changes);
    // Preset base (+6) holds, then explicit automation (-6) wins. The point
    // at offset 10 lands at the next 16-sample sub-block boundary.
    CHECK (harness.outL[0] == Catch::Approx (6.0F).margin (1e-3));
    CHECK (harness.outL[10] == Catch::Approx (6.0F).margin (1e-3));
    CHECK (harness.outL[16] == Catch::Approx (-6.0F).margin (1e-3));
}

TEST_CASE ("effect bypass keeps timing aligned", "[effect][vst3]")
{
    // Bypass on with a ramp input: once the 10 ms crossfade completes, the
    // output is the dry signal delayed by exactly the engine latency.
    Harness harness {};
    Steinberg::Vst::ParameterChanges on {};
    addChange (on, static_cast<Steinberg::Vst::ParamID> (FakeEffectTraits::kBypassParamId), 0, 1.0);
    for (int b = 0; b < 8; ++b)
        harness.run (64, &on, 2, true);
    // 512 samples ran; the fade finished at sample 480 (block 7, index 32).
    const Steinberg::int64 lastBlockStart = harness.inputOffset_ - 64;
    for (int i = 32; i < 64; ++i)
    {
        const float expected = static_cast<float> (lastBlockStart + i - kFakeLatency);
        INFO ("sample " << i);
        CHECK (harness.outL[static_cast<std::size_t> (i)]
               == Catch::Approx (expected).margin (1e-2));
    }
    CHECK (harness.processor->isBypassed());
}

TEST_CASE ("effect handles mono input, 64-bit samples and oversized blocks", "[effect][vst3]")
{
    Harness mono {Steinberg::Vst::SpeakerArr::kMono, 64};
    mono.run (64, nullptr, 1);
    CHECK (mono.outL[63] == Catch::Approx (mono.outR[63]));

    Harness harness {};
    harness.inL64.assign (64, 0.5);
    harness.inR64.assign (64, 0.5);
    harness.outL64.assign (64, 99.0);
    harness.outR64.assign (64, 99.0);
    double* ins[2] = {harness.inL64.data(), harness.inR64.data()};
    double* outs[2] = {harness.outL64.data(), harness.outR64.data()};
    Steinberg::Vst::AudioBusBuffers inBus {};
    inBus.numChannels = 2;
    inBus.channelBuffers64 = ins;
    Steinberg::Vst::AudioBusBuffers outBus {};
    outBus.numChannels = 2;
    outBus.channelBuffers64 = outs;
    Steinberg::Vst::ProcessData data {};
    data.symbolicSampleSize = Steinberg::Vst::kSample64;
    data.numSamples = 64;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inBus;
    data.outputs = &outBus;
    REQUIRE (harness.processor->process (data) == Steinberg::kResultOk);
    CHECK (std::isfinite (harness.outL64[63]));

    Harness big {Steinberg::Vst::SpeakerArr::kStereo, 32};
    big.run (200); // larger than maxSamplesPerBlock: processed in chunks
    CHECK (std::isfinite (big.outL[199]));
}

TEST_CASE ("effect processor state round trip", "[effect][vst3]")
{
    Harness harness {};
    Steinberg::Vst::ParameterChanges changes {};
    addChange (changes, static_cast<Steinberg::Vst::ParamID> (ParamId::trim), 0,
               toNormalized (ParamId::trim, 6.0F));
    addChange (changes, static_cast<Steinberg::Vst::ParamID> (ParamId::mode), 0,
               toNormalized (ParamId::mode, 2.0F));
    harness.run (32, &changes);
    const FakeEffectParameters before = harness.processor->currentParameters();
    REQUIRE (before.get (ParamId::trim) == Catch::Approx (6.0F));

    Steinberg::IPtr<Steinberg::MemoryStream> stream = Steinberg::owned (new Steinberg::MemoryStream());
    REQUIRE (harness.processor->getState (stream) == Steinberg::kResultOk);

    Steinberg::IPtr<FakeProcessor> other = Steinberg::owned (new FakeProcessor());
    REQUIRE (other->initialize (nullptr) == Steinberg::kResultOk);
    stream->seek (0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE (other->setState (stream) == Steinberg::kResultOk);
    for (const auto& info : kParamInfo)
    {
        INFO (info.key);
        CHECK (other->currentParameters().get (info.id) == before.get (info.id));
    }
    other->terminate();

    // Garbage keeps defaults and reports failure.
    Steinberg::IPtr<Steinberg::MemoryStream> junk = Steinberg::owned (new Steinberg::MemoryStream());
    const char bytes[] = "not an effect state";
    Steinberg::int32 written = 0;
    junk->write (const_cast<char*> (bytes), sizeof (bytes), &written);
    junk->seek (0, Steinberg::IBStream::kIBSeekSet, nullptr);
    CHECK (other->setState (junk) == Steinberg::kResultFalse);
    CHECK (other->currentParameters().get (ParamId::trim)
           == FakeEffectParameters::defaults().get (ParamId::trim));
}
