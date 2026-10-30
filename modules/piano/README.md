# modules/piano: Kraftonic piano layer

Headers `kraftonic/piano/`, namespace `kraftonic::piano`, target `kraftonic::piano`, test prefix `piano: `. It began as one product's piano engine and moved here for reuse. Like `modules/core`, it uses no plug-in SDK, UI toolkit or OS header. It links `kraftonic::core` publicly, and `kraftonic::sampler` and `kraftonic::fx` privately.

The public headers are a shared contract: changes need a maintainer's agreement in an issue.

| Header | What it is |
|---|---|
| `PianoSounds.h` | The sound catalogue: types, models, how each one is made. |
| `PianoLayer.h` | One playable piano: voices, sampled grands and the piano bus. |

Everything in `src/` is internal: `TineVoice.h` (Tine EP), `ModelVoice.h/.cpp` (the other models and the grand stand-ins) and `PianoLayer.cpp`.

## Sounds

A sound is a **type** plus a **model** within the type. `kModels` lists them all. Its flat index is permanent once shipped, because products store it in a parameter. Append new models; never reorder or reuse one.

| Index | Type | Model | Source | How it's made |
|---|---|---|---|---|
| 0 | electric | Tine EP | modelled | Two-operator FM body plus an inharmonic tine partial |
| 1 | electric | Reed EP | modelled | Struck steel reed into a non-linear pickup: hard notes bark |
| 2 | electric | Electric Grand | modelled | Short struck strings, two per key, read by a bright under-bridge pickup |
| 3 | clav | Clav | modelled | Waveguide string struck from below, two pickups and a yarn damper |
| 4 | grand | Bright Grand | **sampled** | The `grand` recordings: bright, punchy |
| 5 | grand | Studio Grand | **sampled** | The same recordings: warmer tilt, wider image |
| 6 | electric | Bright Tine | modelled | The Tine voice with a hard hammer and an asymmetric pickup close to the tine: barks with velocity |
| 7 | electric | Hard Reed | modelled | The reed with a strongly asymmetric pickup (a larger even-order term) |
| 8 | electric | Glass Reed | modelled | A plucked reed: a felt tick, an even swing, a shorter decay |
| 9 | clav | Bright Clav | modelled | The clav string read by two pickups out of phase: a comb notch, a thin, bright spectrum |
| 10 | electric | Celesta | modelled | Struck steel bars: exact fundamental, inharmonic modes at 2.76, 5.40 and 8.93 times it |
| 11 | clav | Harpsichord | **sampled** | The `harpsichord` recordings, with release thumps; stand-in: the clav string plucked and read near the bridge |
| 12 | electric | Vibes | **sampled** | The `vibes` recordings (soft mallets); stand-in: tuned metal bars (partials 1, 4, ~10), long ring |
| 13 | electric | Marimba | **sampled** | The `marimba` recordings; stand-in: tuned wooden bars, short ring |

A committed null test (`piano: Piano sounds: original models render as they did before the appended sounds`) holds the original models to their earlier output.

- **Sampled:** the grand types, and Harpsichord, Vibes and Marimba. When their data is missing, they play a modelled stand-in 9 dB quieter, and `samplesMissing()` reports it. The grands read the `grand` folder (`setGrandBank`); the others read their own folder, `PianoLayer::modelFolder (index)`, loaded with `loadModelBank` and handed over with `setModelBank`.
- **Modelled:** the other electric and clav models (Celesta, Vibes and Marimba are listed as electric: the catalogue has no mallet type).
- **No type yet:** upright, harpsichord and digital (the Harpsichord is listed as a clav model). A product that offers them adds models here, appended to `kModels`.

`modelCount (type)`, `modelIndex (type, model)` and `modelWithinType (index)` convert between the two forms.

## Using it

```cpp
#include <kraftonic/piano/PianoLayer.h>
#include <kraftonic/sampler/SampleLocations.h>

using namespace kraftonic::piano;

PianoLayer layer;                                    // may allocate

// NOT real time: load the recordings once, then prepare.
layer.setGrandBank (PianoLayer::loadGrandBank (
    kraftonic::sampler::sampleDirectory ("My Vendor", "My Piano", "MYPIANO_SAMPLES")));
Parameters p;
p.type = Type::grand;
p.model = 1;                                         // Studio Grand
p.stringResonance = 0.4f;
layer.setParameters (p);
layer.prepare (sampleRate, maxBlockSize);

// Real time. Split process() at each event for sample-accurate timing.
layer.setParameters (p);                             // once per block is enough
layer.noteOn (60, 0.8f);                             // velocity 0..1, before the touch curve
layer.sustain (cc64 / 127.0f);                       // dampers lift at 0.5 and above
layer.process (left, right, numSamples);             // replaces left/right
layer.noteOff (60);
```

- **Lifecycle:** `setGrandBank`, `loadGrandBank` and `prepare` are not real-time. Everything else is: no allocation, locks, I/O or exceptions. `reset()` silences at once, and `allNotesOff()` releases every key and the pedal.
- **Block-size independent:** the output for an event and parameter stream is the same however it is split into `process()` calls, and calls longer than `maxBlockSize` are fine.
- **Split processing:** `process()` is `renderVoices()` (the voices, replacing the buffers) followed by `processBus()` (the piano bus, in place). A product can put its own effect between the two; a tremolo, for example.
- **Octave:** `octave` shifts notes at note-on. `noteOff (key)` always releases the note that key started.

### Parameters

| Field | Range | What it does |
|---|---|---|
| `type`, `model` | see above | The sound for new notes. Sounding notes keep the sound they started with. |
| `tone` | 0..1 | Dark to bright. Smoothed and live on sounding notes. |
| `timbre` | soft, mid, bright | Moves the tone by -0.25, 0 or +0.25. |
| `strike` | 0..1 | Strike noise, the Tine's bell, and the grands' hammer and damper noise. Smoothed. |
| `velocitySensitivity` | 0..1 | How much velocity changes level and brightness. Smoothed. |
| `touch` | light, normal, heavy, fixed | The velocity curve. Fixed plays every note at velocity 100. |
| `attack`, `decay`, `sustain`, `release` | 1 ms..0.5 s, 0.2..20 s, 0..1, 10 ms..5 s | The modelled sounds' envelope (smoothed). The grands use the release time only. |
| `softRelease` | on/off | Every release about a third as long. |
| `dynamicCompression` | on/off | Gentle 3:1 peak compression above -18 dBFS. |
| `doubling` | on/off | Detuned doubling: a short delay wobbled by a slow LFO. |
| `stringResonance` | 0..1 | Sympathetic resonance. It opens fully while the pedal is down or a key is held, and rests a quarter open otherwise. |
| `pedalNoise` | 0..1 | A mechanical thump on both pedal edges. |
| `level` | 0..2 | Linear output gain, smoothed over 20 ms. |
| `octave` | -3..+3 | Octave shift, applied at note-on. |

Out-of-range and non-finite values are clamped or replaced by the defaults.

### Levels

The layer's output is an internal piano bus. It runs about 6 dB hotter than normal playing should leave a product, and it has no peak limiter of its own.
- A product adds its own level, for example a -6 dB default output gain and a soft knee above -1 dBFS (`core::StereoPeakProtector`).
- A new product should do the same: for example `level` 0.5 and its own protector at the end of its chain.
- At level 0.5, a velocity-100 triad stays under -3 dBFS on every modelled sound. A ten-note fortissimo chord on Reed EP, Electric Grand or Clav does too.
- Extreme settings can still pass 0 dBFS by a few dB, for example Clav at velocity sensitivity 0, which plays every note at full strike. The protector catches that.

## Tests

```sh
cmake --build <build> --target kraftonic_piano_tests kraftonic_piano_realtime
ctest --test-dir <build> --output-on-failure -R '^piano: '
```

- `PianoLayerTests.cpp`: the contract, the sounds and the bus (ported to this API from the original product's tests).
- `PianoRealtime.cpp`: `piano: realtime and performance`. It checks for zero allocations in every real-time call at 22.05 to 384 kHz, and gates CPU at 3x real time.
- The sampled path uses a synthetic `grand` folder, so no recordings are needed.

**Downstream sound.** Plug-ins built on this layer may check that their output stays bit-identical, so a change here that alters sound needs a maintainer's agreement in an issue first.
