# kraftonic::formats — plug-in format adapters

Format-neutral helpers live at the top of `include/kraftonic/formats/`; each
plug-in format gets its own subfolder beside them: VST3 (`vst3/`) and CLAP
(`clap/`), which share the helpers. On macOS the Audio Unit (AUv2) of each plug-in is built by wrapping its VST3
with the VST3 SDK's AU wrapper (`kraftonic_add_audio_unit` in
`cmake/KraftonicPlugin.cmake`), so there is no `au/` code here.

- `kraftonic::formats` — **SDK-free helpers** (`include/kraftonic/formats/*.h`).
  No host SDK, no UI toolkit, no OS headers. Unit-tested without a host in
  `tests/` (`kraftonic_formats_tests`, CTest prefix `formats: `).
- `kraftonic::vst3` — **shared VST3 base** (`include/kraftonic/formats/vst3/`,
  namespace `kraftonic::formats::vst3`). Templates over a product traits
  struct. Built only with `KRAFTONIC_BUILD_PLUGINS` (needs the Steinberg VST3
  SDK + VSTGUI). Tested in `tests/vst3/` (CTest prefix `formats: vst3: `).
- `kraftonic::clap` — **native CLAP adapter** (`include/kraftonic/formats/clap/`,
  namespace `kraftonic::formats::clap`), header-only over the same traits
  (CLAP headers, MIT, pinned in `cmake/KraftonicDependencies.cmake`). Built
  with `KRAFTONIC_BUILD_CLAP`. Tested in `tests/clap/` (CTest prefix
  `formats: clap: `); `scripts/check.sh` runs clap-validator on each `.clap`.

Products never copy adapter code: they describe themselves with traits and
get a processor, a controller, state, presets, MIDI mapping and a factory
entry for free: instruments run on the shared `vst3::Processor`, effects on
`vst3::effect::EffectProcessor` (see "Effect support"). Each product's state
uses the same versioned layout with its own magic number.

## Files

| File | What |
|------|------|
| `formats/EventHelpers.h` | MIDI/note → `core::Event` translation (audio-thread safe) |
| `formats/Preset.h` | key-based factory-preset application |
| `formats/StateCodec.h` | versioned state codec templated on traits (versioned layout, per-product magic) |
| `formats/DisplayText.h` | UTF-8/UTF-16 host-string bridge (ASCII + U+2212) |
| `formats/SteppedParam.h` | step-index plain-value convention for discrete params |
| `formats/OutputMeter.h` | SDK-free ~30 Hz stereo peak feed (see Meters below) |
| `formats/UiNoteQueue.h` | SDK-free lock-free SPSC queues (`SpscQueue`, on-screen keyboard notes) |
| `formats/ParamText.h` | value ↔ text for the host (the product's hook, clamped; bypass On/Off), shared by VST3 and CLAP |
| `formats/ParamSchedule.h` | sample-accurate automation schedule for effects |
| `formats/LatencyBypass.h` | latency-compensated click-free bypass |
| `formats/HeldNotes.h` | SDK-free 128-note held set + ~30 Hz change-only feed (key lighting) |
| `formats/PendingParamUpdates.h` | UI-thread parameter deferral queue |
| `formats/vst3/Vst3Traits.h` | the `PluginTraits` concept (the contract below) |
| `formats/vst3/Vst3Processor.h` | `Processor<Traits>`: events, automation, bypass, state |
| `formats/vst3/Vst3Controller.h` | `Controller<Traits>` + `Parameter<Traits>` + `BypassParameter` |
| `formats/vst3/Vst3Factory.h` | `KRAFTONIC_VST3_FACTORY` registration macro |
| `formats/vst3/AdapterIds.h` | default hidden-parameter / bypass / program IDs |
| `formats/vst3/Vst3SdkIncludes.h` | central SDK includes with project warnings suppressed |
| `formats/vst3/effect/BusLayout.h` | stereo/mono-in → stereo-out acceptance (SDK-free) |
| `formats/vst3/effect/EffectTraits.h` | `EffectTraits` concept + engine adapter |
| `formats/vst3/effect/Vst3EffectProcessor.h` | `EffectProcessor<Traits>` for effects |
| `formats/clap/ClapPlugin.h` | `clap::Plugin<Traits>`: params, state, notes, audio ports, latency, tail (instruments and effects) |
| `formats/clap/ClapEditor.h` | macOS: the product's VST3 controller and panel in the host's window |
| `formats/clap/ClapEntry.h` | `KRAFTONIC_CLAP_ENTRY` entry-point macro |

## What the base gives each product

- Processor: MIDI/event translation to `core::Event`, sample-accurate
  parameter changes (preset base, then last-value automation; the engine
  smooths), preallocated sorted event array (extra events dropped, never
  allocated), sustain CC64 via `IMidiMapping` onto a hidden parameter,
  all-notes/sound-off, bypass (latched `kIsBypass` switch; engaging resets
  the engine and emits silence), `getState`/`setState` through the codec.
- Controller: one VST3 parameter per contract entry (`ParamId` as ParamID,
  `stepCount` from the table so discrete params get stepped host sliders,
  display strings via the product's formatting hook), hidden adapter params
  marked `kIsHidden` (+ `kIsReadOnly`, so hosts don't list them), program
  list via `IUnitInfo` (only when the product has presets), UI-thread
  deferral via `PendingParamUpdates`, `setComponentState` adopting the
  processor's state format.
- Real-time rules hold: no allocation, locks, I/O, logging or exceptions on
  the audio thread; smoothed audible changes; finite output.

## Minimal product wiring

```cpp
// MyTraits.h — the only file a product writes (plus its DSP + uidesc).
#include "MyEngine.h"                       // core-shaped engine
#include "kraftonic/formats/Preset.h"
#include "kraftonic/formats/vst3/AdapterIds.h"

struct MyTraits
{
    using ParamId = my::ParamId;
    using Params = my::MyParameters;        // ::defaults/get/set
    using Engine = my::MyEngine;            // prepare/setParameters/process/reset
    static constexpr int kNumParams = my::kNumParams;
    static const my::ParamInfo* paramTable() noexcept { return my::kParamInfo.data(); }
    static const my::ParamInfo& paramInfo (ParamId id) noexcept { return my::paramInfo (id); }
    static float toPlain (ParamId id, double n) noexcept { return my::toPlain (id, n); }
    static double toNormalized (ParamId id, float p) noexcept { return my::toNormalized (id, p); }

    static constexpr std::uint32_t kStateMagic = 0x4754534Bu + 1u; // own magic!
    static constexpr std::uint32_t kStateVersion = 1u;

    static std::size_t numPresets() noexcept { return std::size (kPresets); }
    static const kraftonic::formats::FactoryPreset* presets() noexcept { return kPresets; }

    static constexpr std::uint32_t kProcessorFuid[4] = {/* uuidgen, permanent */};
    static constexpr std::uint32_t kControllerFuid[4] = {/* uuidgen, permanent */};
    static constexpr const char* kVendor = "Kraftonic";
    static constexpr const char* kPluginName = "Kraftonic Mine";
    static constexpr const char* kControllerName = "Kraftonic Mine Controller";
    static constexpr const char* kVendorUrl = "https://github.com/kraftonic/studio";
    static constexpr const char* kVendorEmail = "";

    static constexpr std::uint32_t kSustainParamId = kraftonic::formats::vst3::kDefaultSustainParamId;
    static constexpr std::uint32_t kAllNotesOffParamId = kraftonic::formats::vst3::kDefaultAllNotesOffParamId;
    static constexpr std::uint32_t kAllSoundOffParamId = kraftonic::formats::vst3::kDefaultAllSoundOffParamId;
    static constexpr std::uint32_t kProgramParamId = kraftonic::formats::vst3::kDefaultProgramParamId;
    static constexpr std::uint32_t kBypassParamId = kraftonic::formats::vst3::kDefaultBypassParamId;
    static constexpr const char* kProgramListName = "Factory Presets";

    static constexpr const char* kUidescFile = "mine.uidesc"; // nullptr = no editor
    static constexpr const char* kViewName = "view";

    static void formatPlainValue (ParamId id, float plain, char* text, std::size_t size) noexcept;
    static bool parsePlainValue (ParamId id, const char* text, float& plain) noexcept;
    static double tailSeconds (const Params& params) noexcept; // e.g. release + margin
};
```

```cpp
// MyEntry.cpp — factory (four lines).
#include "MyTraits.h"
#include "kraftonic/formats/vst3/Vst3Controller.h"
#include "kraftonic/formats/vst3/Vst3Processor.h"
#include "kraftonic/formats/vst3/Vst3Factory.h"

using MyProcessor = kraftonic::formats::vst3::Processor<MyTraits>;
using MyController = kraftonic::formats::vst3::Controller<MyTraits>;

KRAFTONIC_VST3_FACTORY (MyTraits, MyProcessor, MyController,
                        "1.0.0", Steinberg::Vst::PlugType::kInstrumentSynth)
```

```cpp
// Same file: the CLAP entry (empty in the VST3 build). kraftonic_add_plugin
// compiles the sources again as <Target>.clap, with the BUNDLE_IDENTIFIER as
// its permanent CLAP id.
#include "kraftonic/formats/clap/ClapEntry.h"
KRAFTONIC_CLAP_ENTRY (MyTraits, "1.0.0", CLAP_PLUGIN_FEATURE_INSTRUMENT,
                      CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO)
```

```cmake
# CMakeLists.txt — bundle name stays space-free (see comment in
# tests/selftest/CMakeLists.txt for why); display names come from traits.
smtg_add_vst3plugin(Mine PACKAGE_NAME "Mine" SOURCES_LIST MyEntry.cpp)
target_include_directories(Mine SYSTEM PRIVATE
    "${vst3sdk_SOURCE_DIR}" "${vst3sdk_SOURCE_DIR}/vstgui4")
target_link_libraries(Mine PRIVATE kraftonic::vst3) # sdk + VSTGUI come along
kraftonic_set_source_warnings(MyEntry.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE MyEntry.cpp APPEND PROPERTY
        COMPILE_OPTIONS "-Wno-non-virtual-dtor") # COM-style VST3 interfaces
endif()
```

Constraints the base assumes (checked by the `PluginTraits` concept):
contract ParamIds are dense `0..kNumParams-1`; discrete params are linear
with `stepCount > 0`; preset names are ASCII; `tailSeconds` is finite;
FUIDs are permanent once shipped.

Discrete parameters expose **step indices** (`0..stepCount`) as their VST3
plain values (`SteppedParam.h`; enforced by `Parameter<Traits>::toPlain` /
`toNormalized`). That is the SDK `StringListParameter` convention VSTGUI
option menus, segment buttons and stepped sliders need to show the right
entry; display strings still come from the product's engine-unit formatting
hook, so `toString`/`fromString` are unaffected. Products with a local
step-index workaround can delete it.

## Output-level meters (live panel meters, no automation)

Panels show output level on `KraftLedMeter` without automatable parameters:

1. **Opt in** — expose the meter IDs in the traits (usually the defaults):
   `kMeterLevelParamIdL/R` (`vst3::kDefaultMeterLevelParamIdL/R`). This
   satisfies the `HasOutputMeterIds` concept. Traits without these members
   build exactly as before and send nothing.
2. **Processor** — after every render (and on bypass/silence blocks, as
   zeros) the base folds the block peaks into `OutputMeterFeed` and emits
   one stereo pair at ~30 Hz through the host's output parameter queue.
   Real-time safe: float/int arithmetic plus two host queue writes, no
   allocation, locks, I/O or exceptions.
3. **Controller** — registers the pair as hidden + read-only (never listed,
   automated or persisted) and caches the latest peaks in relaxed atomics.
   Meter traffic short-circuits `setParamNormalized`: it never enters the
   UI-deferral queue. Panels read `Controller::outputMeterLevel (0/1)`.
4. **Panel** — a small subcontroller polls `outputMeterLevel()` on a ~30 Hz
   `CVSTGUITimer` (UI thread) and drives each meter's `setLevel`/`setPeak`,
   doing its own peak-hold/decay there. The starters' panels show the
   panel side of this feed.

## Generic UI note transport (UI notes in, held notes out)

Opt in by exposing one ID in the traits (the `HasUiKeyboard` concept):

```cpp
static constexpr std::uint32_t kHeldNotesParamIdBase = vst3::kDefaultHeldNotesParamIdBase;
```

That turns on both directions; traits without it build and behave exactly
as before.

- **UI -> audio.** `Controller::sendUiNote (note, velocity, on)` (UI thread)
  sends an `IMessage` (`kUiNoteMessageId`, attributes `note`, `velocity`,
  `on`) through the host's connection point. `Processor::notify` (host
  message thread) validates it and pushes it into a 256-entry lock-free
  single-producer/single-consumer `UiNoteQueue`. `process()` drains the
  queue at the start of the next block and injects `core::Event`
  noteOn/noteOff at offset 0, ahead of the block's host events. No
  allocation or locks on the audio thread; a full queue drops the newest
  note.
- **Audio -> UI.** The processor folds every merged event (host MIDI and UI
  notes; all-notes/sound-off clear) into a 128-bit held set and, on the
  same ~30 Hz cadence as the meters, sends the changed 16-note words as 8
  hidden read-only output parameters (`kHeldNotesParamIdBase + 0..7`,
  normalized = word / 65535, exact even if a host stores floats), with a
  full resend about once a second. `Controller::isNoteHeld (note)` reads
  the cached words (relaxed atomics, any thread); they never enter the UI
  deferral queue and are never persisted.
- **Product UI.** The shared adapter only transports notes and held-note
  state; it does not provide or depend on a UI toolkit. A product that
  shows a keyboard owns that view and its controller binding.

  Product panels can use `sendUiNote` and `isNoteHeld` from their own
  controller/view integration. The transport contract stays independent of
  the product UI implementation.

## Effect support (`vst3/effect/`)

`EffectProcessor<Traits>` turns host audio and automation into the SDK-free
effect engine contract for any traits satisfying `EffectTraits` (the whole
`PluginTraits` contract plus `process(block)` in place and
`latencySamples()`; `EffectEngineAdapter` covers the instrument-shaped
`process` the shared concept still demands, so effect engines never see
events). One stereo input (stereo, or mono feeding both channels) plus one
stereo output; sample-accurate automation via `ParamSchedule` (preset base,
then automation wins); latency from the engine with a delay-compensated
click-free bypass; tail via `Traits::tailSeconds()`; 32/64-bit samples,
chunked oversized blocks, in-place buffers; state through the shared codec.
`BusLayout`, `ParamSchedule` and `LatencyBypass` are SDK-free and unit-tested
in the fast loop (`EffectHelpersTests.cpp`); the processor itself is driven
with no host in `tests/vst3/EffectProcessorTests.cpp` (fake engine + traits).

## CLAP (`clap/`)

`clap::Plugin<Traits>` serves the same traits as the VST3 base: the same
parameter ids, ranges, defaults, stepped values and text (`ParamText.h`;
values follow the VST3 plain convention, step indices for stepped
parameters), the same `StateCodec` payload followed by the bypass value (so
a VST3 project's state loads, and the reverse), the same latency, tail and
bypass. Instruments split rendering at each parameter change and pass notes
at their sample offsets like `vst3::Processor`; effects use `ParamSchedule`
and `LatencyBypass` like `EffectProcessor`. The panel's edits and the host's
values meet in lock-free atomics and an SPSC queue; `params.flush` works
with or without processing.

The panel opens on macOS: `ClapEditor.h` creates the product's VST3
controller and VSTGUI editor inside the `.clap` bundle (which carries the
`.vst3`'s Resources) and embeds it in the host's view. On Windows and Linux
the CLAP plug-in has no editor yet: a `.clap` there is a single file with no
Resources folder for the `.uidesc`, bitmaps and fonts VSTGUI loads, and
Linux would also need a run-loop bridge for VSTGUI's timers. The on-screen
keyboard (`sendUiNote`) is not wired into CLAP; held-note lighting and the
meters are.

## Self-test instrument (proof, not product)

`tests/selftest/` holds `KraftonicPluginSelfTest`: a tiny 8-voice sine
instrument (gain + wave switch + attack) built directly on the base, with a
minimal stock-VSTGUI editor that binds all three parameters. It is never
installed or shipped. For a full product with a kit panel, see
`plugins/startersynth` (instrument) and `plugins/startereffect` (effect).
`kraftonic_selftest_render` (same folder) renders listening WAVs from the
engine with no host involved.
