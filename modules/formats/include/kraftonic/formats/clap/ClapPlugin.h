// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — shared CLAP plug-in.
//
//  `Plugin<Traits>` is a whole CLAP plug-in made from the same product traits
//  as the VST3 base (vst3::PluginTraits; effects satisfy
//  vst3::effect::EffectTraits and are detected from them):
//
//    - params: one CLAP parameter per contract entry (ParamId = CLAP id,
//      name, default, stepped values) plus the bypass switch ('bypa'). The
//      value is the VST3 plain convention: the step index 0..stepCount for
//      discrete parameters, normalized 0..1 for continuous ones, so host
//      automation curves feel the same in both formats. Text both ways
//      through ParamText.h (the product's formatting hook);
//    - state: the StateCodec payload, byte for byte the VST3 processor's
//      chunk, followed by the bypass switch as a little-endian float64 (the
//      VST3 controller's chunk). Either format loads the other's state;
//    - audio-ports (stereo out; effects also stereo in), note-ports
//      (instruments: CLAP and MIDI dialects), latency (the effect engine's,
//      0 for instruments, as in VST3) and tail;
//    - process: notes, MIDI and parameter events at their sample offsets.
//      Instruments split the block at every parameter change (at most
//      kMaxSplits per block, like vst3::Processor); effects walk
//      ParamSchedule sub-blocks with the latency-compensated bypass of
//      vst3::effect::EffectProcessor.
//
//  Editor edits reach the audio thread through a lock-free SPSC queue
//  (editFromUi, main thread) and go back to the host as gesture and value
//  events, so the host records them. The main thread reads parameter values
//  from relaxed atomics the audio thread keeps current. Nothing on the
//  audio thread allocates, locks, logs or throws; activate() sizes
//  everything.
//
//  The GUI is a template argument (`EditorT<Plugin>`, see ClapEditor.h);
//  NoEditor builds a plug-in without one (hosts then show their generic
//  controls).
// ============================================================================

#include "kraftonic/core/Instrument.h"
#include "kraftonic/formats/EventHelpers.h"
#include "kraftonic/formats/HeldNotes.h"
#include "kraftonic/formats/LatencyBypass.h"
#include "kraftonic/formats/OutputMeter.h"
#include "kraftonic/formats/ParamSchedule.h"
#include "kraftonic/formats/ParamText.h"
#include "kraftonic/formats/StateCodec.h"
#include "kraftonic/formats/SteppedParam.h"
#include "kraftonic/formats/UiNoteQueue.h"
#include "kraftonic/formats/vst3/Vst3Traits.h"
#include "kraftonic/formats/vst3/effect/EffectTraits.h"

#include <clap/clap.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace kraftonic::formats::clap
{

/** A plug-in without a GUI: hosts show their generic parameter controls. */
template <typename PluginT>
struct NoEditor
{
    static constexpr bool kEnabled = false;
    static bool initModule (const char* /*pluginPath*/) noexcept { return true; }
    static void deinitModule() noexcept {}
};

/** What an editor edit means for the host. */
enum class EditKind : std::uint8_t
{
    begin,   // the user grabbed a control
    value,   // a new value, announced to the host
    end,     // the user let go
    silent   // a new value the host already knows (state load)
};

/** One editor edit on its way to the audio thread. */
struct UiEdit
{
    std::uint32_t id = 0;
    float plain = 0.0F;      // engine units (bypass: 0 or 1)
    double value = 0.0;      // the CLAP value, for the host
    EditKind kind = EditKind::value;
};

template <vst3::PluginTraits Traits, template <typename> class EditorT = NoEditor>
class Plugin
{
public:
    using ParamId = typename Traits::ParamId;
    using Params = typename Traits::Params;
    using Engine = typename Traits::Engine;
    using EditorType = EditorT<Plugin>;
    using TraitsType = Traits;

    static constexpr bool kIsEffect = vst3::effect::EffectTraits<Traits>;
    static constexpr int kNumParams = Traits::kNumParams;
    static constexpr std::uint32_t kBypassId = static_cast<std::uint32_t> (Traits::kBypassParamId);
    /** Most splits of one instrument block (later changes join the last one). */
    static constexpr int kMaxSplits = 64;
    static constexpr std::size_t kSchedulePoints = 2048;

    Plugin (const clap_host* host, const clap_plugin_descriptor* descriptor) : host_ (host)
    {
        plugin_.desc = descriptor;
        plugin_.plugin_data = this;
        plugin_.init = [] (const clap_plugin* p) { return self (p).init(); };
        plugin_.destroy = [] (const clap_plugin* p) { delete &self (p); };
        plugin_.activate = [] (const clap_plugin* p, double rate, std::uint32_t minFrames, std::uint32_t maxFrames) {
            return self (p).activate (rate, minFrames, maxFrames);
        };
        plugin_.deactivate = [] (const clap_plugin*) {};
        plugin_.start_processing = [] (const clap_plugin*) { return true; };
        plugin_.stop_processing = [] (const clap_plugin* p) { self (p).resetEngine(); };
        plugin_.reset = [] (const clap_plugin* p) { self (p).resetEngine(); };
        plugin_.process = [] (const clap_plugin* p, const clap_process* process) {
            return self (p).process (*process);
        };
        plugin_.get_extension = [] (const clap_plugin* p, const char* id) { return self (p).extension (id); };
        plugin_.on_main_thread = [] (const clap_plugin*) {};

        params_ = Params::defaults();
        for (int i = 0; i < kNumParams; ++i)
            plain_[static_cast<std::size_t> (idAt (i))].store (params_.get (idAt (i)), std::memory_order_relaxed);
    }

    ~Plugin() = default;
    Plugin (const Plugin&) = delete;
    Plugin& operator= (const Plugin&) = delete;

    const clap_plugin* clapPlugin() const noexcept { return &plugin_; }
    const clap_host* host() const noexcept { return host_; }

    // --- Values, any thread ---------------------------------------------------

    /** CLAP value of a contract parameter at a plain value (see the header). */
    static double valueFromPlain (ParamId id, float plain) noexcept
    {
        const double normalized = Traits::toNormalized (id, plain);
        const int steps = Traits::paramInfo (id).stepCount;
        return steps > 0 ? stepIndexFromNormalized (steps, normalized) : normalized;
    }

    static double normalizedFromValue (ParamId id, double value) noexcept
    {
        return normalizedFromStepIndex (Traits::paramInfo (id).stepCount, value);
    }

    static double valueFromNormalized (ParamId id, double normalized) noexcept
    {
        const int steps = Traits::paramInfo (id).stepCount;
        if (steps > 0)
            return stepIndexFromNormalized (steps, normalized);
        return normalized < 0.0 ? 0.0 : (normalized > 1.0 ? 1.0 : normalized);
    }

    /** Current plain value of a contract parameter. */
    float plainValue (ParamId id) const noexcept
    {
        return plain_[static_cast<std::size_t> (id)].load (std::memory_order_relaxed);
    }

    bool bypassed() const noexcept { return bypassShadow_.load (std::memory_order_relaxed); }

    /** Latest output peak (0 = left, 1 = right), ~30 Hz; 0 without meter traits. */
    float outputPeak (int channel) const noexcept
    {
        return meterPeaks_[channel == 1 ? 1 : 0].load (std::memory_order_relaxed);
    }

    /** Held-note word `w` (16 notes each, see HeldNotes.h); 0 without a UI keyboard. */
    std::uint16_t heldWord (int w) const noexcept
    {
        return static_cast<std::uint16_t> (heldWords_[static_cast<std::size_t> (w)].load (std::memory_order_relaxed));
    }

    // --- Editor side (main thread) -------------------------------------------

    /** An edit from the editor, `normalized` in [0, 1] as the VST3 controller
        sees it. Contract parameters and the bypass switch; other ids are
        ignored. Lock-free: the audio thread applies it in its next process()
        or params flush, and tells the host. */
    void editFromUi (std::uint32_t id, double normalized, EditKind kind) noexcept
    {
        UiEdit edit {};
        edit.id = id;
        edit.kind = kind;
        if (id == kBypassId)
        {
            edit.plain = normalized >= 0.5 ? 1.0F : 0.0F;
            edit.value = static_cast<double> (edit.plain);
            if (kind == EditKind::value || kind == EditKind::silent)
                bypassShadow_.store (edit.plain >= 0.5F, std::memory_order_relaxed);
        }
        else if (id < static_cast<std::uint32_t> (kNumParams))
        {
            const auto paramId = static_cast<ParamId> (id);
            edit.plain = Traits::toPlain (paramId, normalized);
            edit.value = valueFromPlain (paramId, edit.plain);
            if (kind == EditKind::value || kind == EditKind::silent)
                plain_[id].store (edit.plain, std::memory_order_relaxed);
        }
        else
        {
            return;
        }
        uiEdits_.push (edit); // full: dropped (the shadow value still shows it)
        if (hostParams_ != nullptr && kind != EditKind::silent)
            hostParams_->request_flush (host_);
    }

    /** An on-screen key (HasUiKeyboard traits): played at the next block start. */
    bool uiNote (int note, float velocity, bool on) noexcept
    {
        if (note < 0 || note > 127)
            return false;
        UiNote queued {};
        queued.note = note;
        queued.velocity = velocity >= 0.0F && velocity <= 1.0F ? velocity : (velocity > 1.0F ? 1.0F : 0.0F);
        queued.on = on;
        return uiNotes_.push (queued);
    }

    // --- Extensions, public so tests can drive them like a host -------------

    std::uint32_t paramCount() const noexcept { return static_cast<std::uint32_t> (kNumParams) + 1u; }

    bool paramInfo (std::uint32_t index, clap_param_info& info) const noexcept
    {
        info = clap_param_info {};
        info.cookie = nullptr;
        if (index < static_cast<std::uint32_t> (kNumParams))
        {
            const auto& meta = Traits::paramTable()[index];
            info.id = static_cast<clap_id> (meta.id);
            info.flags = CLAP_PARAM_IS_AUTOMATABLE;
            if (meta.stepCount > 0)
            {
                info.flags |= CLAP_PARAM_IS_STEPPED;
                info.max_value = static_cast<double> (meta.stepCount);
            }
            else
            {
                info.max_value = 1.0;
            }
            info.min_value = 0.0;
            info.default_value = valueFromPlain (meta.id, meta.defaultValue);
            copyText (info.name, sizeof (info.name), meta.name);
            return true;
        }
        if (index == static_cast<std::uint32_t> (kNumParams))
        {
            info.id = kBypassId;
            info.flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS;
            info.min_value = 0.0;
            info.max_value = 1.0;
            info.default_value = 0.0;
            copyText (info.name, sizeof (info.name), "Bypass");
            return true;
        }
        return false;
    }

    bool paramValue (clap_id id, double& value) const noexcept
    {
        if (id == kBypassId)
        {
            value = bypassed() ? 1.0 : 0.0;
            return true;
        }
        if (id >= static_cast<clap_id> (kNumParams))
            return false;
        const auto paramId = static_cast<ParamId> (id);
        value = valueFromPlain (paramId, plain_[id].load (std::memory_order_relaxed));
        return true;
    }

    bool valueToText (clap_id id, double value, char* text, std::uint32_t size) const noexcept
    {
        if (text == nullptr || size == 0)
            return false;
        if (id == kBypassId)
        {
            copyText (text, size, bypassText (value));
            return true;
        }
        if (id >= static_cast<clap_id> (kNumParams))
            return false;
        const auto paramId = static_cast<ParamId> (id);
        formatParamText<Traits> (paramId, normalizedFromValue (paramId, value), text, size);
        return true;
    }

    bool textToValue (clap_id id, const char* text, double& value) const noexcept
    {
        double normalized = 0.0;
        if (id == kBypassId)
        {
            if (!parseBypassText (text, normalized))
                return false;
            value = normalized;
            return true;
        }
        if (id >= static_cast<clap_id> (kNumParams))
            return false;
        const auto paramId = static_cast<ParamId> (id);
        if (!parseParamText<Traits> (paramId, text, normalized))
            return false;
        value = valueFromNormalized (paramId, normalized);
        return true;
    }

    /** Parameter changes with no audio (audio thread, or main thread while
        inactive): editor edits and host events are applied, nothing renders. */
    void flush (const clap_input_events* in, const clap_output_events* out) noexcept
    {
        drainUiEdits (out);
        if (in != nullptr)
        {
            const std::uint32_t count = in->size (in);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const clap_event_header* header = in->get (in, i);
                if (const auto* change = paramEvent (header))
                    applyParam (*change);
            }
        }
        pushParams_ = true;
        if constexpr (kIsEffect)
        {
            if (engineReady_)
            {
                engine_.setParameters (params_);
                pushParams_ = false;
            }
        }
    }

    /** The state: StateCodec payload + bypass float64 (see the header). Main thread. */
    std::size_t saveState (std::uint8_t* bytes, std::size_t capacity) const noexcept
    {
        Params params = Params::defaults();
        for (int i = 0; i < kNumParams; ++i)
            params.set (idAt (i), plainValue (idAt (i)));
        const std::size_t used = encodeState<Traits> (params, bytes, capacity);
        if (used == 0 || capacity < used + 8)
            return 0;
        state_detail::writeF64LE (bytes + used, bypassed() ? 1.0 : 0.0);
        return used + 8;
    }

    /** Loads either format's state. Corrupt data keeps the defaults and fails. Main thread. */
    bool loadState (const std::uint8_t* bytes, std::size_t size) noexcept
    {
        Params params = Params::defaults();
        const bool ok = decodeState<Traits> (bytes, size, params);
        double bypass = 0.0;
        if (ok)
        {
            const std::size_t codec = stateSizeForCount (state_detail::readU32LE (bytes + 8));
            if (size >= codec + 8)
            {
                const double stored = state_detail::readF64LE (bytes + codec);
                bypass = stored >= 0.5 && stored <= 1.0 ? 1.0 : 0.0;
            }
        }
        for (int i = 0; i < kNumParams; ++i)
        {
            const ParamId id = idAt (i);
            editFromUi (static_cast<std::uint32_t> (id), Traits::toNormalized (id, params.get (id)), EditKind::silent);
        }
        editFromUi (kBypassId, bypass, EditKind::silent);
        if (hostParams_ != nullptr)
        {
            hostParams_->rescan (host_, CLAP_PARAM_RESCAN_VALUES);
            hostParams_->request_flush (host_);
        }
        return ok;
    }

    std::uint32_t latency() const noexcept { return static_cast<std::uint32_t> (latency_); }

    std::uint32_t tail() const noexcept
    {
        const double samples = Traits::tailSeconds (params_) * sampleRate_;
        if (samples >= static_cast<double> (0xFFFFFFFEu))
            return 0xFFFFFFFEu; // never "infinite": hosts may sleep the plug-in
        return samples > 0.0 ? static_cast<std::uint32_t> (samples) : 0u;
    }

    bool init() noexcept
    {
        hostParams_ = static_cast<const clap_host_params*> (hostExtension (CLAP_EXT_PARAMS));
        hostLatency_ = static_cast<const clap_host_latency*> (hostExtension (CLAP_EXT_LATENCY));
        return true;
    }

    bool activate (double sampleRate, std::uint32_t /*minFrames*/, std::uint32_t maxFrames)
    {
        // Main thread, audio stopped: allocating is fine here.
        double rate = sampleRate;
        if (!(rate >= 22050.0))
            rate = 22050.0;
        if (rate > 384000.0)
            rate = 384000.0;
        sampleRate_ = rate;
        int maxBlock = maxFrames > 65536u ? 65536 : static_cast<int> (maxFrames);
        if (maxBlock < 1)
            maxBlock = 1;
        maxBlock_ = maxBlock;

        drainUiEdits (nullptr); // edits made while inactive (the host knows them)
        core::ProcessSetup setup {};
        setup.sampleRate = sampleRate_;
        setup.maxBlockSize = maxBlock_;
        engine_.prepare (setup);
        engine_.setParameters (params_);
        pushParams_ = false;
        engineReady_ = true;
        engineIdle_ = false;
        bypassSilenced_ = true;
        const int oldLatency = latency_;
        if constexpr (kIsEffect)
        {
            for (auto* buffer : {&dryLeft_, &dryRight_, &wetLeft_, &wetRight_})
                buffer->assign (static_cast<std::size_t> (maxBlock_), 0.0F);
            latency_ = engine_.latencySamples() > 0 ? engine_.latencySamples() : 0;
            bypass_.prepare (sampleRate_, latency_);
            bypass_.setBypassed (bypassed_);
            bypass_.snapToTarget();
        }
        meterFeed_.setSampleRate (sampleRate_);
        heldFeed_.setSampleRate (sampleRate_);
        heldNotes_.clear();
        if (latency_ != oldLatency && hostLatency_ != nullptr)
            hostLatency_->changed (host_);
        return true;
    }

    clap_process_status process (const clap_process& process) noexcept
    {
        drainUiEdits (process.out_events);
        if constexpr (kIsEffect)
            processEffect (process);
        else
            processInstrument (process);
        return CLAP_PROCESS_CONTINUE;
    }

    /** Test hook: the plug-in's editor, or nullptr. */
    EditorType* editor() noexcept { return editor_.get(); }

private:
    static Plugin& self (const clap_plugin* p) noexcept { return *static_cast<Plugin*> (p->plugin_data); }

    static ParamId idAt (int index) noexcept { return Traits::paramTable()[index].id; }

    static void copyText (char* dst, std::size_t size, const char* src) noexcept
    {
        if (dst == nullptr || size == 0)
            return;
        std::size_t i = 0;
        for (; src != nullptr && src[i] != '\0' && i + 1 < size; ++i)
            dst[i] = src[i];
        dst[i] = '\0';
    }

    const void* hostExtension (const char* id) const noexcept
    {
        return host_ != nullptr && host_->get_extension != nullptr ? host_->get_extension (host_, id) : nullptr;
    }

    void resetEngine() noexcept
    {
        if (!engineReady_)
            return;
        engine_.reset();
        if constexpr (kIsEffect)
            bypass_.reset();
        heldNotes_.clear(); // reset voices: no key can stay lit
    }

    // --- Parameter events -------------------------------------------------------

    static const clap_event_param_value* paramEvent (const clap_event_header* h) noexcept
    {
        if (h == nullptr || h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE)
            return nullptr;
        return reinterpret_cast<const clap_event_param_value*> (h);
    }

    /** Applies one host change now (bypass latches; unknown ids are ignored). */
    void applyParam (const clap_event_param_value& change) noexcept
    {
        if (change.param_id == kBypassId)
        {
            bypassed_ = change.value >= 0.5;
            bypassShadow_.store (bypassed_, std::memory_order_relaxed);
            return;
        }
        if (change.param_id >= static_cast<clap_id> (kNumParams))
            return;
        const auto id = static_cast<ParamId> (change.param_id);
        setPlain (id, Traits::toPlain (id, normalizedFromValue (id, change.value)));
    }

    void setPlain (ParamId id, float plain) noexcept
    {
        params_.set (id, plain);
        plain_[static_cast<std::size_t> (id)].store (params_.get (id), std::memory_order_relaxed);
    }

    /** Applies the editor's queued edits now and tells the host (out may be null). */
    void drainUiEdits (const clap_output_events* out) noexcept
    {
        UiEdit edit {};
        while (uiEdits_.pop (edit))
        {
            if (edit.kind == EditKind::value || edit.kind == EditKind::silent)
            {
                if (edit.id == kBypassId)
                {
                    bypassed_ = edit.plain >= 0.5F;
                    bypassShadow_.store (bypassed_, std::memory_order_relaxed);
                }
                else
                {
                    setPlain (static_cast<ParamId> (edit.id), edit.plain);
                    pushParams_ = true;
                }
            }
            if (out == nullptr || edit.kind == EditKind::silent)
                continue;
            if (edit.kind == EditKind::value)
            {
                clap_event_param_value event {};
                event.header = header (sizeof (event), CLAP_EVENT_PARAM_VALUE);
                event.param_id = edit.id;
                event.cookie = nullptr;
                event.note_id = -1;
                event.port_index = -1;
                event.channel = -1;
                event.key = -1;
                event.value = edit.value;
                out->try_push (out, &event.header);
            }
            else
            {
                clap_event_param_gesture event {};
                const auto type = static_cast<std::uint16_t> (
                    edit.kind == EditKind::begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END);
                event.header = header (sizeof (event), type);
                event.param_id = edit.id;
                out->try_push (out, &event.header);
            }
        }
    }

    static clap_event_header header (std::size_t size, std::uint16_t type) noexcept
    {
        clap_event_header h {};
        h.size = static_cast<std::uint32_t> (size);
        h.time = 0;
        h.space_id = CLAP_CORE_EVENT_SPACE_ID;
        h.type = type;
        h.flags = 0;
        return h;
    }

    static int eventTime (const clap_event_header& h, int numSamples) noexcept
    {
        return clampOffset (static_cast<std::int32_t> (h.time > 0x7FFFFFFFu ? 0x7FFFFFFFu : h.time), numSamples);
    }

    /** The block's bypass: the last bypass value in it (latched, as in VST3). */
    void scanBypass (const clap_input_events* in) noexcept
    {
        if (in == nullptr)
            return;
        const std::uint32_t count = in->size (in);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto* change = paramEvent (in->get (in, i));
            if (change != nullptr && change->param_id == kBypassId)
                applyParam (*change);
        }
    }

    // --- Instruments ------------------------------------------------------------

    void processInstrument (const clap_process& process) noexcept
    {
        const int numSamples = static_cast<int> (process.frames_count);
        float* left = nullptr;
        float* right = nullptr;
        if (process.audio_outputs_count > 0 && process.audio_outputs != nullptr)
        {
            clap_audio_buffer& out = process.audio_outputs[0];
            out.constant_mask = 0;
            if (out.data32 != nullptr && out.channel_count > 0)
            {
                left = out.data32[0];
                right = out.channel_count > 1 && out.data32[1] != nullptr ? out.data32[1] : left;
            }
        }
        scanBypass (process.in_events);
        const bool render = left != nullptr && numSamples > 0 && engineReady_ && !bypassed_;
        if (bypassed_ && !bypassSilenced_ && engineReady_)
        {
            engine_.reset(); // voices off on entry so no note can stick
            bypassSilenced_ = true;
        }
        else if (!bypassed_)
        {
            bypassSilenced_ = false;
        }
        if (render)
            applyTransport (process.transport);

        // Walk the (time-sorted) events: notes collect at their offsets, a
        // parameter change renders everything before it first.
        eventCount_ = 0;
        nextEvent_ = 0;
        collectUiNotes (numSamples);
        int start = 0;
        int splits = 0;
        const clap_input_events* in = process.in_events;
        const std::uint32_t count = in != nullptr ? in->size (in) : 0u;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const clap_event_header* h = in->get (in, i);
            if (h == nullptr || h->space_id != CLAP_CORE_EVENT_SPACE_ID)
                continue;
            const int time = eventTime (*h, numSamples);
            if (const auto* change = paramEvent (h))
            {
                if (change->param_id >= static_cast<clap_id> (kNumParams))
                    continue; // bypass is latched above
                if (time > start && splits < kMaxSplits)
                {
                    if (render)
                        renderRange (left, right, start, time);
                    start = time;
                    ++splits;
                }
                applyParam (*change);
                continue;
            }
            translateNoteEvent (*h, time, numSamples);
        }
        if (render)
        {
            if (start < numSamples)
                renderRange (left, right, start, numSamples);
            noteOutputLevels (blockPeak (left, numSamples), blockPeak (right, numSamples), numSamples);
        }
        else
        {
            clearOutputs (process, numSamples);
            noteOutputLevels (0.0F, 0.0F, numSamples);
        }
        noteHeldNotes (numSamples);
    }

    void applyTransport (const clap_event_transport* transport) noexcept
    {
        if constexpr (vst3::HasEngineTempo<Engine>)
        {
            if (transport != nullptr && (transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0)
                engine_.setTempo (transport->tempo);
        }
        if constexpr (vst3::HasEngineTransport<Engine>)
        {
            const bool positionValid
                = transport != nullptr && (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
            engine_.setTransport (transport != nullptr && (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0,
                                  positionValid,
                                  positionValid ? static_cast<double> (transport->song_pos_beats)
                                                      / static_cast<double> (CLAP_BEATTIME_FACTOR)
                                                : 0.0);
        }
        (void)transport;
    }

    /** Renders [start, end) with the collected events before `end`. */
    void renderRange (float* left, float* right, int start, int end) noexcept
    {
        int segmentEvents = 0;
        while (nextEvent_ < eventCount_ && events_[static_cast<std::size_t> (nextEvent_)].sampleOffset < end)
        {
            core::Event event = events_[static_cast<std::size_t> (nextEvent_++)];
            event.sampleOffset -= start;
            segmentEvents_[static_cast<std::size_t> (segmentEvents++)] = event;
        }
        engine_.setParameters (params_);
        const core::AudioBlock block {left + start, right + start, end - start};
        engine_.process (block, segmentEvents > 0 ? segmentEvents_.data() : nullptr, segmentEvents);
    }

    bool pushEvent (const core::Event& e) noexcept
    {
        if (eventCount_ >= core::kMaxEventsPerBlock)
            return false; // drop overflow without allocating
        events_[static_cast<std::size_t> (eventCount_++)] = e;
        return true;
    }

    void collectUiNotes (int numSamples) noexcept
    {
        UiNote note {};
        while (eventCount_ < core::kMaxEventsPerBlock && uiNotes_.pop (note))
        {
            if (numSamples <= 0)
                continue;
            pushEvent (note.on ? makeNoteEvent (note.note, static_cast<double> (note.velocity), 0)
                               : makeNoteOffEvent (note.note, static_cast<double> (note.velocity), 0));
        }
    }

    /** CLAP note events and MIDI 1.0 messages -> core::Event at `time`. */
    void translateNoteEvent (const clap_event_header& h, int time, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;
        switch (h.type)
        {
            case CLAP_EVENT_NOTE_ON:
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE:
            {
                const auto& note = reinterpret_cast<const clap_event_note&> (h);
                if (note.key < 0)
                {
                    if (h.type != CLAP_EVENT_NOTE_ON) // wildcard: every key
                        pushEvent (control (core::Event::Type::allNotesOff, time));
                    return;
                }
                if (h.type == CLAP_EVENT_NOTE_ON)
                    pushEvent (makeNoteEvent (note.key, note.velocity, time));
                else
                    pushEvent (makeNoteOffEvent (note.key, h.type == CLAP_EVENT_NOTE_OFF ? note.velocity : 0.0, time));
                return;
            }
            case CLAP_EVENT_NOTE_EXPRESSION:
            {
                const auto& expression = reinterpret_cast<const clap_event_note_expression&> (h);
                if (expression.expression_id == CLAP_NOTE_EXPRESSION_PRESSURE && expression.key >= 0)
                    pushEvent (makePolyPressureEvent (expression.key, expression.value, time));
                return;
            }
            case CLAP_EVENT_MIDI:
                translateMidi (reinterpret_cast<const clap_event_midi&> (h).data, time);
                return;
            default:
                return;
        }
    }

    static core::Event control (core::Event::Type type, int time) noexcept
    {
        core::Event e {};
        e.type = type;
        e.sampleOffset = time;
        return e;
    }

    /** The controllers the VST3 base maps (sustain, mod wheel, pitch bend,
        all notes/sound off, pressure) plus note on/off. */
    void translateMidi (const std::uint8_t* data, int time) noexcept
    {
        const int status = data[0] & 0xF0;
        const int data1 = data[1] & 0x7F;
        const int data2 = data[2] & 0x7F;
        const double value = static_cast<double> (data2) / 127.0;
        switch (status)
        {
            case 0x90:
                pushEvent (data2 > 0 ? makeNoteEvent (data1, value, time) : makeNoteOffEvent (data1, 0.0, time));
                return;
            case 0x80:
                pushEvent (makeNoteOffEvent (data1, value, time));
                return;
            case 0xA0:
                pushEvent (makePolyPressureEvent (data1, value, time));
                return;
            case 0xB0:
                if (data1 == 64)
                    pushEvent (makeSustainEvent (value, time));
                else if (data1 == 1)
                    pushEvent (makeModWheelEvent (value, time));
                else if (data1 == 123)
                    pushEvent (control (core::Event::Type::allNotesOff, time));
                else if (data1 == 120)
                    pushEvent (control (core::Event::Type::allSoundOff, time));
                return;
            case 0xD0:
                if constexpr (vst3::HasChannelPressure<Traits>)
                    pushEvent (makeChannelPressureEvent (static_cast<double> (data1) / 127.0, time));
                return;
            case 0xE0:
                pushEvent (makePitchBendEvent (pitchBendFromMidi14 (data1, data2), time));
                return;
            default:
                return;
        }
    }

    static float blockPeak (const float* samples, int numSamples) noexcept
    {
        float peak = 0.0F;
        for (int s = 0; samples != nullptr && s < numSamples; ++s)
        {
            const float magnitude = samples[s] < 0.0F ? -samples[s] : samples[s];
            if (magnitude > peak)
                peak = magnitude;
        }
        return peak;
    }

    void noteOutputLevels (float peakL, float peakR, int numSamples) noexcept
    {
        if constexpr (vst3::HasOutputMeterIds<Traits>)
        {
            meterFeed_.observe (peakL, peakR, numSamples);
            if (!meterFeed_.ready())
                return;
            float emitL = 0.0F;
            float emitR = 0.0F;
            meterFeed_.take (emitL, emitR);
            meterPeaks_[0].store (emitL, std::memory_order_relaxed);
            meterPeaks_[1].store (emitR, std::memory_order_relaxed);
        }
        else
        {
            (void)peakL;
            (void)peakR;
            (void)numSamples;
        }
    }

    void noteHeldNotes (int numSamples) noexcept
    {
        if constexpr (vst3::HasUiKeyboard<Traits>)
        {
            for (int i = 0; i < eventCount_; ++i)
                heldNotes_.apply (events_[static_cast<std::size_t> (i)]);
            if (!heldFeed_.advance (numSamples))
                return;
            for (int w = 0; w < kHeldNoteWords; ++w)
                heldWords_[static_cast<std::size_t> (w)].store (heldNotes_.word (w), std::memory_order_relaxed);
        }
        else
        {
            (void)numSamples;
        }
    }

    static void clearOutputs (const clap_process& process, int numSamples) noexcept
    {
        for (std::uint32_t b = 0; process.audio_outputs != nullptr && b < process.audio_outputs_count; ++b)
        {
            const clap_audio_buffer& out = process.audio_outputs[b];
            for (std::uint32_t c = 0; out.data32 != nullptr && c < out.channel_count; ++c)
            {
                if (out.data32[c] == nullptr)
                    continue;
                for (int s = 0; s < numSamples; ++s)
                    out.data32[c][s] = 0.0F;
            }
        }
    }

    // --- Effects -----------------------------------------------------------------

    void processEffect (const clap_process& process) noexcept
    {
        const int numSamples = static_cast<int> (process.frames_count);
        scanBypass (process.in_events);

        // Parameter changes into one schedule lane, in time order.
        schedule_.clear();
        schedule_.beginLane();
        const clap_input_events* in = process.in_events;
        const std::uint32_t count = in != nullptr ? in->size (in) : 0u;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto* change = paramEvent (in->get (in, i));
            if (change == nullptr || change->param_id >= static_cast<clap_id> (kNumParams))
                continue;
            const auto id = static_cast<ParamId> (change->param_id);
            const float plain = Traits::toPlain (id, normalizedFromValue (id, change->value));
            if (schedule_.numPoints() >= kSchedulePoints)
            {
                setPlain (id, plain); // full (never in practice): the value applies now
                pushParams_ = true;
                continue;
            }
            schedule_.push (static_cast<std::int32_t> (eventTime (change->header, numSamples)),
                            change->param_id, plain, numSamples);
        }

        clap_audio_buffer* out = process.audio_outputs_count > 0 ? process.audio_outputs : nullptr;
        if (out == nullptr || numSamples <= 0 || out->data32 == nullptr || out->channel_count < 1 || !engineReady_)
        {
            applyAllPending();
            clearOutputs (process, numSamples);
            return;
        }
        out->constant_mask = 0;
        bypass_.setBypassed (bypassed_);
        const clap_audio_buffer* input
            = process.audio_inputs_count > 0 && process.audio_inputs != nullptr ? &process.audio_inputs[0] : nullptr;
        for (int chunkStart = 0; chunkStart < numSamples; chunkStart += maxBlock_)
        {
            const int remaining = numSamples - chunkStart;
            const int chunk = remaining < maxBlock_ ? remaining : maxBlock_;
            readInput (input, chunkStart, chunk);
            for (int i = 0; i < chunk; ++i)
            {
                wetLeft_[static_cast<std::size_t> (i)] = dryLeft_[static_cast<std::size_t> (i)];
                wetRight_[static_cast<std::size_t> (i)] = dryRight_[static_cast<std::size_t> (i)];
            }
            runEffect (chunkStart, chunk);
            bypass_.process (dryLeft_.data(), dryRight_.data(), wetLeft_.data(), wetRight_.data(), chunk);
            for (std::uint32_t c = 0; c < out->channel_count; ++c)
            {
                float* dst = out->data32[c];
                if (dst == nullptr)
                    continue;
                const float* src = c == 0 ? wetLeft_.data() : wetRight_.data();
                for (int i = 0; i < chunk; ++i)
                    dst[chunkStart + i] = src[i];
            }
        }
        applyAllPending();
    }

    bool applyUpTo (int limit) noexcept
    {
        return schedule_.applyUpTo (limit, [this] (std::uint32_t id, float plain) noexcept {
            setPlain (static_cast<ParamId> (id), plain);
        });
    }

    void applyAllPending() noexcept
    {
        if (applyUpTo (ParamSchedule<>::kNoMorePoints) || pushParams_)
        {
            if (engineReady_)
                engine_.setParameters (params_);
            pushParams_ = false;
        }
    }

    /** Same walk as vst3::effect::EffectProcessor::runEngine. */
    void runEffect (int chunkStart, int chunk) noexcept
    {
        const int chunkEnd = chunkStart + chunk;
        if (bypass_.fullyBypassed())
        {
            if (applyUpTo (chunkEnd - 1) || pushParams_)
            {
                engine_.setParameters (params_);
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
            bypass_.holdMix (latency_); // dry until the latency line has refilled
            engineIdle_ = false;
        }
        int pos = chunkStart;
        while (pos < chunkEnd)
        {
            if (applyUpTo (pos) || pushParams_)
            {
                engine_.setParameters (params_);
                pushParams_ = false;
            }
            const int end = schedule_.subBlockEnd (pos, chunkEnd);
            const auto local = static_cast<std::size_t> (pos - chunkStart);
            const core::AudioBlock block {wetLeft_.data() + local, wetRight_.data() + local, end - pos};
            engine_.process (block);
            pos = end;
        }
    }

    void readInput (const clap_audio_buffer* in, int start, int count) noexcept
    {
        const std::uint32_t channels = in != nullptr && in->data32 != nullptr ? in->channel_count : 0u;
        const float* left = channels > 0 ? in->data32[0] : nullptr;
        const float* right = channels > 1 && in->data32[1] != nullptr ? in->data32[1] : left; // mono feeds both
        for (int i = 0; i < count; ++i)
        {
            dryLeft_[static_cast<std::size_t> (i)] = left != nullptr ? left[start + i] : 0.0F;
            dryRight_[static_cast<std::size_t> (i)] = right != nullptr ? right[start + i] : 0.0F;
        }
    }

    // --- Extension tables ----------------------------------------------------------

    const void* extension (const char* id) const noexcept
    {
        if (id == nullptr)
            return nullptr;
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)
            return paramsExtension();
        if (std::strcmp (id, CLAP_EXT_STATE) == 0)
            return stateExtension();
        if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0)
            return audioPortsExtension();
        if (std::strcmp (id, CLAP_EXT_LATENCY) == 0)
            return latencyExtension();
        if (std::strcmp (id, CLAP_EXT_TAIL) == 0)
            return tailExtension();
        if constexpr (!kIsEffect)
        {
            if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0)
                return notePortsExtension();
        }
        if constexpr (EditorType::kEnabled)
        {
            if (std::strcmp (id, CLAP_EXT_GUI) == 0)
                return EditorType::guiExtension();
        }
        return nullptr;
    }

    static const clap_plugin_params* paramsExtension() noexcept
    {
        static const clap_plugin_params extension {
            [] (const clap_plugin* p) { return self (p).paramCount(); },
            [] (const clap_plugin* p, std::uint32_t index, clap_param_info* info) {
                return info != nullptr && self (p).paramInfo (index, *info);
            },
            [] (const clap_plugin* p, clap_id id, double* value) {
                return value != nullptr && self (p).paramValue (id, *value);
            },
            [] (const clap_plugin* p, clap_id id, double value, char* text, std::uint32_t size) {
                return self (p).valueToText (id, value, text, size);
            },
            [] (const clap_plugin* p, clap_id id, const char* text, double* value) {
                return value != nullptr && self (p).textToValue (id, text, *value);
            },
            [] (const clap_plugin* p, const clap_input_events* in, const clap_output_events* out) {
                self (p).flush (in, out);
            },
        };
        return &extension;
    }

    static const clap_plugin_state* stateExtension() noexcept
    {
        static const clap_plugin_state extension {
            [] (const clap_plugin* p, const clap_ostream* stream) {
                std::uint8_t bytes[kMaxStateBytes + 8] {};
                const std::size_t used = self (p).saveState (bytes, sizeof (bytes));
                std::size_t written = 0;
                while (used > 0 && written < used)
                {
                    const std::int64_t n = stream->write (stream, bytes + written, used - written);
                    if (n <= 0)
                        return false;
                    written += static_cast<std::size_t> (n);
                }
                return used > 0;
            },
            [] (const clap_plugin* p, const clap_istream* stream) {
                // Bounded: never trusts the stream's length.
                std::uint8_t bytes[kMaxStateBytes + 8] {};
                std::size_t total = 0;
                while (total < sizeof (bytes))
                {
                    const std::int64_t n = stream->read (stream, bytes + total, sizeof (bytes) - total);
                    if (n <= 0)
                        break;
                    total += static_cast<std::size_t> (n);
                }
                return self (p).loadState (bytes, total);
            },
        };
        return &extension;
    }

    static const clap_plugin_audio_ports* audioPortsExtension() noexcept
    {
        static const clap_plugin_audio_ports extension {
            [] (const clap_plugin*, bool isInput) -> std::uint32_t { return isInput && !kIsEffect ? 0u : 1u; },
            [] (const clap_plugin*, std::uint32_t index, bool isInput, clap_audio_port_info* info) {
                if (info == nullptr || index != 0 || (isInput && !kIsEffect))
                    return false;
                *info = clap_audio_port_info {};
                info->id = 0;
                copyText (info->name, sizeof (info->name), isInput ? "Stereo In" : "Stereo Out");
                info->flags = CLAP_AUDIO_PORT_IS_MAIN;
                info->channel_count = 2;
                info->port_type = CLAP_PORT_STEREO;
                info->in_place_pair = kIsEffect ? 0 : CLAP_INVALID_ID; // the effect copies its input first
                return true;
            },
        };
        return &extension;
    }

    static const clap_plugin_note_ports* notePortsExtension() noexcept
    {
        static const clap_plugin_note_ports extension {
            [] (const clap_plugin*, bool isInput) -> std::uint32_t { return isInput ? 1u : 0u; },
            [] (const clap_plugin*, std::uint32_t index, bool isInput, clap_note_port_info* info) {
                if (info == nullptr || index != 0 || !isInput)
                    return false;
                *info = clap_note_port_info {};
                info->id = 0;
                info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
                info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
                copyText (info->name, sizeof (info->name), "MIDI In");
                return true;
            },
        };
        return &extension;
    }

    static const clap_plugin_latency* latencyExtension() noexcept
    {
        static const clap_plugin_latency extension {
            [] (const clap_plugin* p) { return self (p).latency(); },
        };
        return &extension;
    }

    static const clap_plugin_tail* tailExtension() noexcept
    {
        static const clap_plugin_tail extension {
            [] (const clap_plugin* p) { return self (p).tail(); },
        };
        return &extension;
    }

    clap_plugin plugin_ {};
    const clap_host* host_ = nullptr;
    const clap_host_params* hostParams_ = nullptr;
    const clap_host_latency* hostLatency_ = nullptr;

    // Audio thread (main thread while inactive).
    Params params_;
    Engine engine_;
    bool engineReady_ = false;
    bool pushParams_ = true;
    bool bypassed_ = false;
    bool bypassSilenced_ = true;
    bool engineIdle_ = false;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 1;
    int latency_ = 0;
    std::array<core::Event, core::kMaxEventsPerBlock> events_ {};
    std::array<core::Event, core::kMaxEventsPerBlock> segmentEvents_ {};
    int eventCount_ = 0;
    int nextEvent_ = 0;
    ParamSchedule<kSchedulePoints, 1> schedule_;
    LatencyBypass bypass_;
    std::vector<float> dryLeft_;
    std::vector<float> dryRight_;
    std::vector<float> wetLeft_;
    std::vector<float> wetRight_;
    OutputMeterFeed meterFeed_ {};
    HeldNotes heldNotes_ {};
    HeldNotesFeed heldFeed_ {};

    // Between threads: lock-free only.
    std::array<std::atomic<float>, static_cast<std::size_t> (kNumParams)> plain_ {};
    std::atomic<bool> bypassShadow_ {false};
    std::array<std::atomic<float>, 2> meterPeaks_ {};
    std::array<std::atomic<std::uint32_t>, static_cast<std::size_t> (kHeldNoteWords)> heldWords_ {};
    SpscQueue<UiEdit, 1024> uiEdits_ {};
    UiNoteQueue uiNotes_ {};

    friend EditorType;
    std::unique_ptr<EditorType> editor_;
    double uiProgram_ = 0.0; // the panel's preset list selection, across editors (main thread)
};

} // namespace kraftonic::formats::clap
