<!-- SPDX-License-Identifier: MIT -->
# Kraftonic Starter Synth

A small eight-voice polyphonic synth, and a template for your own instrument.
Two oscillators (saw, square, sine) with detune and a mix, a resonant 12 dB
low-pass filter, one ADSR envelope that shapes both the level and the
cutoff, glide, an output level and five factory presets.

![Kraftonic Starter Synth panel](screenshot.png)

## Make your own from it

```sh
scripts/new-product.sh "My Synth" --from synth --vendor "Your Name"
```

This copies this folder to `plugins/mysynth`, renames everything, gives the
copy new permanent plug-in IDs (VST3 class IDs, Audio Unit codes and the
bundle id, which is also the CLAP id) and
adds it to the build. Then read on.

## Install

After a build, copy `build/VST3/Release/KraftonicStarterSynth.vst3` to your VST3
folder. On macOS, also copy `KraftonicStarterSynth.component` from the same folder
to `~/Library/Audio/Plug-Ins/Components` for Logic Pro and GarageBand (Audio
Unit `aumu Stsy Kftc`). `KraftonicStarterSynth.clap`, also in that folder, is the CLAP plug-in
(id `com.kraftonic.startersynth`): copy it to your CLAP folder (`~/Library/Audio/Plug-Ins/CLAP`,
`C:\Program Files\Common Files\CLAP` or `~/.clap`).

## How it is put together

Data flows in one direction: **parameter table → engine → plug-in → panel**.

| File | What it does | Talks to the host? |
|---|---|---|
| `dsp/StarterSynthParameters.h` | The parameter table: id, key, name, unit, range, default, scale. The contract everything else reads. | no |
| `dsp/StarterSynthEngine.h`, `dsp/StarterSynthEngine.cpp` | The sound: voices, oscillators, filter, envelope. Gets plain values and SDK-free note events. | no |
| `plugin/StarterSynthPresets.h` | Factory presets: plain values by parameter key. | no |
| `plugin/StarterSynthTraits.h` | Binds the table, engine and presets to the shared VST3 base, plus names and permanent IDs. | no (SDK-free) |
| `plugin/StarterSynthValueText.h` | Parameter values as text ("2.00 kHz", "300.0 ms") and back. | no |
| `plugin/StarterSynthEntry.cpp` | The VST3 factory (four declarations) and the CLAP entry (one line). The shared base does the rest. | yes |
| `ui/make_startersynth_uidesc.py` | The panel layout (sections and controls). Generates `startersynth.uidesc` and `startersynth.panel.json`. | — |
| `tests/` | Measured DSP tests, a realtime test, plug-in tests and a WAV renderer. | no |

What the shared kit does for you:

- **`modules/synth`**: band-limited oscillators (`Oscillator`) and filters (`StateVariableFilter`, `LadderFilter`).
- **`modules/core`**: the envelope (`Adsr`), parameter smoothing (`LinearSmoother`), voice allocation (`VoiceAllocator`), a peak guard (`StereoPeakProtector`), and the parameter and event contracts.
- **`modules/formats`**: the complete VST3 processor and controller from the traits: MIDI to events, sample-accurate automation, state, presets, bypass and the editor ([modules/formats/README.md](../../modules/formats/README.md)).
- **`modules/ui`**: the panel kit and generator ([modules/ui/README.md](../../modules/ui/README.md)).

### The engine in one paragraph

`process()` splits each block at its note events, so every note starts on
its exact sample. Per sample, the six smoothed controls advance one step
(20 ms ramps, so knob moves never click), then each sounding voice glides its
pitch, runs both oscillators, mixes them, steps its envelope, sets the filter
cutoff to `cutoff × 2^(envAmount × envelope)` and adds its filtered,
enveloped output to the sum. A voice whose release has finished hands its
slot back to the allocator. The sum is scaled by the output level and passes
a soft peak guard.

## What to change first

1. **A parameter.** Add a row to the table in `StarterSynthParameters.h`
   (append the enum value before `count`), read it in
   `StarterSynthEngine::setParameters`, add a control in
   `ui/make_startersynth_uidesc.py` and run the script. The panel test fails
   until every parameter is on the panel exactly once.
2. **The sound.** A third oscillator, a noise source or a second envelope
   for the filter all live in `Voice` and `render()`. `modules/synth` has
   `Noise`, `Lfo`, `Glide` and a four-pole `LadderFilter` ready to use.
3. **The presets.** Edit `plugin/StarterSynthPresets.h`. Keys must match
   the table (a test checks it).
4. **The names.** `kPluginName` and `kVendor` in the traits, the panel
   `title` in the generator. Your plug-in may not be called "Kraftonic"
   ([TRADEMARKS.md](../../TRADEMARKS.md)); `new-product.sh` handles this.

Rules that keep it a good plug-in (from [AGENTS.md](../../AGENTS.md)):

- Never allocate, lock, log or throw in `process`, `setParameters` or
  `reset`. Size everything in `prepare`. The realtime test catches
  allocations.
- Smooth anything that can jump audibly. Keep normal playing under 0 dBFS.
- Once a version ships, parameter ids and keys, the class IDs and the state
  magic are permanent: saved songs use them.

## Build, test, listen

```sh
cmake -S . -B build/check -G Ninja -DCMAKE_BUILD_TYPE=Release -DKRAFTONIC_WARNINGS_AS_ERRORS=ON
cmake --build build/check --target KraftonicStarterSynth startersynth_tests \
    startersynth_realtime startersynth_plugin_tests startersynth_render
ctest --test-dir build/check --output-on-failure -R '^(startersynth: |ui: startersynth panel$)'
# Listen: every preset plays a chord and a melody; prints peak and RMS.
build/check/bin/Release/startersynth_render build/listening/startersynth.wav
# Panel: regenerate after changing the table or layout; snapshot it (macOS).
python3 plugins/startersynth/ui/make_startersynth_uidesc.py
build/check/bin/Release/kraftonic_editor_snapshot build/check/VST3/Release/KraftonicStarterSynth.vst3 \
    plugins/startersynth/screenshot.png
# Before a pull request:
scripts/check.sh startersynth
```

For quick DSP work without the VST3 SDK, configure `build/dsp` with
`-DKRAFTONIC_BUILD_PLUGINS=OFF` and build the three test targets.
