// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — shared VST3 controller.
//
//  `Controller<Traits>` publishes one VST3 parameter per contract entry
//  (identity, stepCount and string conversion all derive from the traits
//  table and the product's formatting hook), the hidden MIDI adapter params
//  (marked kIsHidden so hosts don't list them), the bypass switch, and the
//  factory program list via IUnitInfo. Parameter changes that arrive off the
//  UI thread while an editor is open are deferred through PendingParamUpdates
//  and applied later on the UI thread, so VSTGUI listeners only ever run on
//  the UI thread. setComponentState adopts the processor's state format so
//  hosts see a single project state.
//
//  Products use this class directly (its createInstance/cid satisfy the
//  factory); the editor view comes from Traits::kUidescFile (nullptr means
//  "no editor", which is valid for test instruments).
// ============================================================================

#include "kraftonic/core/Parameters.h"
#include "kraftonic/formats/DisplayText.h"
#include "kraftonic/formats/HeldNotes.h"
#include "kraftonic/formats/ParamText.h"
#include "kraftonic/formats/PendingParamUpdates.h"
#include "kraftonic/formats/Preset.h"
#include "kraftonic/formats/StateCodec.h"
#include "kraftonic/formats/SteppedParam.h"
#include "kraftonic/formats/vst3/AdapterIds.h"
#include "kraftonic/formats/vst3/Vst3SdkIncludes.h"
#include "kraftonic/formats/vst3/Vst3Traits.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

namespace kraftonic::formats::vst3
{

/**
 * One contract parameter as a VST3 Parameter. Identity comes from the
 * contract (ParamId is the VST3 ParamID, stepCount comes from the table so
 * discrete parameters get stepped host sliders); value<->string conversion
 * always goes through the product's formatting hook so ranges, log scaling
 * and stepped names are defined exactly once, in the product.
 */
template <PluginTraits Traits>
class Parameter : public Steinberg::Vst::Parameter
{
public:
    explicit Parameter (const core::ParamInfo<typename Traits::ParamId>& contractInfo)
    {
        Steinberg::UString (this->info.title, USTRINGSIZE (this->info.title))
            .assign (contractInfo.name);
        if (contractInfo.unit != nullptr && contractInfo.unit[0] != '\0')
        {
            Steinberg::UString (this->info.units, USTRINGSIZE (this->info.units))
                .assign (contractInfo.unit);
        }
        this->info.flags = Steinberg::Vst::ParameterInfo::kCanAutomate;
        this->info.id = static_cast<Steinberg::Vst::ParamID> (contractInfo.id);
        this->info.stepCount = contractInfo.stepCount;
        this->info.defaultNormalizedValue
            = Traits::toNormalized (contractInfo.id, contractInfo.defaultValue);
        this->info.unitId = Steinberg::Vst::kRootUnitId;
        setPrecision (contractInfo.stepCount > 0 ? 0 : 4);
        setNormalized (this->info.defaultNormalizedValue);
    }

    void toString (Steinberg::Vst::ParamValue normalizedValue,
                   Steinberg::Vst::String128 string) const SMTG_OVERRIDE
    {
        char text[64] {};
        formatParamText<Traits> (static_cast<typename Traits::ParamId> (info.id), normalizedValue,
                                 text, sizeof (text));
        encodeDisplayText (text, string, 128);
    }

    bool fromString (const Steinberg::Vst::TChar* string,
                     Steinberg::Vst::ParamValue& normalizedValue) const SMTG_OVERRIDE
    {
        if (string == nullptr)
            return false;
        // String128 is UTF-16; decode to UTF-8 (ASCII plus U+2212 only) for parsing.
        char text[384] {};
        if (!decodeDisplayText (string, text, sizeof (text)))
            return false;
        return parseParamText<Traits> (static_cast<typename Traits::ParamId> (info.id), text,
                                       normalizedValue);
    }

    Steinberg::Vst::ParamValue toPlain (Steinberg::Vst::ParamValue normalizedValue) const SMTG_OVERRIDE
    {
        // Discrete parameters expose the step index (0..stepCount) as the
        // plain value — the SDK StringListParameter convention VSTGUI option
        // menus, segment buttons and stepped sliders rely on to pick their
        // entry. Continuous parameters keep the SDK default (identity).
        // toString/fromString are unaffected: they convert through the
        // product's engine-unit formatting hook, not through here.
        const auto id = static_cast<typename Traits::ParamId> (info.id);
        return stepIndexFromNormalized (Traits::paramInfo (id).stepCount, normalizedValue);
    }

    Steinberg::Vst::ParamValue toNormalized (Steinberg::Vst::ParamValue plainValue) const SMTG_OVERRIDE
    {
        const auto id = static_cast<typename Traits::ParamId> (info.id);
        return normalizedFromStepIndex (Traits::paramInfo (id).stepCount, plainValue);
    }
};

/** The shared bypass switch: a latched two-step parameter with kIsBypass. */
class BypassParameter : public Steinberg::Vst::Parameter
{
public:
    explicit BypassParameter (Steinberg::Vst::ParamID tag)
    {
        Steinberg::UString (this->info.title, USTRINGSIZE (this->info.title))
            .assign ("Bypass");
        this->info.flags = Steinberg::Vst::ParameterInfo::kCanAutomate
            | Steinberg::Vst::ParameterInfo::kIsBypass;
        this->info.id = tag;
        this->info.stepCount = 1;
        this->info.defaultNormalizedValue = 0.0;
        this->info.unitId = Steinberg::Vst::kRootUnitId;
        setPrecision (0);
        setNormalized (0.0);
    }

    void toString (Steinberg::Vst::ParamValue normalizedValue,
                   Steinberg::Vst::String128 string) const SMTG_OVERRIDE
    {
        Steinberg::UString (string, 128).assign (bypassText (normalizedValue));
    }

    bool fromString (const Steinberg::Vst::TChar* string,
                     Steinberg::Vst::ParamValue& normalizedValue) const SMTG_OVERRIDE
    {
        if (string == nullptr)
            return false;
        char text[384] {};
        if (!decodeDisplayText (string, text, sizeof (text)))
            return false;
        return parseBypassText (text, normalizedValue);
    }

    Steinberg::Vst::ParamValue toPlain (Steinberg::Vst::ParamValue normalizedValue) const SMTG_OVERRIDE
    {
        // Same step-index convention as Parameter<Traits> above (stepCount 1).
        return stepIndexFromNormalized (info.stepCount, normalizedValue);
    }

    Steinberg::Vst::ParamValue toNormalized (Steinberg::Vst::ParamValue plainValue) const SMTG_OVERRIDE
    {
        return normalizedFromStepIndex (info.stepCount, plainValue);
    }
};

template <PluginTraits Traits>
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public Steinberg::Vst::IMidiMapping,
                   public VSTGUI::VST3EditorDelegate
{
public:
    using ParamId = typename Traits::ParamId;
    using TraitsType = Traits;

    static Steinberg::FUnknown* createInstance (void* /*context*/)
    {
        return static_cast<Steinberg::Vst::IEditController*> (new Controller());
    }

    static Steinberg::FUID cid;

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        tresult result = EditControllerEx1::initialize (context);
        if (result != kResultOk)
            return result;

        if (Traits::numPresets() > 0)
        {
            // Root unit carries the factory program list so presets are
            // selectable in any host (IUnitInfo program list +
            // kIsProgramChange parameter).
            addUnit (new Unit (STR ("Root"), kRootUnitId, kNoParentUnitId,
                               static_cast<ProgramListID> (Traits::kProgramParamId)));
            String128 listName {};
            UString (listName, USTRINGSIZE (listName))
                .fromAscii (Traits::kProgramListName != nullptr
                                ? Traits::kProgramListName
                                : "Presets");
            auto* programList = new ProgramList (listName,
                                                 static_cast<ProgramListID> (
                                                     Traits::kProgramParamId),
                                                 kRootUnitId);
            for (std::size_t i = 0; i < Traits::numPresets(); ++i)
            {
                String128 progName {};
                UString (progName, USTRINGSIZE (progName))
                    .fromAscii (Traits::presets()[i].name);
                programList->addProgram (progName);
            }
            addProgramList (programList);
            parameters.addParameter (programList->getParameter());
        }

        // Every contract parameter, with the ParamId as the VST3 ParamID.
        const auto* table = Traits::paramTable();
        for (int p = 0; p < Traits::kNumParams; ++p)
            parameters.addParameter (new Parameter<Traits> (table[p]));

        // Hidden, read-only adapter parameters: MIDI CCs reach the engine
        // through IMidiMapping plus sample-offset parameter changes in
        // process data, which these flags do not gate. kIsHidden alone still
        // leaves them in host parameter lists; since SDK 3.7.0 the hidden
        // contract also implies kIsReadOnly (and never kCanAutomate), which
        // keeps them out of host lists and generic UIs. Never persisted.
        const int32 adapterFlags = ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly;
        parameters.addParameter (STR16 ("Sustain Pedal"), nullptr, 0, 0.0,
                                 adapterFlags,
                                 static_cast<ParamID> (Traits::kSustainParamId));
        parameters.addParameter (STR16 ("All Notes Off"), nullptr, 0, 0.0,
                                 adapterFlags,
                                 static_cast<ParamID> (Traits::kAllNotesOffParamId));
        parameters.addParameter (STR16 ("All Sound Off"), nullptr, 0, 0.0,
                                 adapterFlags,
                                 static_cast<ParamID> (Traits::kAllSoundOffParamId));
        parameters.addParameter (STR16 ("Mod Wheel"), nullptr, 0, 0.0,
                                 adapterFlags,
                                 static_cast<ParamID> (Traits::kModWheelParamId));
        parameters.addParameter (STR16 ("Pitch Bend"), nullptr, 0, 0.5,
                                 adapterFlags,
                                 static_cast<ParamID> (Traits::kPitchBendParamId));
        parameters.addParameter (
            new BypassParameter (static_cast<ParamID> (Traits::kBypassParamId)));
        if constexpr (HasOutputMeterIds<Traits>)
        {
            // Output-level meter pair (processor -> host -> here, ~30 Hz).
            // Same hidden + read-only contract as the adapter params above:
            // hosts never list or automate them, they are never persisted,
            // and no view binds them (meters poll outputMeterLevel() below).
            parameters.addParameter (
                STR16 ("Output Level L"), nullptr, 0, 0.0, adapterFlags,
                static_cast<ParamID> (Traits::kMeterLevelParamIdL));
            parameters.addParameter (
                STR16 ("Output Level R"), nullptr, 0, 0.0, adapterFlags,
                static_cast<ParamID> (Traits::kMeterLevelParamIdR));
        }
        if constexpr (HasUiKeyboard<Traits>)
        {
            // Held-note feedback words (processor -> host -> here, ~30 Hz).
            // Same hidden + read-only contract as the meters; panels poll
            // isNoteHeld() below.
            for (int w = 0; w < kHeldNoteWords; ++w)
            {
                parameters.addParameter (
                    STR16 ("Held Notes"), nullptr, 0, 0.0, adapterFlags,
                    static_cast<ParamID> (Traits::kHeldNotesParamIdBase
                                          + static_cast<std::uint32_t> (w)));
            }
        }
        // Depend on ourselves so deferUpdate() below is actually delivered:
        // the UpdateHandler drops deferred messages for objects with no
        // dependents, and nothing else depends on the controller.
        addDependent (this);
        return kResultOk;
    }

    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        removeDependent (this);
        return Steinberg::Vst::EditControllerEx1::terminate();
    }

    void applyPreset (int index)
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        const FactoryPreset* preset = presetAt<Traits> (index);
        if (preset == nullptr)
            return;
        const typename Traits::Params params = presetToParameters<Traits> (*preset);
        const auto* table = Traits::paramTable();
        for (int p = 0; p < Traits::kNumParams; ++p)
        {
            EditControllerEx1::setParamNormalized (
                static_cast<ParamID> (table[p].id),
                Traits::toNormalized (table[p].id, params.get (table[p].id)));
        }
        if (componentHandler != nullptr)
            componentHandler->restartComponent (kParamValuesChanged);
    }

    /**
     * Latest output peak, linear 0..1, for channel 0 (L) or 1 (R). Anything
     * outside 0/1 reads as L. Safe from any thread (relaxed atomics); the
     * panel polls this on a ~30 Hz UI timer and drives KraftLedMeter
     * (setLevel/setPeak with product-side peak-hold/decay). Always 0 when
     * the traits opt out of the meter path.
     */
    float outputMeterLevel (int channel) const noexcept
    {
        if constexpr (HasOutputMeterIds<Traits>)
        {
            return meterLevels_[channel == 1 ? 1 : 0].load (std::memory_order_relaxed);
        }
        else
        {
            (void)channel;
            return 0.0F;
        }
    }

    /**
     * Plays (on = true) or releases an on-screen key: sends the note to the
     * processor as an IMessage (kUiNoteMessageId), which injects it at the
     * next block start. UI thread. No-op without HasUiKeyboard traits or
     * before the host connected the processor. Returns true when sent.
     */
    bool sendUiNote (int note, float velocity, bool on)
    {
        if constexpr (HasUiKeyboard<Traits>)
        {
            if (note < 0 || note > 127)
                return false;
            Steinberg::IPtr<Steinberg::Vst::IMessage> message
                = Steinberg::owned (allocateMessage());
            if (!message)
                return false;
            message->setMessageID (kUiNoteMessageId);
            Steinberg::Vst::IAttributeList* attributes = message->getAttributes();
            if (attributes == nullptr)
                return false;
            float clamped = velocity;
            if (!(clamped >= 0.0F))
                clamped = 0.0F;
            else if (clamped > 1.0F)
                clamped = 1.0F;
            attributes->setInt ("note", note);
            attributes->setFloat ("velocity", static_cast<double> (clamped));
            attributes->setInt ("on", on ? 1 : 0);
            return sendMessage (message) == Steinberg::kResultOk;
        }
        else
        {
            (void)note;
            (void)velocity;
            (void)on;
            return false;
        }
    }

    /**
     * True while `note` is held according to the processor's ~30 Hz
     * feedback (host MIDI and on-screen keys alike). Any thread (relaxed
     * atomics). Always false without HasUiKeyboard traits.
     */
    bool isNoteHeld (int note) const noexcept
    {
        if constexpr (HasUiKeyboard<Traits>)
        {
            if (note < 0 || note > 127)
                return false;
            const std::uint32_t word
                = heldWords_[note / kHeldNoteWordBits].load (std::memory_order_relaxed);
            return ((word >> (note % kHeldNoteWordBits)) & 1u) != 0;
        }
        else
        {
            (void)note;
            return false;
        }
    }

    Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized (
        Steinberg::Vst::ParamID tag) SMTG_OVERRIDE
    {
        if constexpr (HasUiKeyboard<Traits>)
        {
            const int w = heldWordIndex (tag);
            if (w >= 0)
            {
                return HeldNotes::wordToNormalized (static_cast<std::uint16_t> (
                    heldWords_[w].load (std::memory_order_relaxed)));
            }
        }
        if constexpr (HasOutputMeterIds<Traits>)
        {
            if (tag == static_cast<Steinberg::Vst::ParamID> (Traits::kMeterLevelParamIdL))
                return meterLevels_[0].load (std::memory_order_relaxed);
            if (tag == static_cast<Steinberg::Vst::ParamID> (Traits::kMeterLevelParamIdR))
                return meterLevels_[1].load (std::memory_order_relaxed);
        }
        return EditControllerEx1::getParamNormalized (tag);
    }

    Steinberg::tresult PLUGIN_API setParamNormalized (
        Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if constexpr (HasOutputMeterIds<Traits>)
        {
            const auto meterL = static_cast<Vst::ParamID> (Traits::kMeterLevelParamIdL);
            const auto meterR = static_cast<Vst::ParamID> (Traits::kMeterLevelParamIdR);
            if (tag == meterL || tag == meterR)
            {
                // Meter traffic (~30 Hz x2) never enters the UI-deferral
                // queue: nothing binds these tags, so the mutex + idle
                // churn would buy nothing. Cache and return.
                float clamped = static_cast<float> (value);
                if (!(clamped >= 0.0F)) // negatives and NaN rest the meter at 0
                    clamped = 0.0F;
                else if (clamped > 1.0F)
                    clamped = 1.0F;
                meterLevels_[tag == meterR ? 1 : 0].store (clamped,
                                                          std::memory_order_relaxed);
                return kResultOk;
            }
        }
        if constexpr (HasUiKeyboard<Traits>)
        {
            const int w = heldWordIndex (tag);
            if (w >= 0)
            {
                // Held-note words bypass the UI deferral like the meters.
                heldWords_[w].store (HeldNotes::wordFromNormalized (value),
                                     std::memory_order_relaxed);
                return kResultOk;
            }
        }
        if (needsUiDeferral())
        {
            // Off the UI thread with an editor open: writing the parameter
            // now would run its VSTGUI listeners on this thread. Queue it
            // for the UI thread instead (preset expansion happens there).
            enqueueParamForUi (tag, value);
            deferUpdate (kDeferredParamMessage);
            return kResultOk;
        }
        if (!draining_)
            drainPendingParams();
        tresult result = EditControllerEx1::setParamNormalized (tag, value);
        if (result == kResultOk && Traits::numPresets() > 0
            && tag == static_cast<Vst::ParamID> (Traits::kProgramParamId))
        {
            const auto last
                = static_cast<Vst::ParamValue> (Traits::numPresets() - 1);
            int index = static_cast<int> (value * last + 0.5);
            if (index < 0)
                index = 0;
            if (index >= static_cast<int> (Traits::numPresets()))
                index = static_cast<int> (Traits::numPresets()) - 1;
            // Same rule as the processor: only a changed selection applies a
            // preset, and the first value after a state load is adopted.
            if (appliedProgram_ == kAdoptNextProgram)
                appliedProgram_ = index;
            else if (index != appliedProgram_)
            {
                appliedProgram_ = index;
                applyPreset (index);
            }
        }
        return result;
    }

    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        if (state == nullptr)
            return kResultFalse;
        appliedProgram_ = kAdoptNextProgram; // loaded values win over a re-sent program
        // Same shared format as the processor: drain, decode, adopt known IDs.
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
        typename Traits::Params params = Traits::Params::defaults();
        if (!decodeState<Traits> (bytes, total, params))
            return kResultFalse;
        if (needsUiDeferral())
        {
            // Same hazard as setParamNormalized, once per parameter: queue
            // the whole snapshot for the UI thread instead of touching
            // VSTGUI here.
            const auto* table = Traits::paramTable();
            for (int p = 0; p < Traits::kNumParams; ++p)
            {
                enqueueParamForUi (static_cast<ParamID> (table[p].id),
                                   Traits::toNormalized (table[p].id,
                                                         params.get (table[p].id)));
            }
            deferUpdate (kDeferredParamMessage);
            return kResultOk;
        }
        if (!draining_)
            drainPendingParams();
        const auto* table = Traits::paramTable();
        for (int p = 0; p < Traits::kNumParams; ++p)
            setParamNormalized (static_cast<ParamID> (table[p].id),
                                Traits::toNormalized (table[p].id,
                                                      params.get (table[p].id)));
        return kResultOk;
    }

    bool needsUiDeferral()
    {
        std::lock_guard<std::mutex> lock (uiMutex_);
        return uiThreadKnown_ && editorsAlive_ > 0
            && std::this_thread::get_id() != uiThreadId_;
    }

    void enqueueParamForUi (Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value)
    {
        pending_.add (static_cast<std::uint32_t> (tag), static_cast<double> (value));
    }

    void drainPendingParams()
    {
        // UI thread only: callers either run there (update, willClose, or the
        // direct paths above) or run with no editor open and no listeners.
        const auto queued = pending_.take();
        if (queued.empty())
            return;
        draining_ = true;
        for (const auto& queuedUpdate : queued)
        {
            // Back through the override: on the UI thread this takes the
            // direct path, so program changes still expand to presets here.
            setParamNormalized (static_cast<Steinberg::Vst::ParamID> (queuedUpdate.id),
                                queuedUpdate.normalized);
        }
        draining_ = false;
    }

    void PLUGIN_API update (Steinberg::FUnknown* changedUnknown,
                            Steinberg::int32 message) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if (message == kDeferredParamMessage)
        {
            // Delivered by the editor's idle timer via triggerDeferedUpdates,
            // so this runs on the UI thread: queued writes may touch VSTGUI.
            drainPendingParams();
            return;
        }
        EditControllerEx1::update (changedUnknown, message);
    }

    void didOpen (VSTGUI::VST3Editor* /*editor*/) SMTG_OVERRIDE
    {
        std::lock_guard<std::mutex> lock (uiMutex_);
        uiThreadId_ = std::this_thread::get_id();
        uiThreadKnown_ = true;
    }

    void willClose (VSTGUI::VST3Editor* /*editor*/) SMTG_OVERRIDE
    {
        // Still on the UI thread with the listeners alive: apply anything
        // the idle timer has not delivered yet so no update is lost.
        drainPendingParams();
    }

    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        // The bypass switch is controller state: hosts persist it through
        // the controller chunk (the processor chunk never carries it), and
        // the validator's bypass-persistence test requires the round-trip.
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        if (state == nullptr)
            return kResultFalse;
        IBStreamer streamer (state, kLittleEndian);
        std::uint8_t bytes[8] {};
        if (streamer.readRaw (bytes, 8) != 8)
            return kResultFalse;
        const double bypass = state_detail::readF64LE (bytes);
        if (!(bypass >= 0.0 && bypass <= 1.0))
            return kResultFalse;
        // Back through the override so UI-thread deferral still applies.
        setParamNormalized (static_cast<ParamID> (Traits::kBypassParamId),
                            bypass >= 0.5 ? 1.0 : 0.0);
        return kResultOk;
    }

    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        if (state == nullptr)
            return kResultFalse;
        const ParamValue bypass = EditControllerEx1::getParamNormalized (
            static_cast<ParamID> (Traits::kBypassParamId));
        std::uint8_t bytes[8] {};
        state_detail::writeF64LE (bytes, bypass);
        IBStreamer streamer (state, kLittleEndian);
        return streamer.writeRaw (bytes, 8) == 8 ? kResultOk : kResultFalse;
    }

    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if (Traits::kUidescFile == nullptr)
            return nullptr; // test instruments may ship without an editor
        ConstString viewName (name);
        if (viewName == Vst::ViewType::kEditor)
        {
            // Defer off-UI-thread parameter writes from the moment the editor
            // EXISTS, not only once didOpen() runs: VST3Editor binds its
            // parameter listeners while it opens, before didOpen(), and a
            // host updating parameters from the audio thread in that window
            // would touch half-built VSTGUI views (pluginval "Open editor
            // whilst processing" crashed about 1 run in 8).
            {
                std::lock_guard<std::mutex> lock (uiMutex_);
                uiThreadId_ = std::this_thread::get_id(); // hosts create views on the UI thread
                uiThreadKnown_ = true;
                ++editorsAlive_;
            }
            return new VSTGUI::VST3Editor (this, Traits::kViewName, Traits::kUidescFile);
        }
        return nullptr;
    }

    /** EditorView's destructor calls this (SDK hook), so deferral ends
        exactly when the last editor is gone, including editors that were
        created but never opened. Runs on the UI thread. */
    void editorDestroyed (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE
    {
        EditControllerEx1::editorDestroyed (editor);
        bool last = false;
        {
            std::lock_guard<std::mutex> lock (uiMutex_);
            if (editorsAlive_ > 0)
                --editorsAlive_;
            last = editorsAlive_ == 0;
        }
        if (last)
            drainPendingParams(); // no views left: queued values apply directly
    }

    VSTGUI::IController* createSubController (
        VSTGUI::UTF8StringPtr /*name*/, const VSTGUI::IUIDescription* /*description*/,
        VSTGUI::VST3Editor* /*editor*/) SMTG_OVERRIDE
    {
        return nullptr; // plain parameter bindings; override in a subclass for more
    }

    Steinberg::tresult PLUGIN_API getMidiControllerAssignment (
        Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
        Steinberg::Vst::CtrlNumber midiControllerNumber,
        Steinberg::Vst::ParamID& tag) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if (busIndex != 0)
            return kResultFalse;
        if (midiControllerNumber == Vst::kCtrlSustainOnOff)
        {
            tag = static_cast<Vst::ParamID> (Traits::kSustainParamId);
            return kResultTrue;
        }
        if (midiControllerNumber == Vst::kCtrlAllNotesOff)
        {
            tag = static_cast<Vst::ParamID> (Traits::kAllNotesOffParamId);
            return kResultTrue;
        }
        if (midiControllerNumber == Vst::kCtrlAllSoundsOff)
        {
            tag = static_cast<Vst::ParamID> (Traits::kAllSoundOffParamId);
            return kResultTrue;
        }
        if (midiControllerNumber == Vst::kCtrlModWheel)
        {
            tag = static_cast<Vst::ParamID> (Traits::kModWheelParamId);
            return kResultTrue;
        }
        if (midiControllerNumber == Vst::kPitchBend)
        {
            tag = static_cast<Vst::ParamID> (Traits::kPitchBendParamId);
            return kResultTrue;
        }
        return kResultFalse;
    }

    DELEGATE_REFCOUNT (Steinberg::Vst::EditController)
    Steinberg::tresult PLUGIN_API queryInterface (
        const Steinberg::TUID _iid, void** obj) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        QUERY_INTERFACE (_iid, obj, Vst::IMidiMapping::iid, Vst::IMidiMapping)
        return EditController::queryInterface (_iid, obj);
    }

private:
    /** Index of a held-note word parameter, or -1 for any other tag. */
    static int heldWordIndex (Steinberg::Vst::ParamID tag) noexcept
    {
        if constexpr (HasUiKeyboard<Traits>)
        {
            const auto base = static_cast<std::uint32_t> (Traits::kHeldNotesParamIdBase);
            const auto id = static_cast<std::uint32_t> (tag);
            if (id >= base && id < base + static_cast<std::uint32_t> (kHeldNoteWords))
                return static_cast<int> (id - base);
        }
        (void)tag;
        return -1;
    }

    static constexpr int kAdoptNextProgram = -2;
    int appliedProgram_ = -1; // factory program last applied (-1 none yet)
    // Deferred parameter updates: `Parameter::setNormalized` notifies the
    // editor's listeners synchronously on the calling thread, so parameter
    // changes that arrive off the UI thread while an editor is open are
    // queued here and applied later on the UI thread (see update()).
    // Well outside the IDependent::ChangeMessage range (0..3).
    static constexpr Steinberg::int32 kDeferredParamMessage = 0x53745571;

    // Guarded by uiMutex_; taken only on the UI thread and on host threads
    // calling into the controller, never on the audio thread (the processor
    // owns its own state and never touches these).
    std::mutex uiMutex_;
    std::thread::id uiThreadId_;
    bool uiThreadKnown_ = false;
    int editorsAlive_ = 0; // created and not yet destroyed (see createView)
    kraftonic::formats::PendingParamUpdates pending_;
    bool draining_ = false; // UI thread only, guards drain re-entry
    // Output-meter cache (linear peaks, L/R). Written by setParamNormalized
    // on host threads, read by the panel on the UI thread: relaxed atomics,
    // no lock, never the deferral queue.
    std::atomic<float> meterLevels_[2] {};
    // Held-note feedback words (16 notes each), same threading as the meters.
    std::atomic<std::uint32_t> heldWords_[kHeldNoteWords] {};
};

template <PluginTraits Traits>
Steinberg::FUID Controller<Traits>::cid (Traits::kControllerFuid[0],
                                        Traits::kControllerFuid[1],
                                        Traits::kControllerFuid[2],
                                        Traits::kControllerFuid[3]);

} // namespace kraftonic::formats::vst3
