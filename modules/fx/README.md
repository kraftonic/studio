# modules/fx: stereo effects

Namespace `kraftonic::fx`, target `kraftonic::fx`. Plain C++20 on top of `kraftonic::core`: no plug-in SDK, UI toolkit or OS header.

Every effect has the same shape, defined in `Effect.h` and checked by the `StereoEffect` concept:

```cpp
struct Parameters { ... };                        // plain values: Hz, ms, dB, 0..1
void prepare (const core::ProcessSetup&);         // NOT real time; may allocate
void reset() noexcept;                            // clear tails and state
void setParameters (const Parameters&) noexcept;  // once per block; smoothed inside
void process (float* left, float* right, int n) noexcept;   // in place, stereo
int latencySamples() const noexcept;
```

Effects never see a product's parameter IDs: the product maps its own parameters onto each effect's `Parameters`.

| Effect | Parameters |
|---|---|
| `Drive` | drive, tone, output dB, mix |
| `EQ` | low shelf (Hz, dB), mid peak (Hz, Q, dB), high shelf (Hz, dB), output dB |
| `Chorus` | taps, rate Hz, depth, mix |
| `Phaser` | stages, rate Hz, depth, feedback, stereo phase, mix |
| `Delay` | time ms or tempo-synced note division, feedback, low and high cut, ping-pong, mix |
| `Reverb` | pre-delay ms, size, decay s, damping, width, mix |

Each has `enabled`; turning it off crossfades instead of clicking. All are zero-latency.

## Using it

```cpp
#include <kraftonic/fx/Delay.h>

kraftonic::fx::Delay delay;
delay.prepare ({ sampleRate, maxBlockSize });   // not real time

kraftonic::fx::Delay::Parameters p;
p.timeMs = 375.0f;
p.feedback = 0.4f;
p.mix = 0.3f;
delay.setParameters (p);                         // real time, once per block
delay.process (left, right, numSamples);         // in place
```

`plugins/startereffect` is a complete plug-in built from `Drive` and `Delay`.

## Tests

```sh
ctest --test-dir build -R '^fx: '
```
