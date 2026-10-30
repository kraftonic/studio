# Architecture

This repository is an open-source kit: shared modules, the plug-in format layer (VST3, CLAP, AU), the panel kit and two starter templates. If this file and the code disagree, the code wins: fix this file.

## Targets and dependencies

From `target_link_libraries`. One naming rule: `modules/X` → `#include <kraftonic/X/...>` → `namespace kraftonic::X` → target `kraftonic::X` → test prefix `X: `. Plug-ins in `plugins/<p>/` are not part of the kit: plain `<p>` namespace and targets.

```
kraftonic::core        STATIC    (no deps)
kraftonic::fx          STATIC    → core
kraftonic::sampler     STATIC    → core
kraftonic::synth       STATIC    → core                          oscillators, filters, modulation
kraftonic::piano       STATIC    → core (sampler, fx private)    piano layer
kraftonic::wavetable   STATIC    → core, fx                      wavetable synth engine
kraftonic::formats     INTERFACE → core                          SDK-free host helpers
kraftonic::vst3        INTERFACE → formats, VST3 SDK, vstgui     header-only templates over plug-in traits

<p>_dsp                STATIC    → core (+ fx, sampler as needed)
  startersynth_dsp     → core, synth
  startereffect_dsp    → core, fx
Kraftonic<P>.vst3      → <p>_dsp + kraftonic::vst3
```

- `kraftonic::vst3` is built only with `KRAFTONIC_BUILD_PLUGINS=ON`. With it off, everything else builds and tests without the SDK.
- `modules/ui` provides the shared bitmap kit, panel generator and tools; each product's `ui/` generator script writes its `.uidesc` from it.
- These never include a plug-in SDK, UI toolkit or OS header: `core`, `fx`, `sampler` and `plugins/*/dsp`. The sampler uses only the standard library, with `#if` platform defaults for sample paths.

## Contracts

| Contract | Header | Shape |
|---|---|---|
| Parameters | `core/Parameters.h` | Each product has a `ParamInfo<ParamId>` table: id = VST3 ParamID, stable `key`, range, default, linear/log, `stepCount`. `toPlain` / `toNormalized`. Engines get **plain** values. |
| Instrument I/O | `core/Instrument.h` | `ProcessSetup{sampleRate 22.05–384 kHz, maxBlockSize}`. `Event{noteOn, noteOff, sustainPedal, allNotesOff, allSoundOff, modWheel, pitchBend}` with a sample offset. `AudioBlock` = 2 non-interleaved float channels that the engine overwrites. At most `kMaxEventsPerBlock` = 512 events per block. |
| Effects | `fx/Effect.h` | `StereoEffect` concept: `prepare` (not real-time) / `reset` / `setParameters` / `process(L, R, n)` in place / `latencySamples`. Plain-value `Parameters` structs; never product ParamIds. |
| Product engine | `plugins/<p>/dsp/*.h` | `<P>Engine` + `<P>Parameters` (the table). Shipped ids and keys never change. |
| Plug-in traits | `formats/vst3/Vst3Traits.h` (`PluginTraits`), `formats/vst3/effect/EffectTraits.h` | A product describes identity, table, engine, presets, formatting and state magic in `<P>Traits.h`. The base then provides the processor, controller, state, presets, MIDI mapping and factory. |

## Products

| Product | Kind | Plug-in layer |
|---|---|---|
| Starter Synth (`plugins/startersynth`) | template instrument: 8 voices, 2 oscillators, resonant low-pass, ADSR | shared VST3 instrument base |
| Starter Effect (`plugins/startereffect`) | template effect: drive into a stereo delay, zero latency | shared VST3 effect base (`formats/vst3/effect`) |

Start a new product from either with `scripts/new-product.sh <name> --from synth|effect`.

`KraftonicPluginSelfTest` (`modules/formats/tests/selftest`) is a test-only instrument built on the base. It is never packaged.

## Threads

| Thread | Runs | Rules |
|---|---|---|
| Audio (host process call) | `Processor::process` → engine `setParameters` + `process`; queue drains | No allocation, locks, waits, I/O, logging or exceptions |
| Host message thread | `Processor::notify` (UI notes from the controller) | Pushes into the lock-free `UiNoteQueue` (SPSC, 256 entries; when full, the newest note is dropped) |
| Controller / UI thread | `Controller`, VSTGUI views, sub-controllers, `CVSTGUITimer` (~30 Hz) | Mutexes are allowed here (`PendingParamUpdates`) |
| Non-real-time setup | `prepare` / `setupProcessing` / `setActive` | Allocation and file I/O happen here (for example loading sample banks in `prepare()`) |

Audio → UI data (output meters, held notes for key lighting) travels as **hidden, read-only output parameters** at about 30 Hz. The controller caches them in relaxed atomics.

## State and presets

- `formats/StateCodec.h` is a versioned, little-endian format of plain values keyed by ParamId, with a magic number per product (`kStateMagic` in `<P>Traits.h`).
- Factory presets are sets of plain values keyed by the stable string `key`, in `plugins/<p>/plugin/<P>Presets.h`.

## Build, test, validate

| Option | Default | Effect |
|---|---|---|
| `KRAFTONIC_BUILD_PLUGINS` | ON | downloads and pins VST3 SDK 3.8.1; builds VSTGUI, `kraftonic::vst3` and the `.vst3` bundles |
| `KRAFTONIC_BUILD_TESTS` | ON | Catch2 v3.16.0; one test executable per module and product |
| `KRAFTONIC_BUILD_STARTERS`, `KRAFTONIC_BUILD_MODULE_TESTS` | ON at top level, OFF as a subproject | the starters; the module tests (see BUILDING.md, "Studio as a dependency of another repository") |
| `KRAFTONIC_WARNINGS_AS_ERRORS` | OFF (CI: ON) | treat warnings in Kraftonic sources as errors |

- **Test names:** prefixed with the module or product (`core:`, `fx:`, `sampler:`, `synth:`, `piano:`, `wavetable:`, `formats:`, `formats: vst3:`, `ui:`, `startersynth:`, `startereffect:`). Each has a separate `<prefix>: realtime and performance` test.
- **Commands:** [BUILDING.md](BUILDING.md), [SPEC §4](SPEC.md#4-build-commands-macos-dev-machine) and the `verification` skill.
- **Check:** `scripts/check.sh <product>` (native build, that product's tests and the Steinberg validator; module tests and every product when shared code changes; `--full` adds pluginval).
- **Pull requests:** GitHub Actions builds and validates every plug-in on macOS, Windows and Linux.

## Risks and known gaps

1. **Module changes affect downstream plug-ins.** Plug-ins in their own repositories build on these modules, so shared contract headers change only after a maintainer agrees in an issue.
2. **Linux and Windows:**
   - There is no automated editor test on Linux ([KNOWN_ISSUES](KNOWN_ISSUES.md)).
   - Windows and Linux are built and validated in CI only; `scripts/check.sh` runs on the developer's own OS.
3. **Formatting isn't enforced.** There is no formatter config; style is checked in review only.
