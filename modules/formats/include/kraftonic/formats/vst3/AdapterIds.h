// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — default adapter-local parameter IDs
//
//  Hidden, non-automatable VST3 parameters used only so that MIDI
//  controllers can reach the engine through IMidiMapping and sample-offset
//  parameter changes. They live outside any product's contract range
//  [0, kNumParams), so they can never collide with a present or future
//  contract ParamId, and they are never persisted. A product may override
//  individual IDs in its traits, but the defaults below are recommended so
//  every Kraftonic instrument behaves the same under MIDI control.
// ============================================================================

#include <cstdint>

namespace kraftonic::formats::vst3
{

inline constexpr std::uint32_t kDefaultSustainParamId = 0x10000u;    // CC64 damper pedal
inline constexpr std::uint32_t kDefaultAllNotesOffParamId = 0x10001u; // CC123
inline constexpr std::uint32_t kDefaultAllSoundOffParamId = 0x10002u; // CC120
inline constexpr std::uint32_t kDefaultModWheelParamId = 0x10003u;    // CC1 mod wheel
inline constexpr std::uint32_t kDefaultPitchBendParamId = 0x10004u;   // pitch bend

// Standard bypass switch (VST3 ParamID 'bypa'). Presented to hosts with the
// kIsBypass flag so it appears as the plug-in bypass control.
inline constexpr std::uint32_t kDefaultBypassParamId = 0x62797061u; // 'bypa'

// Factory preset selector (IUnitInfo program list + kIsProgramChange).
inline constexpr std::uint32_t kDefaultProgramParamId = 1000u;

// Output-level meter (stereo peak pair, linear 0..1). The processor observes
// each rendered block and emits the peaks at ~30 Hz through these read-only
// output parameters; the controller caches them for UI-thread polling (see
// OutputMeter.h and the meter section of modules/formats/README.md). Hidden
// and read-only so hosts never list or automate them; never persisted.
// A product opts in by exposing these IDs in its traits as
// kMeterLevelParamIdL/R (see the HasOutputMeterIds concept); products
// without them build exactly as before and send nothing.
inline constexpr std::uint32_t kDefaultMeterLevelParamIdL = 0x10005u;
inline constexpr std::uint32_t kDefaultMeterLevelParamIdR = 0x10006u;

// On-screen keyboard feedback ("notes currently held", see HeldNotes.h):
// kHeldNoteWords (8) consecutive hidden read-only output parameters starting
// here, 16 notes each. A product opts in to the whole UI keyboard path (UI
// note messages in, held-note words out) by exposing this base in its
// traits as kHeldNotesParamIdBase (see the HasUiKeyboard concept).
inline constexpr std::uint32_t kDefaultHeldNotesParamIdBase = 0x10010u; // .. 0x10017

// IMessage the controller sends to the processor for an on-screen key.
// Attributes: "note" (int 0..127), "velocity" (float 0..1), "on" (int 0/1).
inline constexpr const char* kUiNoteMessageId = "KraftUiNote";

} // namespace kraftonic::formats::vst3
