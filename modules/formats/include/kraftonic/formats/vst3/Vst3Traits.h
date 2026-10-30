// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — product traits contract.
//
//  A product instantiates the shared VST3 base with one traits struct that
//  binds its contract (ParamId enum, kParamInfo table, engine, parameters),
//  its identity (FUIDs, names, factory presets, state magic) and its display
//  formatting. The base never includes a product header; the product includes
//  this one. See modules/formats/README.md for a minimal example.
//
//  The `PluginTraits` concept checks the full contract so a mistake fails
//  here with a readable error instead of deep inside the VST3 templates.
// ============================================================================

#include "kraftonic/core/Instrument.h"
#include "kraftonic/core/Parameters.h"
#include "kraftonic/formats/Preset.h"

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace kraftonic::formats::vst3
{

template <typename T>
concept PluginTraits = requires (
    typename T::Params& params,
    const typename T::Params& cparams,
    typename T::Engine& engine,
    typename T::ParamId id,
    char* text,
    const char* ctext,
    float& plain)
{
    typename T::ParamId;
    typename T::Params;
    typename T::Engine;

    // Contract table.
    { T::kNumParams } -> std::convertible_to<int>;
    { T::paramTable() } -> std::convertible_to<const core::ParamInfo<typename T::ParamId>*>;
    { T::paramInfo (id) } -> std::convertible_to<const core::ParamInfo<typename T::ParamId>&>;
    { T::toPlain (id, 0.5) } -> std::convertible_to<float>;
    { T::toNormalized (id, 1.0F) } -> std::convertible_to<double>;

    // Parameter struct.
    { T::Params::defaults() } -> std::same_as<typename T::Params>;
    { cparams.get (id) } -> std::convertible_to<float>;
    params.set (id, 1.0F);

    // Engine with the core::Instrument-shaped API.
    engine.prepare (std::declval<const core::ProcessSetup&>());
    engine.reset();
    engine.setParameters (cparams);
    engine.process (std::declval<const core::AudioBlock&>(), nullptr, 0);

    // State identity.
    { T::kStateMagic } -> std::convertible_to<std::uint32_t>;
    { T::kStateVersion } -> std::convertible_to<std::uint32_t>;

    // Factory presets.
    { T::numPresets() } -> std::convertible_to<std::size_t>;
    { T::presets() } -> std::convertible_to<const FactoryPreset*>;

    // Plug-in identity.
    { T::kProcessorFuid[0] } -> std::convertible_to<std::uint32_t>;
    { T::kControllerFuid[0] } -> std::convertible_to<std::uint32_t>;
    { T::kVendor } -> std::convertible_to<const char*>;
    { T::kPluginName } -> std::convertible_to<const char*>;
    { T::kControllerName } -> std::convertible_to<const char*>;
    { T::kVendorUrl } -> std::convertible_to<const char*>;
    { T::kVendorEmail } -> std::convertible_to<const char*>;

    // Adapter-local parameter IDs (see AdapterIds.h for the defaults).
    { T::kSustainParamId } -> std::convertible_to<std::uint32_t>;
    { T::kAllNotesOffParamId } -> std::convertible_to<std::uint32_t>;
    { T::kAllSoundOffParamId } -> std::convertible_to<std::uint32_t>;
    { T::kModWheelParamId } -> std::convertible_to<std::uint32_t>;
    { T::kPitchBendParamId } -> std::convertible_to<std::uint32_t>;
    { T::kProgramParamId } -> std::convertible_to<std::uint32_t>;
    { T::kBypassParamId } -> std::convertible_to<std::uint32_t>;
    { T::kProgramListName } -> std::convertible_to<const char*>;

    // Editor (nullptr kUidescFile means "no editor view").
    { T::kUidescFile } -> std::convertible_to<const char*>;
    { T::kViewName } -> std::convertible_to<const char*>;

    // Value formatting hook (hosts and the editor share these strings).
    T::formatPlainValue (id, 1.0F, text, std::size_t {64});
    { T::parsePlainValue (id, ctext, plain) } -> std::convertible_to<bool>;

    // Tail length in seconds (for getTailSamples).
    { T::tailSeconds (cparams) } -> std::convertible_to<double>;
};

/**
 * Opt-in output-meter IDs. A product whose traits expose
 * `kMeterLevelParamIdL/R` (usually the AdapterIds defaults) gets the shared
 * meter path: the processor emits stereo peaks at ~30 Hz and the controller
 * caches them for UI polling. Traits without these members are unaffected
 * (the processor sends nothing, the controller serves silence).
 */
template <typename T>
concept HasOutputMeterIds = requires {
    { T::kMeterLevelParamIdL } -> std::convertible_to<std::uint32_t>;
    { T::kMeterLevelParamIdR } -> std::convertible_to<std::uint32_t>;
};

/**
 * Opt-in on-screen keyboard path. A product whose traits expose
 * `kHeldNotesParamIdBase` (usually vst3::kDefaultHeldNotesParamIdBase) gets
 * both directions: Controller::sendUiNote() reaches the processor as an
 * IMessage and is injected as noteOn/noteOff at the next block start, and
 * the processor reports the notes currently held (MIDI + UI) at ~30 Hz for
 * Controller::isNoteHeld(). Traits without the member are unaffected.
 */
template <typename T>
concept HasUiKeyboard = requires {
    { T::kHeldNotesParamIdBase } -> std::convertible_to<std::uint32_t>;
};

/** Opt-in channel aftertouch translation for products that consume pressure. */
template <typename T>
concept HasChannelPressure = requires {
    { T::kChannelPressureEnabled } -> std::convertible_to<bool>;
} && T::kChannelPressureEnabled;

/** Opt-in host tempo (BPM) for engines with tempo-synced parts. */
template <typename T>
concept HasEngineTempo = requires (T& engine, double tempo)
{
    { engine.setTempo (tempo) } noexcept;
};

/** Opt-in host transport: playing, and the song position in quarter notes. */
template <typename T>
concept HasEngineTransport = requires (T& engine, bool playing, bool positionValid, double ppq)
{
    { engine.setTransport (playing, positionValid, ppq) } noexcept;
};

} // namespace kraftonic::formats::vst3
