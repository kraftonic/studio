// SPDX-License-Identifier: MIT
//
// CLAP adapter tests: drive kraftonic::formats::clap::Plugin like a host,
// with no host and no VST3 SDK. Parameter info and value text, the state
// (the VST3 payload plus bypass), event order and sample offsets for an
// instrument, MIDI translation, params flush with editor edits, and an
// effect's latency, sample-accurate automation and bypass.

#include "../FakeEffect.h"
#include "SelfTestTraits.h"

#include "kraftonic/formats/ParamText.h"
#include "kraftonic/formats/StateCodec.h"
#include "kraftonic/formats/clap/ClapPlugin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using kraftonic::core::Event;
using kraftonic::formats::clap::EditKind;
using kraftonic::formats::selftest::ParamId;
using kraftonic::formats::selftest::SelfTestParameters;
using kraftonic::formats::selftest::SelfTestTraits;

/** Records what the adapter hands the engine: one entry per process call. */
struct RecordingEngine
{
    struct Call
    {
        int numSamples = 0;
        float gain = 0.0F;
        std::vector<Event> events;
    };
    static inline std::vector<Call> calls;
    float gain = 0.0F;

    void prepare (const kraftonic::core::ProcessSetup&) { calls.clear(); }
    void reset() noexcept {}
    void setParameters (const SelfTestParameters& p) noexcept { gain = p.get (ParamId::outputGain); }
    void process (const kraftonic::core::AudioBlock& block, const Event* events, int numEvents) noexcept
    {
        Call call;
        call.numSamples = block.numSamples;
        call.gain = gain;
        call.events.assign (events, events + numEvents);
        calls.push_back (call);
        for (int i = 0; i < block.numSamples; ++i)
            block.left[i] = block.right[i] = 0.25F;
    }
};

struct RecordingTraits : SelfTestTraits
{
    using Engine = RecordingEngine;
};

using Instrument = kraftonic::formats::clap::Plugin<SelfTestTraits>;
using Recorder = kraftonic::formats::clap::Plugin<RecordingTraits>;
using Effect = kraftonic::formats::clap::Plugin<fakeeffect::FakeEffectTraits>;

static_assert (!Instrument::kIsEffect);
static_assert (Effect::kIsEffect);

const clap_host kHost {CLAP_VERSION_INIT, nullptr, "test", "", "", "1",
                       [] (const clap_host*, const char*) -> const void* { return nullptr; },
                       [] (const clap_host*) {}, [] (const clap_host*) {}, [] (const clap_host*) {}};
const clap_plugin_descriptor kDescriptor {CLAP_VERSION_INIT, "com.kraftonic.test", "Test", "Kraftonic", "", "", "",
                                          "1.0.0", "", nullptr};

/** A host's event lists: events in, events out. */
struct EventList
{
    std::vector<std::vector<std::uint8_t>> in;
    std::vector<std::vector<std::uint8_t>> out;
    clap_input_events inList {this, [] (const clap_input_events* l) {
                                  return static_cast<std::uint32_t> (static_cast<EventList*> (l->ctx)->in.size());
                              },
                              [] (const clap_input_events* l, std::uint32_t i) {
                                  return reinterpret_cast<const clap_event_header*> (
                                      static_cast<EventList*> (l->ctx)->in[i].data());
                              }};
    clap_output_events outList {this, [] (const clap_output_events* l, const clap_event_header* e) {
                                    const auto* bytes = reinterpret_cast<const std::uint8_t*> (e);
                                    static_cast<EventList*> (l->ctx)->out.emplace_back (bytes, bytes + e->size);
                                    return true;
                                }};

    template <typename T>
    void add (const T& event)
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*> (&event);
        in.emplace_back (bytes, bytes + sizeof (T));
    }

    void param (std::uint32_t time, clap_id id, double value, std::uint16_t space = CLAP_CORE_EVENT_SPACE_ID)
    {
        clap_event_param_value e {};
        e.header = {sizeof (e), time, space, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = value;
        add (e);
    }

    void note (std::uint32_t time, std::uint16_t type, int key, double velocity)
    {
        clap_event_note e {};
        e.header = {sizeof (e), time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
        e.note_id = -1;
        e.port_index = 0;
        e.channel = 0;
        e.key = static_cast<std::int16_t> (key);
        e.velocity = velocity;
        add (e);
    }

    void midi (std::uint32_t time, std::uint8_t status, std::uint8_t data1, std::uint8_t data2)
    {
        clap_event_midi e {};
        e.header = {sizeof (e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        e.port_index = 0;
        e.data[0] = status;
        e.data[1] = data1;
        e.data[2] = data2;
        add (e);
    }
};

/** Stereo buffers and a clap_process for one block. */
struct Block
{
    std::vector<float> inL, inR, outL, outR;
    float* ins[2] {};
    float* outs[2] {};
    clap_audio_buffer input {};
    clap_audio_buffer output {};
    clap_process process {};

    Block (int frames, EventList& events, bool withInput)
    : inL (static_cast<std::size_t> (frames)), inR (inL.size()), outL (inL.size()), outR (inL.size())
    {
        ins[0] = inL.data();
        ins[1] = inR.data();
        outs[0] = outL.data();
        outs[1] = outR.data();
        input.data32 = ins;
        input.channel_count = 2;
        output.data32 = outs;
        output.channel_count = 2;
        process.steady_time = -1;
        process.frames_count = static_cast<std::uint32_t> (frames);
        process.audio_inputs = withInput ? &input : nullptr;
        process.audio_inputs_count = withInput ? 1u : 0u;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1;
        process.in_events = &events.inList;
        process.out_events = &events.outList;
    }
};

template <typename P>
P* makePlugin (int maxFrames = 512)
{
    auto* plugin = new P (&kHost, &kDescriptor);
    const clap_plugin* p = plugin->clapPlugin();
    REQUIRE (p->init (p));
    REQUIRE (p->activate (p, 48000.0, 1, static_cast<std::uint32_t> (maxFrames)));
    REQUIRE (p->start_processing (p));
    return plugin;
}

template <typename P>
void destroy (P* plugin)
{
    const clap_plugin* p = plugin->clapPlugin();
    p->stop_processing (p);
    p->deactivate (p);
    p->destroy (p);
}

template <typename T>
const T* extension (const clap_plugin* p, const char* id)
{
    return static_cast<const T*> (p->get_extension (p, id));
}
} // namespace

TEST_CASE ("clap: parameter info mirrors the contract table", "[clap]")
{
    auto* plugin = makePlugin<Instrument>();
    const clap_plugin* p = plugin->clapPlugin();
    const auto* params = extension<clap_plugin_params> (p, CLAP_EXT_PARAMS);
    REQUIRE (params != nullptr);
    REQUIRE (params->count (p) == static_cast<std::uint32_t> (SelfTestTraits::kNumParams) + 1u);
    for (int i = 0; i < SelfTestTraits::kNumParams; ++i)
    {
        const auto& meta = SelfTestTraits::paramTable()[i];
        clap_param_info info {};
        REQUIRE (params->get_info (p, static_cast<std::uint32_t> (i), &info));
        INFO (meta.key);
        CHECK (info.id == static_cast<clap_id> (meta.id));
        CHECK (std::string (info.name) == meta.name);
        CHECK ((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0);
        CHECK (((info.flags & CLAP_PARAM_IS_STEPPED) != 0) == (meta.stepCount > 0));
        CHECK (info.min_value == 0.0);
        CHECK (info.max_value == (meta.stepCount > 0 ? static_cast<double> (meta.stepCount) : 1.0));
        // Defaults: the step index for stepped values, normalized otherwise (the VST3 plain convention).
        const double normalized = SelfTestTraits::toNormalized (meta.id, meta.defaultValue);
        CHECK (info.default_value
               == Catch::Approx (meta.stepCount > 0 ? normalized * meta.stepCount : normalized));
        double value = -1.0;
        REQUIRE (params->get_value (p, info.id, &value));
        CHECK (value == Catch::Approx (info.default_value));
    }
    clap_param_info bypass {};
    REQUIRE (params->get_info (p, static_cast<std::uint32_t> (SelfTestTraits::kNumParams), &bypass));
    CHECK (bypass.id == SelfTestTraits::kBypassParamId);
    CHECK ((bypass.flags & CLAP_PARAM_IS_BYPASS) != 0);
    CHECK ((bypass.flags & CLAP_PARAM_IS_STEPPED) != 0);
    clap_param_info none {};
    CHECK_FALSE (params->get_info (p, params->count (p), &none));
    double value = 0.0;
    CHECK_FALSE (params->get_value (p, 12345u, &value));

    const auto* notes = extension<clap_plugin_note_ports> (p, CLAP_EXT_NOTE_PORTS);
    REQUIRE (notes != nullptr);
    CHECK (notes->count (p, true) == 1);
    CHECK (notes->count (p, false) == 0);
    const auto* audio = extension<clap_plugin_audio_ports> (p, CLAP_EXT_AUDIO_PORTS);
    CHECK (audio->count (p, true) == 0);
    CHECK (audio->count (p, false) == 1);
    CHECK (p->get_extension (p, CLAP_EXT_GUI) == nullptr); // NoEditor
    destroy (plugin);
}

TEST_CASE ("clap: value text uses the product's formatting both ways", "[clap]")
{
    auto* plugin = makePlugin<Instrument>();
    const clap_plugin* p = plugin->clapPlugin();
    const auto* params = extension<clap_plugin_params> (p, CLAP_EXT_PARAMS);
    for (int i = 0; i < SelfTestTraits::kNumParams; ++i)
    {
        const auto& meta = SelfTestTraits::paramTable()[i];
        const double top = meta.stepCount > 0 ? meta.stepCount : 1.0;
        for (double v : {0.0, 0.25 * top, 0.5 * top, top})
        {
            const double value = meta.stepCount > 0 ? std::round (v) : v;
            char text[64] {};
            char expected[64] {};
            REQUIRE (params->value_to_text (p, static_cast<clap_id> (meta.id), value, text, sizeof (text)));
            kraftonic::formats::formatParamText<SelfTestTraits> (meta.id, meta.stepCount > 0 ? value / top : value,
                                                                 expected, sizeof (expected));
            INFO (meta.key << " " << value << " -> " << text);
            CHECK (std::string (text) == expected);
            double back = -1.0;
            REQUIRE (params->text_to_value (p, static_cast<clap_id> (meta.id), text, &back));
            char again[64] {};
            REQUIRE (params->value_to_text (p, static_cast<clap_id> (meta.id), back, again, sizeof (again)));
            CHECK (std::string (again) == text);
        }
    }
    char text[16] {};
    REQUIRE (params->value_to_text (p, SelfTestTraits::kBypassParamId, 1.0, text, sizeof (text)));
    CHECK (std::string (text) == "On");
    double value = 0.0;
    CHECK (params->text_to_value (p, SelfTestTraits::kBypassParamId, "off", &value));
    CHECK (value == 0.0);
    CHECK_FALSE (params->text_to_value (p, SelfTestTraits::kBypassParamId, "maybe", &value));
    char tiny[4] {};
    REQUIRE (params->value_to_text (p, static_cast<clap_id> (ParamId::outputGain), 0.5, tiny, sizeof (tiny)));
    CHECK (std::strlen (tiny) < sizeof (tiny)); // truncated, always terminated
    destroy (plugin);
}

TEST_CASE ("clap: state is the VST3 payload plus the bypass switch", "[clap]")
{
    auto* plugin = makePlugin<Instrument>();
    const clap_plugin* p = plugin->clapPlugin();
    EventList events;
    events.param (0, static_cast<clap_id> (ParamId::outputGain), 0.25);
    events.param (0, static_cast<clap_id> (ParamId::wave), 2.0);
    events.param (0, SelfTestTraits::kBypassParamId, 1.0);
    Block block (64, events, false);
    p->process (p, &block.process);

    std::uint8_t bytes[kraftonic::formats::kMaxStateBytes + 8] {};
    const std::size_t used = plugin->saveState (bytes, sizeof (bytes));
    // What the VST3 processor's getState writes for the same values.
    SelfTestParameters expected = SelfTestParameters::defaults();
    expected.set (ParamId::outputGain, SelfTestTraits::toPlain (ParamId::outputGain, 0.25));
    expected.set (ParamId::wave, 2.0F);
    std::uint8_t vst3[kraftonic::formats::kMaxStateBytes] {};
    const std::size_t vst3Size = kraftonic::formats::encodeState<SelfTestTraits> (expected, vst3, sizeof (vst3));
    REQUIRE (vst3Size > 0);
    REQUIRE (used == vst3Size + 8);
    CHECK (std::memcmp (bytes, vst3, vst3Size) == 0);
    CHECK (kraftonic::formats::state_detail::readF64LE (bytes + vst3Size) == 1.0);

    // A new instance loads it (through the clap_istream path, 7 bytes at a time).
    auto* other = makePlugin<Instrument>();
    const clap_plugin* q = other->clapPlugin();
    struct Reader
    {
        const std::uint8_t* data;
        std::size_t size;
        std::size_t at = 0;
    } reader {bytes, used};
    clap_istream stream {&reader, [] (const clap_istream* s, void* buffer, std::uint64_t size) -> std::int64_t {
                             auto* r = static_cast<Reader*> (s->ctx);
                             std::size_t n = std::min<std::size_t> ({static_cast<std::size_t> (size), r->size - r->at, 7});
                             std::memcpy (buffer, r->data + r->at, n);
                             r->at += n;
                             return static_cast<std::int64_t> (n);
                         }};
    const auto* state = extension<clap_plugin_state> (q, CLAP_EXT_STATE);
    REQUIRE (state->load (q, &stream));
    double value = 0.0;
    const auto* params = extension<clap_plugin_params> (q, CLAP_EXT_PARAMS);
    REQUIRE (params->get_value (q, static_cast<clap_id> (ParamId::outputGain), &value));
    CHECK (value == Catch::Approx (0.25).margin (1e-6));
    REQUIRE (params->get_value (q, static_cast<clap_id> (ParamId::wave), &value));
    CHECK (value == 2.0);
    REQUIRE (params->get_value (q, SelfTestTraits::kBypassParamId, &value));
    CHECK (value == 1.0);
    std::uint8_t again[kraftonic::formats::kMaxStateBytes + 8] {};
    REQUIRE (other->saveState (again, sizeof (again)) == used);
    CHECK (std::memcmp (again, bytes, used) == 0);

    // A VST3 processor chunk (no bypass) loads too: bypass off.
    REQUIRE (other->loadState (vst3, vst3Size));
    REQUIRE (params->get_value (q, SelfTestTraits::kBypassParamId, &value));
    CHECK (value == 0.0);
    REQUIRE (params->get_value (q, static_cast<clap_id> (ParamId::wave), &value));
    CHECK (value == 2.0);

    // Corrupt data: false, and the defaults.
    std::uint8_t corrupt[16] {1, 2, 3};
    CHECK_FALSE (other->loadState (corrupt, sizeof (corrupt)));
    REQUIRE (params->get_value (q, static_cast<clap_id> (ParamId::wave), &value));
    CHECK (value == 0.0);
    destroy (other);
    destroy (plugin);
}

TEST_CASE ("clap: an instrument block splits at parameter changes and keeps note offsets", "[clap]")
{
    auto* plugin = makePlugin<Recorder>();
    const clap_plugin* p = plugin->clapPlugin();
    const auto gain = static_cast<clap_id> (ParamId::outputGain);
    EventList events;
    events.note (10, CLAP_EVENT_NOTE_ON, 60, 0.5);
    events.param (100, gain, 1.0);
    events.note (100, CLAP_EVENT_NOTE_ON, 64, 1.0);
    events.note (200, CLAP_EVENT_NOTE_OFF, 60, 0.0);
    events.param (300, gain, 0.0);
    events.param (300, 0x7777u, 0.5);                                  // unknown id: ignored
    events.param (400, gain, 0.5, static_cast<std::uint16_t> (1234)); // other space: ignored
    Block block (512, events, false);
    RecordingEngine::calls.clear();
    REQUIRE (p->process (p, &block.process) == CLAP_PROCESS_CONTINUE);

    const auto& calls = RecordingEngine::calls;
    REQUIRE (calls.size() == 3);
    CHECK (calls[0].numSamples == 100);
    CHECK (calls[0].gain == Catch::Approx (-6.0F));
    REQUIRE (calls[0].events.size() == 1);
    CHECK (calls[0].events[0].type == Event::Type::noteOn);
    CHECK (calls[0].events[0].sampleOffset == 10);
    CHECK (calls[1].numSamples == 200);
    CHECK (calls[1].gain == Catch::Approx (6.0F));
    REQUIRE (calls[1].events.size() == 2);
    CHECK (calls[1].events[0].note == 64);
    CHECK (calls[1].events[0].sampleOffset == 0); // at the split
    CHECK (calls[1].events[1].type == Event::Type::noteOff);
    CHECK (calls[1].events[1].sampleOffset == 100);
    CHECK (calls[2].numSamples == 212);
    CHECK (calls[2].gain == Catch::Approx (-60.0F));
    CHECK (block.outL[511] == 0.25F);

    // Bypass: silence, automation still tracked.
    events.in.clear();
    events.param (0, SelfTestTraits::kBypassParamId, 1.0);
    events.param (50, gain, 1.0);
    RecordingEngine::calls.clear();
    p->process (p, &block.process);
    CHECK (RecordingEngine::calls.empty());
    CHECK (block.outL[0] == 0.0F);
    double value = 0.0;
    extension<clap_plugin_params> (p, CLAP_EXT_PARAMS)->get_value (p, gain, &value);
    CHECK (value == 1.0);
    destroy (plugin);
}

TEST_CASE ("clap: MIDI and note events become the engine's events", "[clap]")
{
    auto* plugin = makePlugin<Recorder>();
    const clap_plugin* p = plugin->clapPlugin();
    EventList events;
    events.midi (0, 0x90, 60, 127);  // note on
    events.midi (1, 0x90, 60, 0);    // velocity 0: note off
    events.midi (2, 0xB0, 64, 127);  // sustain down
    events.midi (3, 0xB0, 1, 64);    // mod wheel
    events.midi (4, 0xE0, 0, 0x40);  // pitch bend centre
    events.midi (5, 0xB0, 123, 0);   // all notes off (any value)
    events.midi (6, 0xB0, 7, 100);   // volume: not mapped
    events.note (7, CLAP_EVENT_NOTE_CHOKE, 62, 0.0);
    events.note (8, CLAP_EVENT_NOTE_OFF, -1, 0.0); // wildcard key
    Block block (64, events, false);
    RecordingEngine::calls.clear();
    p->process (p, &block.process);
    REQUIRE (RecordingEngine::calls.size() == 1);
    const auto& e = RecordingEngine::calls[0].events;
    REQUIRE (e.size() == 8);
    CHECK (e[0].type == Event::Type::noteOn);
    CHECK (e[0].value == Catch::Approx (1.0F));
    CHECK (e[1].type == Event::Type::noteOff);
    CHECK (e[2].type == Event::Type::sustainPedal);
    CHECK (e[2].value >= 0.5F);
    CHECK (e[3].type == Event::Type::modWheel);
    CHECK (e[4].type == Event::Type::pitchBend);
    CHECK (e[4].value == Catch::Approx (0.0F).margin (1e-6));
    CHECK (e[5].type == Event::Type::allNotesOff);
    CHECK (e[6].type == Event::Type::noteOff);
    CHECK (e[6].note == 62);
    CHECK (e[7].type == Event::Type::allNotesOff);
    for (std::size_t i = 0; i < e.size(); ++i)
        CHECK (e[i].sampleOffset == static_cast<int> (i < 6 ? i : i + 1));
    destroy (plugin);
}

TEST_CASE ("clap: flush applies changes without audio and reports editor edits", "[clap]")
{
    auto* plugin = makePlugin<Instrument>();
    const clap_plugin* p = plugin->clapPlugin();
    const auto* params = extension<clap_plugin_params> (p, CLAP_EXT_PARAMS);
    const auto wave = static_cast<clap_id> (ParamId::wave);
    EventList events;
    events.param (0, wave, 1.0);
    params->flush (p, &events.inList, &events.outList);
    double value = 0.0;
    REQUIRE (params->get_value (p, wave, &value));
    CHECK (value == 1.0);
    CHECK (events.out.empty());

    // An edit on the panel: the main thread sees it at once, the host gets
    // gesture begin, the value and gesture end at the next flush.
    plugin->editFromUi (wave, 0.0, EditKind::begin);
    plugin->editFromUi (wave, 1.0, EditKind::value);
    plugin->editFromUi (wave, 0.0, EditKind::end);
    REQUIRE (params->get_value (p, wave, &value));
    CHECK (value == 2.0);
    events.in.clear();
    params->flush (p, &events.inList, &events.outList);
    REQUIRE (events.out.size() == 3);
    const auto* first = reinterpret_cast<const clap_event_header*> (events.out[0].data());
    const auto* second = reinterpret_cast<const clap_event_param_value*> (events.out[1].data());
    const auto* third = reinterpret_cast<const clap_event_header*> (events.out[2].data());
    CHECK (first->type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
    CHECK (second->header.type == CLAP_EVENT_PARAM_VALUE);
    CHECK (second->param_id == wave);
    CHECK (second->value == 2.0);
    CHECK (third->type == CLAP_EVENT_PARAM_GESTURE_END);
    // Ids outside the contract and the bypass are not edits.
    plugin->editFromUi (SelfTestTraits::kProgramParamId, 1.0, EditKind::value);
    events.out.clear();
    params->flush (p, &events.inList, &events.outList);
    CHECK (events.out.empty());
    destroy (plugin);
}

TEST_CASE ("clap: on-screen notes start the next block and the meters fill", "[clap]")
{
    auto* plugin = makePlugin<Instrument>();
    const clap_plugin* p = plugin->clapPlugin();
    REQUIRE (plugin->uiNote (69, 0.8F, true));
    CHECK_FALSE (plugin->uiNote (128, 0.8F, true));
    EventList events;
    Block block (512, events, false);
    float peak = 0.0F;
    for (int i = 0; i < 8; ++i)
    {
        p->process (p, &block.process);
        for (float s : block.outL)
            peak = std::max (peak, std::abs (s));
    }
    CHECK (peak > 0.01F);
    CHECK (plugin->outputPeak (0) > 0.0F);
    CHECK ((plugin->heldWord (69 / 16) & (1u << (69 % 16))) != 0);
    destroy (plugin);
}

TEST_CASE ("clap: an effect reports latency, automates at the sample and bypasses in time", "[clap]")
{
    auto* plugin = makePlugin<Effect> (256);
    const clap_plugin* p = plugin->clapPlugin();
    const auto* latency = extension<clap_plugin_latency> (p, CLAP_EXT_LATENCY);
    REQUIRE (latency != nullptr);
    CHECK (latency->get (p) == static_cast<std::uint32_t> (fakeeffect::kFakeLatency));
    CHECK (p->get_extension (p, CLAP_EXT_NOTE_PORTS) == nullptr);
    const auto* audio = extension<clap_plugin_audio_ports> (p, CLAP_EXT_AUDIO_PORTS);
    CHECK (audio->count (p, true) == 1);

    // trim +12 dB (normalized 1) from sample 300 of a 512-sample block (two
    // 256 chunks): DC 0 before, 12 from within kMinSubBlock of 300.
    EventList events;
    events.param (300, static_cast<clap_id> (fakeeffect::ParamId::trim), 1.0);
    Block block (512, events, true);
    // Silence in; the engine's DC shows the parameter.
    p->process (p, &block.process);
    CHECK (block.outL[299] == Catch::Approx (0.0F).margin (1e-6));
    CHECK (block.outL[300 + kraftonic::formats::ParamSchedule<>::kMinSubBlock] == Catch::Approx (12.0F));
    CHECK (block.outR[511] == Catch::Approx (12.0F));

    // Stepped automation: mode 2 triples the DC.
    events.in.clear();
    events.param (0, static_cast<clap_id> (fakeeffect::ParamId::mode), 2.0);
    p->process (p, &block.process);
    CHECK (block.outL[0] == Catch::Approx (36.0F));

    // Bypass: the latency-delayed dry signal (an impulse at 0 shows at 16).
    events.in.clear();
    events.param (0, fakeeffect::FakeEffectTraits::kBypassParamId, 1.0);
    for (int i = 0; i < 20; ++i)
        p->process (p, &block.process); // past the crossfade
    events.in.clear();
    std::fill (block.inL.begin(), block.inL.end(), 0.0F);
    block.inL[0] = 1.0F;
    p->process (p, &block.process);
    CHECK (block.outL[fakeeffect::kFakeLatency] == Catch::Approx (1.0F));
    CHECK (block.outL[0] == Catch::Approx (0.0F).margin (1e-6));
    destroy (plugin);
}
