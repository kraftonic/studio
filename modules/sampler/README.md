# modules/sampler: Kraftonic sampler engine

Namespace `kraftonic::sampler`, target `kraftonic::sampler`. The module is framework-free like `modules/core`: no plug-in SDK, UI toolkit or OS header. It depends only on `kraftonic::core`.

| Header | What it is |
|---|---|
| `SampleBank.h` | One sampled instrument in memory: samples, zones and instrument settings. Loaded **not** in real time from a folder, then shared read-only. |
| `Sampler.h` | The playback engine. It has the usual instrument shape: `prepare`, then `setParameters` and `process (block, events, n)`. |
| `SampleLocations.h` | Where a product's samples are installed (`<Vendor>/<Product>/Samples` per platform), with an environment override. |
| `WavFile.h` | Bounds-checked WAV reader and writer, used by the loader, tools and tests. |

## Using it

```cpp
#include "kraftonic/sampler/SampleBank.h"
#include "kraftonic/sampler/SampleLocations.h"
#include "kraftonic/sampler/Sampler.h"

// NOT real time (the engine's prepare): load each instrument once, share it.
kraftonic::sampler::LoadReport report;
auto grand = kraftonic::sampler::SampleBank::load (
    kraftonic::sampler::instrumentDirectory (sampleRoot, "grand"), report);  // nullptr if missing
// report.error / report.warnings explain what was skipped.

kraftonic::sampler::Sampler brightGrand, studioGrand;  // two sounds, one set of recordings
brightGrand.prepare (setup);
brightGrand.setBank (grand);
studioGrand.prepare (setup);
studioGrand.setBank (grand);

// Real time, every block:
kraftonic::sampler::Sampler::Parameters p;
p.stereoWidth = 1.3f;        // "Studio Grand": wider
p.velocitySensitivity = 0.8f;
studioGrand.setParameters (p);
studioGrand.process (block, events, numEvents);   // overwrites block.left/right
```

- `sampleRoot` is the product's choice, usually `kraftonic::sampler::sampleDirectory ("My Vendor", "My Piano", "MYPIANO_SAMPLES")`.
- If a folder is missing, `load` returns `nullptr`; the product decides what plays instead.
- `setBank` and `prepare` allocate, so never call them on the audio thread or while `process` may run.
- `process` handles `noteOn`, `noteOff`, `sustainPedal`, `allNotesOff`, `allSoundOff` and `pitchBend`, and ignores `modWheel`.
- `Parameters`: `gainDb`, `tuneCents`, `pitchBendSemitones`, `releaseScale`, `velocitySensitivity`, `pedalResonance` (a value below 0 uses the manifest's), `releaseNoiseDb`, `stereoWidth` and `polyphony` (0 uses the manifest's). Gain and width are smoothed over 20 ms.

## Instrument folder format (version 1)

```
<root>/<instrument>/instrument.ini
<root>/<instrument>/samples/*.wav       16-bit (or 24-bit / float) PCM, mono or stereo, any rate
```

`instrument.ini` is a small INI file. `#` or `;` starts a comment when it begins a line or follows a space, so file names such as `F#3.wav` are fine. Velocities are MIDI 1..127. Keys are MIDI note numbers, where 60 = C4.

```ini
[instrument]
format = 1                    ; required
name = Grand Piano
gain_db = -6                  ; instrument level
velocity_range_db = 14        ; level difference between velocity 127 and 1
velocity_crossfade = 12       ; width (MIDI velocity units) of crossfades between layers, 0 = switch
velocity_crossfade_curve = linear ; linear (correlated layers) or power (uncorrelated, e.g. ensembles)
attack_ms = 0.5
release_ms = 260              ; damper / release time (reaches -60 dB)
retrigger_release_ms = 350    ; a re-struck note fades its previous voice in this time
pedal_resonance = 0.3         ; 0..1 sympathetic string resonance while the pedal is down
release_trigger_gain_db = 0   ; extra level for all release-trigger zones
polyphony = 128               ; voices (one per playing zone)

[zone]
sample = samples/grand_C4_v10.wav   ; relative to the folder; no "..", no absolute paths
root = 60
key = 59-61                   ; or a single key: key = 60
velocity = 57-82
tune_cents = 0
gain_db = 0
trigger = attack              ; attack | release | pedal_down | pedal_up
key_tracking = 1              ; 0 = fixed pitch (noises)
round_robin = 0               ; zones with the same non-zero id alternate
release_ms = 260              ; overrides the instrument's
damped = true                 ; false: note-off doesn't stop it (top piano strings)
release_decay_db_per_s = 0    ; release triggers get quieter the longer the key was held
loop = 41000-162000           ; optional loop, frames [start, end); also read from a WAV smpl chunk
```

When a zone's file is missing or corrupt, or a value is invalid, that zone or key is skipped and `LoadReport::warnings` says why. Loading fails, returning `nullptr`, only when the folder or manifest is missing, `format` is not 1, or no attack zone can be played. Nothing in the loader throws or crashes on bad input. The tests feed it fuzzed WAV headers.

## Playback details

- **Interpolation.** A 24-tap Kaiser-windowed sinc (β = 9) with 512 fractional phases, linearly interpolated. Seven kernels are chosen by pitch ratio, up to 4×. Above a ratio of 1 the cutoff drops, so upward transposition and low host rates do not alias. Measured: a transposed sine has residual error at the 16-bit noise floor (about −92 dB). Content pushed above the output Nyquist is about −62 dB. Up to 16 kHz (44.1 kHz source at 48 kHz) the response is flat within 1 dB.
- **Velocity layers.** Crossfades are linear by default: weights sum to 1, so loudness is monotonic in velocity for correlated layers (tested). `power` keeps the level of uncorrelated layers (ensembles) constant through the crossfade. The grand switches layers without a crossfade: two recordings of the same piano note are neither correlated nor uncorrelated, and blending them sounds phasey. Its layers are level-matched by the pipeline instead.
- **Damping, pedal and release triggers.** Note-off with the pedal up means the damper falls: voices release and `release` zones fire, using the note-on velocity and `release_decay_db_per_s × seconds held`. With the pedal down, notes are held until it lifts. `pedal_down` and `pedal_up` zones play the pedal's own noise.
- **Sympathetic resonance.** While the pedal is down, 12 tuned feedback combs per channel ring with the dry signal. They are a fourth apart, so every pitch class is covered. Lifting the pedal shortens their decay from about 3.3 s to 0.12 s, like dampers landing.
- **Voices.** There are 192 slots. Polyphony is clamped to 176, keeping 16 slots so a stolen voice can fade over 3 ms instead of being cut. The engine steals the quietest releasing voice first, then the oldest held one. A re-struck key fades its previous voice over `retrigger_release_ms`.
- **Timing.** Events apply at their exact `sampleOffset`. Pitch bend, kernel choice and resonance feedback update every 32 samples of absolute time, so the output is the same for any block size (tested to 1e-6).
- **Real-time safety.** `process` performs no allocation, locks, I/O or exceptions, and its output is always finite. `sampler: realtime and performance` counts allocations and enforces a 3× CPU gate.

## Memory budget and the streaming path

v1 preloads every sample as 16-bit planar PCM: 2 bytes per sample per channel, plus 16 guard frames at each end. The original grand, strings and brass set is trimmed to fit **under ~500 MB** of RAM:

| Instrument | Trade-off taken |
|---|---|
| grand | 5 of 16 velocity layers. Notes capped at 18 s (bass) / 14 s / 10 s / 7 s (treble) with a 1.5 s fade, which cuts very long pedalled bass tails short. Two resonance-release layers (of 3), hammer and damper noise, pedal noise. |
| strings, brass | 2 dynamic layers per note, looped sustains. Audio after the loop end is dropped. |



**Disk streaming later.** No caller changes are needed. `LoadOptions::preloadFrames` is reserved for it: a `Sample` would hold only its first N frames in RAM and a background reader thread would fill per-voice ring buffers. `Sampler::process` stays real-time safe by reading those buffers. `SampleBank::load`, `Sampler::setBank` and `Sampler::process` keep their signatures.

## Tests

```sh
ctest --test-dir build/dsp -R '^sampler: '
build/dsp/modules/sampler/tests/kraftonic_sampler_realtime --bench <sample-root>                 # real banks: CPU and RAM
build/dsp/modules/sampler/tests/kraftonic_sampler_realtime --render build/listening/sampler <sample-root>
```
