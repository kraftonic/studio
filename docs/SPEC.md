# Kraftonic Studio: technical spec

Read first: [AGENTS.md](../AGENTS.md) (rules for every contributor) and [ARCHITECTURE.md](ARCHITECTURE.md).

## 1. Contracts

**Shared contract headers.** Every product depends on these, so they change only after a maintainer agrees in an issue:
- `modules/core/include/kraftonic/core/Parameters.h` and `Instrument.h`
- `modules/fx/include/kraftonic/fx/Effect.h`
- the public headers of any newer module (`modules/<m>/include/**`), once a second product uses them
- the root `CMakeLists.txt`

A product's own headers (`plugins/<p>/dsp/*.h`) can change with the product, but something already shipped (ids, keys, state format) never changes. The product's parameter table (`<P>Parameters.h`) is the contract between its engine, panel and presets: land a parameter change before the engine, panel or preset work that uses it.

- **Parameters** (`core/Parameters.h`): each product has a `ParamInfo<ParamId>` table that sets:
  - the numeric id (which is also the VST3 ParamID)
  - a stable `key`, name and unit
  - min, max and default
  - linear or log scale
  - `stepCount` (0 means continuous)

  `toPlain` and `toNormalized` convert host values. Engines always receive **plain** values. Once a product ships, its ids and keys are permanent.
- **Instruments** (`core/Instrument.h`):
  - `prepare (ProcessSetup)` is not real-time.
  - Each block: `setParameters` once, then `process (AudioBlock, events, n)`.
  - Events are SDK-free (`noteOn`, `noteOff`, `sustainPedal`, `allNotesOff`, `allSoundOff`), sample-accurate and sorted by offset.
  - Instrument engines share this shape.
- **Effects** (`fx/Effect.h`):
  - Interface: `prepare / reset / setParameters / process (in place, stereo) / latencySamples`, checked by the `StereoEffect` concept.
  - Effects never see product ParamIds. Products map their own parameters onto each effect's `Parameters` struct.

## 2. Threading

- **Audio thread:** `process`, `setParameters` and `reset`. No allocation, locks, waits, I/O, logging or exceptions; everything is sized in `prepare` / `setupProcessing` (not real-time).
- **Controller / UI thread:** parameter edits, state load and save, editor. Mutexes are allowed here only.
- **Between them:** lock-free SPSC queues (see `formats/UiNoteQueue.h`) or atomics.
- **Samples and files** load off the audio thread (`SampleBank::load` is not real-time; load banks in `prepare()`).
- Results don't depend on the host's block split, and every sample rate from 22.05 to 384 kHz works.

## 3. Rules

The code, real-time and content rules are in [AGENTS.md](../AGENTS.md); the pull-request process is in [CONTRIBUTING.md](../CONTRIBUTING.md).

## 4. Build commands (macOS dev machine)

```sh
# Fast DSP loop (no SDK download):
cmake -S . -B build/dsp -G Ninja -DKRAFTONIC_BUILD_PLUGINS=OFF -DKRAFTONIC_WARNINGS_AS_ERRORS=ON
cmake --build build/dsp && ctest --test-dir build/dsp --output-on-failure
ctest --test-dir build/dsp -R '^startersynth: '     # one product's tests only

# Full build with the VST3 SDK. Reuse the local SDK checkout:
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DKRAFTONIC_WARNINGS_AS_ERRORS=ON \
  -DFETCHCONTENT_SOURCE_DIR_VST3SDK=$HOME/.cache/kraftonic/sdk381/vst3sdk "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build/mac

# Validation:
build/mac/bin/Release/validator build/mac/VST3/Release/KraftonicStarterSynth.vst3
~/.cache/kraftonic/tools/pluginval/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 \
  --validate-in-process --vst3validator build/mac/bin/Release/validator build/mac/VST3/Release/KraftonicStarterSynth.vst3

# Merge check (before opening a pull request; see CONTRIBUTING.md):
scripts/check.sh startersynth         # native build + that product's tests + Steinberg validator
scripts/check.sh startersynth --full  # the same plus pluginval at strictness 10 (slow)
scripts/check.sh all                  # every product and module (automatic when core modules, cmake/, CI or packaging change)

# macOS, Windows and Linux: GitHub Actions runs on every pull request into main.
```
