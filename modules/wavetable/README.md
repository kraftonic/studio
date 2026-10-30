<!-- SPDX-License-Identifier: MIT -->
# kraftonic::wavetable

The wavetable synth engine. The static target links `kraftonic::core` and `kraftonic::fx` and uses no plug-in SDK, UI toolkit or OS header.

## Contract

- `kraftonic/wavetable/WaveEngine.h`: `kraftonic::wavetable::WaveEngine`, the `core` instrument-style engine.
- `kraftonic/wavetable/WaveParameters.h`: `kraftonic::wavetable::WaveParameters`, the parameter table and defaults.

Both are shared contracts: changing them needs a maintainer's agreement in an issue and an audible change can change downstream plug-ins. `src/**` is private.

## Tests

The engine's contract, wavetable, DSP and real-time tests live in `tests/` (prefix `wavetable: `). Plug-ins that play this engine keep their own sound and null tests.
