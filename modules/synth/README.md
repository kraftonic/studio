<!-- SPDX-License-Identifier: MIT -->
# kraftonic::synth

SDK-free synthesizer DSP shared by Kraftonic instruments. The static target links only to `kraftonic::core`; its headers use the C++ standard library and contain no host, UI, or operating system dependencies.

## Classes

- `Oscillator`: sine, PolyBLEP saw and PWM pulse, integrated triangle, saw-triangle, hard phase sync input, phase modulation, per-instance slow pitch drift, and random start phase.
- `Noise`: deterministic white and stable multi-pole pink noise.
- `LadderFilter`: four-stage nonlinear low-pass with 1 ms time-based cutoff smoothing, immediate cutoff setting, resonance, drive and optional passband compensation.
- `StateVariableFilter`: TPT state-variable LP, HP, BP and notch outputs, 1 ms cutoff smoothing, immediate cutoff setting, and selectable 12/24 dB low-pass response.
- `OnePoleLowPass` and `OnePoleHighPass`: single-pole utility filters.
- `Lfo`: triangle, sine, rising/falling saw, square and sample-and-hold waves, Hz or beat tempo rate, delay, fade and key synchronization.
- `Glide`: constant-time or constant-rate interpolation.
- `NotePriority<N>`: fixed-capacity mono held-note stack, last/low/high priority, legato and configurable retrigger policy.

## Use

```cpp
#include "kraftonic/synth/Oscillator.h"
#include "kraftonic/synth/Filters.h"

kraftonic::synth::Oscillator oscillator;
oscillator.prepare (48000.0);
oscillator.reset ();
oscillator.setFrequency (440.0);
const float sample = oscillator.process (kraftonic::synth::Waveform::saw);
```

Call `prepare` when the sample rate changes. Processing and state reset allocate no memory. `Oscillator::process` takes a sync edge and an optional fractional position in the sample. When a master wraps, pass `sync=true` and `syncFraction=(1-masterPhase)/masterPhaseIncrement`; the slave applies a PolyBLEP correction at that sub-sample reset and resumes at the remaining fractional phase. Phase modulation is expressed in cycles. Each oscillator has a distinct default drift seed; `setSeed` makes a voice seed reproducible. Values are bounded to keep processing finite.

## Measurements

The alias test compares each oscillator waveform with its finite-bandwidth Fourier reference at 44.1 kHz. Saw reduction is 140.65 dB at 2.5 kHz and 139.62 dB at C7; pulse reduction is 138.83 dB and triangle reduction is 117.75 dB. These unusually large values reflect the high-pitch additive band-limited path and the numerical precision floor of the reference comparison. Hard sync applies a two-sided PolyBLEP to every edge in the sync interval (natural wraps, pulse edges and the reset jump at its fractional position) with no added latency, so a synced slave aliases like a free-running PolyBLEP oscillator. Off-harmonic energy at 48 kHz without oversampling, slave a fifth above the master, pulse width 0.25, to 20 kHz (and to 24 kHz): saw at an A5 master −35.9 dB (−31.6 dB), naive reset −16.2 dB (−15.3 dB); saw at E7 −30.9 dB (−28.2 dB), naive −11.3 dB (−10.8 dB); pulse at A5 −34.2 dB (−32.2 dB), naive −15.6 dB (−15.1 dB); pulse at E7 −36.3 dB (−28.9 dB), naive −11.6 dB (−10.8 dB). A free-running PolyBLEP saw at the slave pitch of the A5 case measures −36.2 dB (−31.0 dB), which is the floor of a two-point PolyBLEP. A total-error comparison of a 441 Hz master and 1,950 Hz slave against the ideal band-limited period gives 6.6 dB over naive; that metric also counts PolyBLEP's in-band roll-off (a free-running PolyBLEP saw scores 9.9 dB on it). Pulse duty and S&H timing are checked by their dedicated tests.

Cutoff 10–90% response time is 2.200 ms at 44.1 kHz and 2.198 ms at 192 kHz for both filters (target 2.197 ms); `setCutoffImmediate` jumps directly. Ladder slope checks cover 44.1, 96 and 192 kHz at 100 and 200 Hz cutoff; all tested octaves meet −24 dB/octave within ±1.5 dB. Ladder feedback is 4 × resonance up to 0.9, unchanged from earlier versions (bit-identical output). Above 0.9 it rises past the self-oscillation threshold (resonance ≈ 0.909) to 8 at 1.0, while the stages fade to linear, so the feedback saturation alone sets the oscillation. From silence, resonance 1.0 starts and sustains a 0.29–0.30 peak (about −10.7 dBFS) within 0.4% of cutoff at 65.4, 100, 500, 1,200, 4,000 and 8,000 Hz at 48 and 192 kHz. The level over 2.5–3.0 s stays within 3 dB of 0.5–1.0 s, and full-scale input at resonance 1.0 peaks below 0.41 at drive 1 and 16. At resonance 0, drive 1 and a 5 kHz cutoff, a 100 Hz sine passes 1% THD at −21 dBFS input at 48 kHz and −26 dBFS at 192 kHz (−11 and −13 dBFS at resonance 0.5). Passband gain compensation raises gain by the configured factor at resonance 0.8.

At 48 kHz and 1 kHz cutoff, SVF LP/HP/BP/notch responses are checked at 500/1,000/2,000 Hz, and its LP slope is measured in 12 and 24 dB modes. LFO tests count cycles and verify S&H steps. Random block partitions from 1 to 37 samples produce bit-identical output to a single 512-sample run for oscillator, ladder and LFO. Non-finite recovery and denormal flushing are exercised across the stateful classes. The real-time executable processes 512,000 samples through one oscillator/filter/LFO chain and prints nanoseconds per voice sample; timing depends on machine and build.
