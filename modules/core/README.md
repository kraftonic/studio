# modules/core: the foundations

Namespace `kraftonic::core`, target `kraftonic::core`. Plain C++20 with no dependencies: no plug-in SDK, UI toolkit or OS header. Every other module and every product builds on it.

| Header | What it is |
|---|---|
| `Parameters.h` | **Contract.** `ParamInfo<Id>`: one row per parameter (id, stable key, name, unit, range, default, linear/log, steps). `toPlain` / `toNormalized` convert host values; engines always get plain values. |
| `Instrument.h` | **Contract.** `ProcessSetup` (sample rate, max block size), `Event` (note on/off, sustain pedal, all notes/sound off, mod wheel, pitch bend, each with a sample offset) and `AudioBlock` (two channels the engine writes). |
| `Adsr.h` | Analog-style ADSR envelope with exponential segments and click-free minimum times. |
| `LowPassFilter.h` | Two-pole TPT state-variable low-pass with per-sample coefficients. |
| `Smoother.h` | `LinearSmoother`: ramps a parameter to each new target in a fixed time, so jumps don't click. |
| `VoiceAllocator.h` | `VoiceAllocator<N>`: which voice slot plays each note, with stealing. Knows nothing about sound. |
| `StereoPeakProtector.h` | Stereo-linked soft knee that keeps rare full-scale peaks under 0 dBFS. |
| `Math.h` | `midiNoteToHz`, `decibelsToGain`, `wrapPhase`, π. |

The two contract headers are shared by every product, so they change only after a maintainer agrees in an issue ([docs/SPEC.md](../../docs/SPEC.md#1-contracts)).

## Using it

```cpp
#include <kraftonic/core/Adsr.h>
#include <kraftonic/core/Math.h>

kraftonic::core::Adsr env;
env.prepare (sampleRate);                       // not real time
env.setParameters ({ 0.01f, 0.2f, 0.7f, 0.5f }); // attack s, decay s, sustain, release s
env.noteOn();
float level = env.process();                     // per sample, real-time safe
double hz = kraftonic::core::midiNoteToHz (69);  // 440
```

Everything except `prepare` is real-time safe: no allocation, locks or I/O.

## Tests

```sh
ctest --test-dir build -R '^core: '
```
