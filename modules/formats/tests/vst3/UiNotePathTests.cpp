// SPDX-License-Identifier: MIT
//
// UI-to-audio note path through the shared VST3 processor: an
// IMessage from the controller becomes a sounding note at the next
// block, note-off stops it, and the held-note words reach the host's output
// parameter queue at ~30 Hz. Uses the SelfTest traits (HasUiKeyboard).

#include "SelfTestTraits.h"

#include "kraftonic/formats/HeldNotes.h"
#include "kraftonic/formats/vst3/Vst3Processor.h"

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
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

using kraftonic::formats::HeldNotes;
using kraftonic::formats::selftest::SelfTestTraits;
using Processor = kraftonic::formats::vst3::Processor<SelfTestTraits>;

namespace
{
constexpr int kBlock = 256;

std::atomic<bool> gTrackAllocations {false};
std::atomic<int> gAllocations {0};

void* allocate (std::size_t size)
{
    if (gTrackAllocations.load (std::memory_order_relaxed))
        gAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* result = std::malloc (size == 0 ? 1 : size))
        return result;
    throw std::bad_alloc();
}

class OffsetEngine
{
public:
    void prepare (const kraftonic::core::ProcessSetup&) noexcept {}
    void reset() noexcept {}
    void setParameters (const kraftonic::formats::selftest::SelfTestParameters& params) noexcept
    {
        value_ = params.get (kraftonic::formats::selftest::ParamId::outputGain);
    }
    void process (const kraftonic::core::AudioBlock& block,
                  const kraftonic::core::Event* events, int eventCount) noexcept
    {
        for (int i = 0; i < block.numSamples; ++i)
        {
            for (int event = 0; event < eventCount; ++event)
            {
                if (events[event].sampleOffset == i
                    && events[event].type == kraftonic::core::Event::Type::noteOn)
                {
                    marker_ = 100.0F;
                }
            }
            block.left[i] = value_ + marker_;
            block.right[i] = value_ + marker_;
        }
    }
    int activeVoiceCount() const noexcept { return 0; }

private:
    float value_ = -6.0F;
    float marker_ = 0.0F;
};

struct OffsetTraits : SelfTestTraits
{
    using Engine = OffsetEngine;
};

using OffsetProcessor = kraftonic::formats::vst3::Processor<OffsetTraits>;

class TempoEngine : public kraftonic::formats::selftest::SelfTestEngine
{
public:
    void setTempo (double tempo) noexcept
    {
        lastTempo = tempo;
        tempoCallOrder = ++callOrder;
        ++tempoCalls;
    }

    void setTransport (bool playing, bool positionValid, double ppq) noexcept
    {
        lastPlaying = playing;
        lastPositionValid = positionValid;
        lastPpq = ppq;
        ++transportCalls;
    }

    void setParameters (const kraftonic::formats::selftest::SelfTestParameters& params) noexcept
    {
        kraftonic::formats::selftest::SelfTestEngine::setParameters (params);
        parametersCallOrder = ++callOrder;
    }

    inline static bool lastPlaying = false;
    inline static bool lastPositionValid = false;
    inline static double lastPpq = 0.0;
    inline static int transportCalls = 0;
    inline static double lastTempo = 0.0;
    inline static int tempoCalls = 0;
    inline static int callOrder = 0;
    inline static int tempoCallOrder = 0;
    inline static int parametersCallOrder = 0;
};

struct TempoTraits : SelfTestTraits
{
    using Engine = TempoEngine;
};

using TempoProcessor = kraftonic::formats::vst3::Processor<TempoTraits>;

void addChange (Steinberg::Vst::ParameterChanges& changes, Steinberg::Vst::ParamID id,
                Steinberg::int32 offset, Steinberg::Vst::ParamValue value)
{
    Steinberg::int32 index = 0;
    Steinberg::Vst::IParamValueQueue* queue = changes.addParameterData (id, index);
    REQUIRE (queue != nullptr);
    Steinberg::int32 pointIndex = 0;
    REQUIRE (queue->addPoint (offset, value, pointIndex) == Steinberg::kResultTrue);
}

struct OffsetHarness
{
    OffsetHarness()
    {
        processor = Steinberg::owned (new OffsetProcessor());
        REQUIRE (processor->initialize (nullptr) == Steinberg::kResultOk);
        Steinberg::Vst::ProcessSetup setup {Steinberg::Vst::kRealtime,
                                            Steinberg::Vst::kSample32, kBlock, 48000.0};
        REQUIRE (processor->setupProcessing (setup) == Steinberg::kResultOk);
        REQUIRE (processor->setActive (true) == Steinberg::kResultOk);
        processor->setProcessing (true);
    }

    ~OffsetHarness()
    {
        processor->setProcessing (false);
        processor->setActive (false);
        processor->terminate();
    }

    void run (int samples, Steinberg::Vst::IParameterChanges* changes = nullptr,
              Steinberg::Vst::IEventList* events = nullptr)
    {
        float* channels[2] = {left.data(), right.data()};
        Steinberg::Vst::AudioBusBuffers bus {};
        bus.numChannels = 2;
        bus.channelBuffers32 = channels;
        Steinberg::Vst::ProcessData data {};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = samples;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputParameterChanges = changes;
        data.inputEvents = events;
        REQUIRE (processor->process (data) == Steinberg::kResultOk);
    }

    Steinberg::IPtr<OffsetProcessor> processor;
    std::array<float, kBlock> left {};
    std::array<float, kBlock> right {};
};

struct Harness
{
    Harness()
    {
        processor = Steinberg::owned (new Processor());
        processor->initialize (nullptr);
        Steinberg::Vst::ProcessSetup setup {Steinberg::Vst::kRealtime,
                                            Steinberg::Vst::kSample32, kBlock, 48000.0};
        processor->setupProcessing (setup);
        processor->setActive (true);
        processor->setProcessing (true);
    }

    ~Harness()
    {
        processor->setProcessing (false);
        processor->setActive (false);
        processor->terminate();
    }

    Harness (const Harness&) = delete;
    Harness& operator= (const Harness&) = delete;

    /** One block; returns its peak and leaves the output params in `out`. */
    float run()
    {
        out.clearQueue();
        Steinberg::Vst::ProcessData data {};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = kBlock;
        data.numOutputs = 1;
        float* channels[2] = {left.data(), right.data()};
        Steinberg::Vst::AudioBusBuffers bus {};
        bus.numChannels = 2;
        bus.channelBuffers32 = channels;
        data.outputs = &bus;
        data.outputParameterChanges = &out;
        processor->process (data);
        float peak = 0.0F;
        for (float s : left)
            peak = std::fabs (s) > peak ? std::fabs (s) : peak;
        return peak;
    }

    bool send (int note, double velocity, bool on)
    {
        Steinberg::IPtr<Steinberg::Vst::HostMessage> message
            = Steinberg::owned (new Steinberg::Vst::HostMessage());
        message->setMessageID (kraftonic::formats::vst3::kUiNoteMessageId);
        message->getAttributes()->setInt ("note", note);
        message->getAttributes()->setFloat ("velocity", velocity);
        message->getAttributes()->setInt ("on", on ? 1 : 0);
        return processor->notify (message) == Steinberg::kResultOk;
    }

    /** Last held-note word value reported for `word` (-1 = none this block). */
    int reportedWord (int word)
    {
        const auto tag = static_cast<Steinberg::Vst::ParamID> (
            SelfTestTraits::kHeldNotesParamIdBase + static_cast<std::uint32_t> (word));
        for (Steinberg::int32 i = 0; i < out.getParameterCount(); ++i)
        {
            auto* queue = out.getParameterData (i);
            if (queue == nullptr || queue->getParameterId() != tag || queue->getPointCount() < 1)
                continue;
            Steinberg::int32 offset = 0;
            Steinberg::Vst::ParamValue value = 0.0;
            queue->getPoint (queue->getPointCount() - 1, offset, value);
            return HeldNotes::wordFromNormalized (value);
        }
        return -1;
    }

    Steinberg::IPtr<Processor> processor;
    Steinberg::Vst::ParameterChanges out {64};
    std::vector<float> left = std::vector<float> (kBlock, 0.0F);
    std::vector<float> right = std::vector<float> (kBlock, 0.0F);
};
} // namespace

void* operator new (std::size_t size)
{
    return allocate (size);
}

void* operator new[] (std::size_t size)
{
    return allocate (size);
}

void operator delete (void* ptr) noexcept
{
    std::free (ptr);
}

void operator delete[] (void* ptr) noexcept
{
    std::free (ptr);
}

void operator delete (void* ptr, std::size_t /*size*/) noexcept
{
    std::free (ptr);
}

void operator delete[] (void* ptr, std::size_t /*size*/) noexcept
{
    std::free (ptr);
}

TEST_CASE ("UI note message plays and stops the engine", "[ui-notes]")
{
    Harness harness {};
    CHECK (harness.run() == 0.0F);
    REQUIRE (harness.send (69, 0.9, true));
    float peak = 0.0F;
    for (int i = 0; i < 8; ++i)
        peak = harness.run();
    CHECK (peak > 0.01F);
    REQUIRE (harness.send (69, 0.0, false));
    for (int i = 0; i < 200; ++i) // release tail (0.15 s) and then silence
        peak = harness.run();
    CHECK (peak < 1.0e-4F);
}

TEST_CASE ("UI note messages are validated", "[ui-notes]")
{
    Harness harness {};
    CHECK_FALSE (harness.send (128, 0.5, true));
    CHECK_FALSE (harness.send (-1, 0.5, true));
    Steinberg::IPtr<Steinberg::Vst::HostMessage> other
        = Steinberg::owned (new Steinberg::Vst::HostMessage());
    other->setMessageID ("SomethingElse");
    CHECK (harness.processor->notify (other) != Steinberg::kResultOk);
}

TEST_CASE ("held notes reach the output parameters at ~30 Hz", "[ui-notes]")
{
    Harness harness {};
    REQUIRE (harness.send (64, 0.8, true)); // word 4, bit 0
    REQUIRE (harness.send (65, 0.8, true)); // word 4, bit 1
    int word4 = -1;
    int blocks = 0;
    while (word4 < 0 && blocks < 20)
    {
        harness.run();
        word4 = harness.reportedWord (4);
        ++blocks;
    }
    CHECK (blocks <= 7); // 1600-sample tick = 6.25 blocks of 256
    CHECK (word4 == 0x3);
    REQUIRE (harness.send (64, 0.0, false));
    word4 = -1;
    for (int i = 0; i < 8 && word4 < 0; ++i)
    {
        harness.run();
        word4 = harness.reportedWord (4);
    }
    CHECK (word4 == 0x2);
}

TEST_CASE ("contract automation reaches instrument processors at sample offsets",
           "[automation][sample-offset]")
{
    const auto parameter = static_cast<Steinberg::Vst::ParamID> (
        kraftonic::formats::selftest::ParamId::outputGain);
    const double low = kraftonic::formats::selftest::toNormalized (
        kraftonic::formats::selftest::ParamId::outputGain, -24.0F);
    const double high = kraftonic::formats::selftest::toNormalized (
        kraftonic::formats::selftest::ParamId::outputGain, 3.0F);

    OffsetHarness oneBlock {};
    Steinberg::Vst::ParameterChanges changes {};
    addChange (changes, parameter, 64, low);
    addChange (changes, parameter, 160, high);
    oneBlock.run (kBlock, &changes);
    for (int i = 0; i < 64; ++i)
        CHECK (oneBlock.left[static_cast<std::size_t> (i)] == -6.0F);
    for (int i = 64; i < 160; ++i)
        CHECK (oneBlock.left[static_cast<std::size_t> (i)] == -24.0F);
    for (int i = 160; i < kBlock; ++i)
        CHECK (oneBlock.left[static_cast<std::size_t> (i)] == 3.0F);
    OffsetHarness splitBlocks {};
    std::array<float, kBlock> split {};
    for (int block = 0; block < 4; ++block)
    {
        Steinberg::Vst::ParameterChanges blockChanges {};
        if (block == 1)
            addChange (blockChanges, parameter, 0, low);
        if (block == 2)
            addChange (blockChanges, parameter, 32, high);
        splitBlocks.run (64, blockChanges.getParameterCount() > 0 ? &blockChanges : nullptr);
        for (int i = 0; i < 64; ++i)
            split[static_cast<std::size_t> (block * 64 + i)]
                = splitBlocks.left[static_cast<std::size_t> (i)];
    }
    CHECK (split == oneBlock.left);
}

TEST_CASE ("automation sub-blocks preserve MIDI sample offsets", "[automation][midi]")
{
    OffsetHarness harness {};
    Steinberg::Vst::ParameterChanges changes {};
    addChange (changes, static_cast<Steinberg::Vst::ParamID> (
                   kraftonic::formats::selftest::ParamId::outputGain),
               64, kraftonic::formats::selftest::toNormalized (
                       kraftonic::formats::selftest::ParamId::outputGain, -24.0F));
    Steinberg::Vst::EventList events {};
    Steinberg::Vst::Event note {};
    note.type = Steinberg::Vst::Event::kNoteOnEvent;
    note.sampleOffset = 100;
    note.noteOn.pitch = 60;
    note.noteOn.velocity = 0.8F;
    REQUIRE (events.addEvent (note) == Steinberg::kResultTrue);

    harness.run (kBlock, &changes, &events);
    CHECK (harness.left[63] == -6.0F);
    CHECK (harness.left[64] == -24.0F);
    CHECK (harness.left[99] == -24.0F);
    CHECK (harness.left[100] == 76.0F);
}

TEST_CASE ("automation boundary overflow coalesces at the final retained boundary",
           "[automation][overflow][realtime]")
{
    constexpr int points = 65;
    const auto parameter = static_cast<Steinberg::Vst::ParamID> (
        kraftonic::formats::selftest::ParamId::outputGain);
    OffsetHarness harness {};
    Steinberg::Vst::ParameterChanges changes {points};
    std::array<float, points> expected {};
    for (int i = 0; i < points; ++i)
    {
        const double normalized = static_cast<double> (i) / static_cast<double> (points - 1);
        expected[static_cast<std::size_t> (i)] = kraftonic::formats::selftest::toPlain (
            kraftonic::formats::selftest::ParamId::outputGain, normalized);
        addChange (changes, parameter, i, normalized);
    }

    gAllocations.store (0, std::memory_order_relaxed);
    gTrackAllocations.store (true, std::memory_order_relaxed);
    harness.run (kBlock, &changes);
    gTrackAllocations.store (false, std::memory_order_relaxed);

    CHECK (gAllocations.load (std::memory_order_relaxed) == 0);
    for (float sample : harness.left)
    {
        CHECK (std::isfinite (sample));
        CHECK (sample >= -60.0F);
        CHECK (sample <= 6.0F);
    }
    for (int i = 0; i < 63; ++i)
        CHECK (harness.left[static_cast<std::size_t> (i)]
               == expected[static_cast<std::size_t> (i)]);
    for (int i = 63; i < kBlock; ++i)
    {
        const float sample = harness.left[static_cast<std::size_t> (i)];
        CHECK (sample == expected.back());
    }
}

TEST_CASE ("valid host tempo reaches opted-in engines", "[vst3][processor][tempo]")
{
    constexpr int blockSize = 16;
    Steinberg::IPtr<TempoProcessor> processor = Steinberg::owned (new TempoProcessor());
    REQUIRE (processor->initialize (nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup {Steinberg::Vst::kRealtime,
                                        Steinberg::Vst::kSample32, blockSize, 48000.0};
    REQUIRE (processor->setupProcessing (setup) == Steinberg::kResultOk);
    REQUIRE (processor->setActive (true) == Steinberg::kResultOk);
    processor->setProcessing (true);

    std::array<float, blockSize> left {};
    std::array<float, blockSize> right {};
    float* channels[2] = {left.data(), right.data()};
    Steinberg::Vst::AudioBusBuffers bus {};
    bus.numChannels = 2;
    bus.channelBuffers32 = channels;
    auto processWithContext = [&] (Steinberg::Vst::ProcessContext* context)
    {
        Steinberg::Vst::ProcessData data {};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = blockSize;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.processContext = context;
        REQUIRE (processor->process (data) == Steinberg::kResultOk);
    };

    TempoEngine::tempoCalls = 0;
    Steinberg::Vst::ProcessContext context {};
    context.state = Steinberg::Vst::ProcessContext::kTempoValid;
    context.tempo = 137.5;
    processWithContext (&context);
    REQUIRE (TempoEngine::tempoCalls == 1);
    REQUIRE (TempoEngine::lastTempo == 137.5);
    REQUIRE (TempoEngine::tempoCallOrder < TempoEngine::parametersCallOrder);
    REQUIRE (TempoEngine::transportCalls == 1);
    REQUIRE_FALSE (TempoEngine::lastPlaying);
    REQUIRE_FALSE (TempoEngine::lastPositionValid);

    context.state = Steinberg::Vst::ProcessContext::kPlaying
                    | Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
    context.projectTimeMusic = 12.75;
    processWithContext (&context);
    REQUIRE (TempoEngine::lastPlaying);
    REQUIRE (TempoEngine::lastPositionValid);
    REQUIRE (TempoEngine::lastPpq == 12.75);

    context.state = 0;
    processWithContext (&context);
    REQUIRE (TempoEngine::tempoCalls == 1);

    processWithContext (nullptr);
    REQUIRE (TempoEngine::tempoCalls == 1);
    REQUIRE_FALSE (TempoEngine::lastPlaying);
    REQUIRE_FALSE (TempoEngine::lastPositionValid);
    REQUIRE (TempoEngine::lastPpq == 0.0);

    processor->setProcessing (false);
    REQUIRE (processor->setActive (false) == Steinberg::kResultOk);
    REQUIRE (processor->terminate() == Steinberg::kResultOk);
}
