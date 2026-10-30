---
name: audio-engineering
description: Use for any Kraftonic change to DSP, the audio thread, parameters, presets, effects, samples or anything audible, to keep sound quality, real-time safety and compatibility.
---

# Audio engineering

## When to use
Changes to `modules/core|fx|sampler`, `plugins/*/dsp`, the processor side of `modules/formats` or `plugins/*/plugin`, parameter tables, presets or samples.

## Steps
1. **Contract first:**
   - Engines get plain parameter values once per block (`setParameters`), then `process (block, events, n)`. Events are SDK-free, sorted, and sample-accurate (`core/Instrument.h`).
   - Effects satisfy `fx::StereoEffect` and never see product ParamIds (`fx/Effect.h`).
2. **Real-time safety:**
   - Allocate and size everything in `prepare()`, for the maximum block size and the largest setting.
   - No `new`/`malloc`, container growth, `std::string`, locks, waits, I/O, logging or exceptions on the audio path. Mark real-time functions `noexcept`.
   - Data between threads goes through lock-free SPSC queues or atomics only (see `formats/UiNoteQueue.h`).
   - Loading samples or files never happens on the audio thread. `SampleBank::load` is not real-time; load banks in `prepare()`.
3. **Sound quality:**
   - Smooth every parameter that can jump audibly (`core/Smoother.h`).
   - Crossfade on/off switches; a disabled effect is bit-exact pass-through once its crossfade ends.
   - Control aliasing: limit FM index or bandwidth, oversample nonlinear stages where needed.
   - Denormals and NaN: output is always finite, including after extreme settings and `reset()`.
   - Level: normal playing stays under 0 dBFS. The peak protector is a safety net, not a mixer.
4. **Host robustness:**
   - Results must be identical for any block split (render once with 1 large block, once with random small blocks, and compare).
   - Test sample rates from 22.05 to 384 kHz; pitch and time constants scale with the rate.
   - Voice stealing is click-free; `allNotesOff` and `allSoundOff` behave as documented.
5. **Compatibility:** parameter IDs and keys are permanent, and new parameters get new IDs. Any range or default change needs a state-compat test.
6. **Measure, then listen:**
   - Add a test that measures the behaviour: level, spectrum or DFT bins, decay time, click energy at switch points, CPU time in the realtime test.
   - Render WAVs before and after for the reviewer.
7. **Content:** original sounds and names only; samples per AGENTS.md "Content, licensing and brand".

## Evidence
- Realtime test output for the touched product or module.
- The new measuring test passing.
- The block-split and sample-rate checks.
- WAV paths with before/after peak and RMS.
- A CPU number when performance matters.

## Stop and escalate
- Meeting the requirement needs allocation or locking on the audio thread, or added latency: raise it with a maintainer in an issue.
- A change alters the sound of shipped presets or programs: get a maintainer's sign-off with renders.
- A sample source's licence is unclear: reject it (see the rejected sources in THIRD_PARTY_NOTICES.md).
