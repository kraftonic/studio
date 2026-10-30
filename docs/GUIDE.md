# A beginner's guide to DSP, sound design and plug-ins

This guide is for you if you can write some C++ (or are learning) and have just forked Kraftonic Studio, but digital audio, synthesis or plug-in development is new to you. By the end you should understand what happens inside the two starters, be able to make your first real sound change with confidence, and know what to learn next.

Every section stands on its own, so skip what you already know. Every file, class and command named here exists in the repository; if the code and this guide ever disagree, the code wins, so please fix the guide.

## Contents

1. [How a plug-in works](#1-how-a-plug-in-works)
2. [Digital audio in ten minutes](#2-digital-audio-in-ten-minutes)
3. [Building blocks of a synth](#3-building-blocks-of-a-synth)
4. [Building blocks of effects](#4-building-blocks-of-effects)
5. [A sound design primer](#5-a-sound-design-primer)
6. [Your first changes, step by step](#6-your-first-changes-step-by-step)
7. [The real-time rules, explained](#7-the-real-time-rules-explained)
8. [Testing, listening and looking](#8-testing-listening-and-looking)
9. [Common problems and how to find them](#9-common-problems-and-how-to-find-them)
10. [Glossary](#10-glossary)
11. [Where to learn more](#11-where-to-learn-more)

## 1. How a plug-in works

### The host and the audio thread

A plug-in is a library that a **host** (a DAW, a live-performance app, a test tool) loads and calls. It never runs on its own. The host owns the audio device, the timeline and the mixer; your plug-in is one box in its signal chain.

Sound is processed in **blocks**. The audio device asks the host for, say, 256 samples every few milliseconds, and the host asks each plug-in in turn to fill or transform its share. That call happens on the **audio thread**, a high-priority thread with a hard deadline: if your code is late, the listener hears a click or a dropout. Section 7 explains what that means for your code.

Two numbers describe the stream:

- **Sample rate:** samples per second, per channel. 44.1 kHz and 48 kHz are common; studios also use 88.2, 96 and 192 kHz. Studio supports any rate from 22.05 to 384 kHz.
- **Buffer size** (block size): how many samples each call handles. Small buffers mean low latency but more calls per second. The host can change it, and it may send blocks shorter than the maximum it promised.

### Processor, controller and editor

A plug-in has three parts that the host talks to separately:

| Part | What it does | Thread |
|---|---|---|
| Processor | Turns notes and input audio into output audio | Audio thread |
| Controller | Describes the parameters to the host, formats their values as text, holds the preset list | Main (UI) thread |
| Editor | The panel the user sees: knobs, switches, menus | Main (UI) thread |

The processor and the controller never share memory directly. The host carries parameter changes between them, and Studio's format layer handles the rest.

### Parameters, automation, state and presets

A **parameter** is a value the host can see, save and automate: cutoff, attack, mix. Hosts deal in **normalized** values from 0 to 1; your engine wants **plain** values in real units (Hz, seconds, dB). Each parameter's range, default and scale (linear or logarithmic) define the mapping. In Studio this is `kraftonic::core::ParamInfo` with `toPlain` and `toNormalized` in `modules/core/include/kraftonic/core/Parameters.h`.

**Automation** is the host changing a parameter over time from a recorded curve. Changes can arrive at any sample within a block, so the engine has to cope with values that move between blocks.

**State** is everything the host saves in a project so the song sounds the same next time: in Studio, one plain value per parameter, written by `modules/formats/include/kraftonic/formats/StateCodec.h`. **Presets** are named sets of values that ship with the plug-in.

### Why IDs are permanent

Saved songs refer to your plug-in and its parameters by number, not by name:

- the plug-in's **class IDs** (`kProcessorFuid`, `kControllerFuid` in the traits), its **bundle identifier** (also its CLAP id) and its **Audio Unit codes** tell the host which plug-in to load;
- each **parameter id** (the `ParamId` enum value) tells the host which knob an automation lane drives;
- each parameter's **key** (a string such as `"cutoff"`) is how presets name it;
- the **state magic** (`kStateMagic`) marks your project data as yours.

Change any of these after you ship and old songs open the wrong plug-in, lose their automation or fall back to defaults. Add new ones; never rename or reuse old ones.

### VST3, CLAP and AU

These are three plug-in formats: three different contracts between host and plug-in for the same ideas.

- **VST3** is Steinberg's format, supported by most hosts on Windows, macOS and Linux.
- **CLAP** is an open, MIT-licensed format with a small C API, supported by a growing number of hosts.
- **AU** (Audio Unit) is Apple's format on macOS, and the only one some Apple hosts load.

You write your plug-in once. Studio builds a `.vst3` and a `.clap` of it everywhere and, on macOS, a `.component` (an AU that wraps the VST3).

### Where each idea lives in Studio

Every plug-in folder has the same layout. Using the Starter Synth as the example:

| Idea | File |
|---|---|
| Parameter table: ids, keys, ranges, defaults | `plugins/startersynth/dsp/StarterSynthParameters.h` |
| The sound engine (runs in the processor) | `plugins/startersynth/dsp/StarterSynthEngine.h`, `.cpp` |
| Factory presets | `plugins/startersynth/plugin/StarterSynthPresets.h` |
| Values as text ("2.00 kHz") and back | `plugins/startersynth/plugin/StarterSynthValueText.h` |
| Identity, names, permanent IDs, tail length | `plugins/startersynth/plugin/StarterSynthTraits.h` |
| VST3 factory and CLAP entry | `plugins/startersynth/plugin/StarterSynthEntry.cpp` |
| Panel layout, generated into `.uidesc` | `plugins/startersynth/ui/make_startersynth_uidesc.py` |
| Tests and the WAV renderer | `plugins/startersynth/tests/` |

The engine in `dsp/` knows nothing about any plug-in format. It receives plain values and SDK-free events (`core::Event`, `core::AudioBlock` from `modules/core/include/kraftonic/core/Instrument.h`), so tests drive it directly with no host. The traits struct in `plugin/` describes the product, and the templates in `modules/formats` (`vst3::Processor`, `vst3::Controller`, `vst3::effect::EffectProcessor`, `clap::Plugin`) turn it into a working plug-in: MIDI to events, automation, state, presets, bypass and the editor. You rarely need to touch `modules/formats` at all.

## 2. Digital audio in ten minutes

### Samples and sample rate

Sound is air pressure changing over time. A microphone turns it into a voltage, and a converter measures that voltage many times a second. Each measurement is a **sample**; in Studio a sample is a `float`, where 0 is silence and -1 to +1 is the full range of the output.

### Nyquist and aliasing

A sample rate can only describe frequencies up to half of itself, the **Nyquist frequency**: 24 kHz at a 48 kHz rate. That is enough for human hearing (about 20 Hz to 20 kHz), but only if nothing in your signal goes above it.

If your code *generates* content above Nyquist, it doesn't vanish. It folds back down and reappears as a different, unrelated frequency. This is **aliasing**. A 30 kHz tone at a 48 kHz rate comes out as 18 kHz. Musically, aliasing sounds like harsh, metallic, inharmonic tones that move the wrong way when you play a melody (up when the note goes down). Oscillators with sharp edges and distortion are the usual sources; sections 3 and 4 show how Studio avoids them.

### Amplitude, decibels and dBFS

Our ears hear loudness roughly logarithmically, so audio levels are measured in **decibels**. For a gain (a multiplier on the signal):

    dB = 20 × log10(gain)

So doubling the amplitude is about +6 dB, halving is -6 dB, and a tenth is -20 dB. **dBFS** (decibels relative to full scale) measures a level against the largest value the output can hold: 0 dBFS is a sample of ±1.0, and everything below is negative. The starters convert with `std::pow (10.0F, db / 20.0F)`; `kraftonic::core::decibelsToGain` in `core/Math.h` does the same.

### Headroom and clipping

Anything beyond ±1.0 at the output is **clipped**: flattened, because the converter can't go further. Clipping sounds like crackle or harsh distortion on peaks. **Headroom** is the space you leave below 0 dBFS so peaks never reach it. Studio's rule is that normal playing stays under 0 dBFS. The Starter Synth scales each voice by `kVoiceGain` (0.25) so a full chord stays below full scale, and passes the sum through `core::StereoPeakProtector`, a soft knee that only touches the rare peaks above -1 dBFS.

### Mono and stereo

Stereo is two channels, left and right. In Studio, `AudioBlock` always has two non-interleaved channels. The Starter Synth writes the same signal to both (mono content); the effect processes true stereo, and its ping-pong delay sends echoes back and forth between the sides. Effects must also work when `right == left`, which is how the format layer passes a mono input.

### Latency

**Latency** is the delay between input and output. Some of it is the buffer itself; some is added by plug-ins that look ahead (a limiter) or oversample. A plug-in that adds latency reports it (`latencySamples()`) so the host can line tracks up again. Both starters and every effect in `modules/fx` report zero.

### DC offset

**DC offset** is a constant shift away from zero, a 0 Hz component. You can't hear it directly, but it wastes headroom, makes the start and end of a sound click, and can upset effects further down the chain. Asymmetric distortion is a common source. A gentle high-pass filter at a few Hz to a few tens of Hz removes it; the starter effect's delay uses a 40 Hz low cut in its feedback path for a similar reason, so low rumble can't build up.

### Denormals and NaN

Two kinds of number cause trouble in audio code:

- **Denormals** (subnormal floats) are tiny numbers near zero, for example the tail of a decaying filter or feedback loop. On many CPUs, arithmetic on them is dozens of times slower, so a plug-in that goes quiet can suddenly use far *more* CPU. Studio's effects flush anything below 1e-30 to exactly zero (`fx::detail::safe` in `modules/fx/include/kraftonic/fx/Detail.h`), and the synth module's tests check denormal flushing.
- **NaN** ("not a number") and infinity come from things like dividing by zero or an unstable filter. One NaN spreads through every calculation it touches and silences the output, or worse, sends a burst of noise to the host. Studio's rule is that output is always finite, and the tests check it.

## 3. Building blocks of a synth

The Starter Synth is a classic **subtractive** synth: start with a sound rich in harmonics, then filter some away and shape the result over time. Here is one voice, from the comment at the top of `StarterSynthEngine.h`:

```
osc 1 ─┐
       ├─ mix ─> resonant low-pass ─> amp ─> sum of voices ─> output
osc 2 ─┘             ^                 ^
                     └──── ADSR ───────┘   (one envelope, two jobs)
```

### Oscillators

An oscillator makes a repeating waveform at a given frequency. Its shape decides which **harmonics** (multiples of the fundamental frequency) it contains, and so its tone:

| Wave | Harmonics | Sounds |
|---|---|---|
| Sine | Only the fundamental | Pure, soft, flute-like or sub-bass |
| Saw | All harmonics, falling as 1/n | Bright, buzzy, the classic synth string and brass |
| Square | Odd harmonics only | Hollow, woody, reedy |
| Triangle | Odd harmonics, falling fast | Between sine and square, mellow |

**Why naive saws and squares alias.** The simple way to make a saw is a counter that ramps up and jumps back down. That jump is instantly sharp, which means its harmonics go on forever, far beyond Nyquist, and they fold back as aliasing. The higher the note, the worse it gets. **Band-limited** oscillators remove the content above Nyquist. `kraftonic::synth::Oscillator` (`modules/synth/include/kraftonic/synth/Oscillator.h`) uses PolyBLEP, a small correction applied right at each jump that smooths it just enough. Its waveforms are `Waveform::saw`, `pulse`, `triangle`, `sawTriangle` and `sine`; the Starter Synth offers saw, square (`pulse` at 50 % width) and sine.

**Two oscillators and detune.** `osc2Detune` shifts oscillator 2 by cents (hundredths of a semitone). A few cents of difference makes the two waves drift in and out of phase, which thickens the sound (a slow "beating"). A large detune turns it into an interval. `oscMix` crossfades between the two.

**Where:** `Voice::osc1`, `Voice::osc2` and `render()` in `plugins/startersynth/dsp/StarterSynthEngine.cpp`.

### Noise

Noise has energy at all frequencies at once. **White** noise is equal energy per hertz and sounds bright and hissy; **pink** noise falls 3 dB per octave and sounds more even and natural. Noise is how you make breath, wind, snare and hi-hat sounds, and a little of it adds air to a pad. `kraftonic::synth::Noise` in `modules/synth/include/kraftonic/synth/Noise.h` gives `white()` and `pink()`. The Starter Synth doesn't use it yet; it's a good second project after section 6.

### Filters

A filter changes the level of frequencies. In a subtractive synth the main one is a **low-pass** filter, which lets lows through and takes highs away.

- **Cutoff:** the frequency where the filter starts cutting. Lower is darker and duller; higher is brighter. Turning the cutoff is the single most expressive move in subtractive synthesis.
- **Resonance:** a boost right at the cutoff. A little sounds vocal and focused; a lot sounds whistling or "squelchy". Near the maximum many filters **self-oscillate**, ringing as a sine on their own. The Starter Synth scales its resonance by `kMaxFilterResonance` (0.85) to keep a strong but tame peak.
- **Slope:** how fast the level falls above the cutoff, in dB per octave. A 12 dB/octave filter (two "poles") sounds gentle and open; 24 dB/octave (four poles) is steeper and darker.
- **Keyboard tracking:** moving the cutoff with the note played, so high notes are as bright as low ones. The Starter Synth has no key tracking; you could add it by adding a fraction of `(voice.pitch - 60) / 12` octaves to the cutoff in `render()`.

The Starter Synth uses a 12 dB/octave `synth::StateVariableFilter` in low-pass mode (`SVFMode::lowPass`). `modules/synth/include/kraftonic/synth/Filters.h` also has a four-pole `LadderFilter` (24 dB/octave, with drive) and simple `OnePoleLowPass` and `OnePoleHighPass` filters.

### Envelopes (ADSR)

An envelope is a control signal from 0 to 1 that shapes a note over time. The usual one has four stages:

| Stage | What it is | Short | Long |
|---|---|---|---|
| Attack | Time to rise from 0 to full after a key goes down | Percussive start | Swell, fade-in |
| Decay | Time to fall from full to the sustain level | Plucky, snappy | Slow bloom |
| Sustain | The *level* held while the key stays down (not a time) | 0 = the note dies away | 1 = organ-like hold |
| Release | Time to fall to 0 after the key comes up | Abrupt stop | Long tail |

`kraftonic::core::Adsr` (`modules/core/include/kraftonic/core/Adsr.h`) has exponential segments, which sound natural, and minimum times (`kMinAttackSeconds` and friends) that keep the start and end click-free. In the Starter Synth one envelope does two jobs: it sets the voice's level, and it opens the filter. The cutoff for each sample is `cutoff × 2^(envAmount × envelope)`, so `envAmount` is in octaves: at +2, the filter opens two octaves above the cutoff knob at the envelope's peak.

### LFOs and modulation

An **LFO** (low-frequency oscillator) is an oscillator too slow to hear, usually 0.1 to 20 Hz, used to move another control. Routed to pitch it gives vibrato; to level, tremolo; to cutoff, a "wah" sweep. **Modulation** is the general idea: one signal (an envelope, an LFO, velocity, the mod wheel) changing a parameter of another. `kraftonic::synth::Lfo` in `modules/synth/include/kraftonic/synth/Modulation.h` has triangle, sine, saw, square and sample-and-hold waves (`LfoWave`), a rate in Hz or in beats, delay, fade-in and key sync. The Starter Synth has no LFO yet. The engine already receives `Event::Type::modWheel` events; `handleEvent` simply ignores them for now.

### Voices and polyphony

Each note needs its own oscillators, filter and envelope: a **voice**. The number of voices is the **polyphony**. The Starter Synth has `kMaxVoices = 8`. When a ninth note arrives, one voice must be **stolen**. `kraftonic::core::VoiceAllocator` (`core/VoiceAllocator.h`) decides which: a voice already playing the same note, else a free one, else the oldest released one (it's the quietest), else the oldest held one. A stolen voice keeps its state, and its envelope restarts from the current level, so stealing never clicks.

### Glide

**Glide** (portamento) slides the pitch from the previous note to the new one instead of jumping. It's the sound of a sliding bass or a singing lead. In the Starter Synth, each sample moves a fixed fraction of the remaining distance (a one-pole slide in pitch), so the `glide` parameter is a time constant in seconds. `synth::Glide` in `Modulation.h` offers constant-time and constant-rate versions.

### Parameter smoothing and zipper noise

If a gain jumps from 0.2 to 0.8 between two samples, the waveform gets a step in it, and a step is a click. Many small steps, from a knob turned slowly or automation applied once per block, make a buzzing, stepped sound called **zipper noise**. The cure is **smoothing**: ramp to the new value over a few milliseconds. `core::LinearSmoother` (`core/Smoother.h`) reaches each new target in a fixed time; the Starter Synth uses 20 ms for six controls. It smooths the cutoff in octaves (`std::log2` of the value), so a sweep sounds even across the range. Discrete choices like the waveform aren't smoothed, and the envelope times don't need it.

### Velocity

**Velocity** is how hard a key was struck, from 0 to 1 in `Event::value`. The Starter Synth multiplies each voice's output by it, so soft notes are quieter. Many synths also map velocity to filter cutoff or attack time, so harder notes are brighter or snappier as well as louder.

## 4. Building blocks of effects

### The StereoEffect contract

Every effect in `modules/fx` has the same shape, defined in `modules/fx/include/kraftonic/fx/Effect.h` and checked at compile time by the `StereoEffect` concept:

```cpp
struct Parameters { ... };                        // plain values: Hz, ms, dB, 0..1
void prepare (const core::ProcessSetup&);         // NOT real time; may allocate
void reset() noexcept;                            // clear tails and state
void setParameters (const Parameters&) noexcept;  // once per block; smoothed inside
void process (float* left, float* right, int n) noexcept;   // in place, stereo
int latencySamples() const noexcept;
```

Each `Parameters` struct has `bool enabled`, and most have `float mix`. Turning an effect off crossfades over 5 ms instead of clicking, and a disabled effect is bit-exact pass-through once the crossfade is done. Effects never see a product's parameter ids: the product fills each effect's `Parameters` from its own table. The Starter Effect does exactly that in `StarterEffectEngine::setParameters`.

### Gain, drive and waveshaping

**Gain** is multiplication: louder or quieter. **Drive** pushes the signal into a curve that flattens the peaks, a **waveshaper**. `fx::Drive` uses a `tanh` curve, which is smooth near zero and leans over gently near full scale. Bending the waveform adds **harmonics**: a soft, symmetric curve adds mostly odd harmonics; an asymmetric one (positive and negative halves treated differently, as `fx::Drive` does) adds even harmonics too, which tends to sound warmer. Light drive sounds like warmth and thickness; heavy drive sounds like fuzz.

Those new harmonics can go above Nyquist and alias, just like a naive oscillator. `fx::Drive` uses **antiderivative anti-aliasing** (ADAA, in `Drive::adaa`), which reduces aliasing without the latency and CPU of oversampling. Its `tone` control is a low-pass after the shaper that tames the fizz. Drive raises the level, so it compensates its output (`1 / sqrt (gain)`); always compare a driven sound with the dry one at the same loudness (section 5).

### EQ

An **equalizer** boosts or cuts frequency ranges to shape tone, not to create it. `fx::EQ` has three bands: a **low shelf** (raises or lowers everything below a frequency), a **peak** (a bell around a centre frequency, with Q setting its width) and a **high shelf**. Each band is a **biquad**, a small two-pole filter, designed with the classic Audio EQ Cookbook formulas (section 11). Cut to fix and boost to flavour: a narrow cut removes a harsh ring; a broad, gentle shelf adds warmth or air.

### Delay

A **delay** plays the input back later. It's built on a **circular buffer** (a ring buffer): an array where you write each new sample at a moving position and read an older one from a fixed distance behind it, wrapping around at the end. `fx::detail::Ring` is Studio's ring buffer. It reads between samples with linear interpolation, so the delay time can change smoothly.

- **Time:** distance between the input and its echo. Under about 50 ms the ear hears a thicker sound rather than an echo ("slapback" lives around 80–150 ms); longer times are distinct repeats.
- **Feedback:** how much of the output goes back into the input, so each echo produces the next one, each `feedback` times quieter. At 1.0 or above the echoes never fade, so `fx::Delay` caps it at 0.95 and the starter at 0.9.
- **Damping:** a low-pass in the feedback loop, so each repeat is darker than the last, like a vintage tape echo. In the starter it's `damping` (mapped to `highCutHz`), plus a fixed 40 Hz low cut.
- **Ping-pong:** the feedback crosses between left and right, so echoes bounce from side to side.

`fx::Delay` holds up to two seconds, allocated in `prepare()`. It can also follow the host tempo (`tempoSync`, `noteDivision`, `setTempo`).

### Chorus and phaser

Both are **modulated** effects: an LFO keeps changing something, and the result is mixed with the dry signal.

- **Chorus** reads a short delay (around 10–25 ms) whose time wobbles slowly. The delayed copy's pitch rises and falls slightly, and mixed with the dry sound it sounds like several players at once: wide, shimmering. `fx::Chorus` uses one to three taps.
- **Phaser** passes the signal through a chain of **all-pass** filters, which keep every frequency's level but shift its phase. Mixed with the dry sound, some frequencies cancel and make notches in the spectrum; the LFO sweeps those notches up and down for the familiar whooshing sound. `fx::Phaser` has 4 to 8 stages, feedback and a stereo phase offset.

### Reverb

**Reverb** imitates a room: thousands of reflections that blur into a smooth, decaying tail. A common design, used by `fx::Reverb`, is a **feedback delay network**: several delay lines of different lengths whose outputs are mixed by a matrix (here an eight-point Hadamard matrix) and fed back. A pre-delay sets the gap before the tail (bigger rooms have longer ones), `decaySeconds` sets how long the tail lasts, `damping` darkens it as it fades, and `width` sets how wide it spreads.

### Dry/wet mix and bypass

**Mix** (dry/wet) blends the untouched input with the processed signal. Delay and reverb usually sit at 10–40 % wet on an insert; at 100 % wet on a send. **Bypass** passes the input through untouched. Hosts offer a bypass button for every plug-in, and Studio's effect base handles it with a crossfade and latency compensation (`formats/LatencyBypass.h`), so engaging it never clicks.

## 5. A sound design primer

Load the Starter Synth, choose the Init preset and try these recipes. They use only its own controls. The point is less the result than hearing *why* each move does what it does.

### A bass

1. Osc 1 **Square**, osc 2 **Sine**, Detune 0, Mix about 40 %. The square gives body and bite; the sine reinforces the fundamental.
2. Cutoff low, about 300–500 Hz; Resonance about 40 %. Bass lives below a few hundred hertz, and a little resonance gives the attack a "bump".
3. Env Amt +2 octaves, Attack as short as it goes, Decay about 0.4 s, Sustain about 50 %. Each note opens bright and settles darker, which makes it punchy.
4. Release short (0.1 s) so notes don't smear together. Add a little Glide (50–100 ms) and play overlapping notes for slides.

This is close to the Glide Bass preset. Play it low (below C2) and high (C3): notice how the low-pass makes high notes duller, which is what keyboard tracking would fix.

### A pad

1. Two saws, or a saw and a square, with Detune at 10–15 cents. The slow beating between them is what makes a pad feel alive.
2. Cutoff around 1 kHz, low Resonance, Env Amt about +1 octave.
3. Attack 0.5–1 s, Decay 1–2 s, Sustain about 80 %, Release 1.5–2.5 s. Slow attack and release turn notes into swells that overlap.
4. Play chords and hold them. If the low end gets muddy, raise the cutoff less and play the chord higher.

### A lead

1. Two saws, Detune 5–7 cents (the Init sound is already close).
2. Cutoff around 2 kHz, Resonance 30–40 %, Env Amt +1 to +2 octaves.
3. Attack short, Decay 0.3 s, Sustain 70 %: the note speaks immediately and stays present while held.
4. Glide 30–60 ms for a singing, connected feel when you play legato.

A lead must cut through a mix, so it lives in the upper midrange and keeps some brightness. Check it against a drum loop, not on its own.

### A pluck

1. Two saws, a small negative Detune.
2. Cutoff very low (300 Hz), Resonance about 35 %, Env Amt +3.5 octaves.
3. Attack minimum, Decay 0.2–0.3 s, **Sustain 0**, Release about the same as Decay.

With sustain at zero, every note dies away like a plucked string, and because the envelope also closes the filter, the sound gets darker as it fades, the way real plucked strings do. That one link between level and brightness is most of what makes it sound natural. Compare it with the Pluck preset.

### Designing an effect chain

Order matters, because each stage processes the output of the one before:

- **Drive before delay** (the Starter Effect's order): each echo is a copy of the distorted sound, clean and separate.
- **Delay before drive:** the echoes pile up and are distorted together, which gets dense and muddy fast.
- **Reverb last:** it places everything before it in the same space. Put a delay after a reverb and you get echoes of a room, which is rarely what you want.
- **EQ** goes before drive to choose what gets distorted (cut lows to keep it tight), after it to tidy the result.

### Listening critically

- **Level-match before you judge.** Louder sounds better to everyone, every time. Before deciding whether a change improves a sound, set the output so both versions are equally loud, then A/B them. The Output knob exists for this.
- **Check in mono.** Many listeners hear one speaker. Stereo tricks (detune, chorus, ping-pong) can thin out or cancel when the channels are summed. Your DAW's mono button or a utility plug-in does it.
- **Watch the low end.** Small speakers and headphones hide it, and a little too much bass can swamp a mix. Look at an analyser (section 8) and check on more than one system.
- **Listen to transients.** The first few milliseconds of a note decide whether it sounds sharp or soft, close or distant. Change the attack time by small amounts and listen to just the start.
- **Rest your ears.** After loud listening everything starts to sound the same. Keep the volume moderate and take breaks.

### Making good presets

- Give each preset one clear job and a plain name that says what it is ("Warm Pad", "Glide Bass"). Never use the name of a real instrument maker or model.
- Level-match the set: every preset should sit at a similar loudness, under 0 dBFS when playing chords. Use `outputGain` to balance them, and check with the render tool, which prints peak and RMS per run.
- Make each one responsive: it should sound good across the keyboard and react to velocity.
- In Studio a preset lists plain values by key in `plugin/<P>Presets.h`; anything left out keeps its default. A test checks that every key exists and that every preset differs from Init.

## 6. Your first changes, step by step

Steps 6a and 6b were carried out on the starters and verified with the fast DSP-only build and the starters' tests before this guide was written, and the `new-product.sh` dry run in 6c was checked the same way. Do them on your own copy (step 6c), or on the starters in a branch to learn.

The build you'll use throughout:

```sh
# Configure once (no plug-in SDK download):
cmake -S . -B build/dsp -G Ninja -DKRAFTONIC_BUILD_PLUGINS=OFF -DKRAFTONIC_WARNINGS_AS_ERRORS=ON
# Build and run one product's tests:
cmake --build build/dsp && ctest --test-dir build/dsp --output-on-failure -R '^startersynth: '
```

### 6a. Starter Synth

#### Change a preset or a default

Open `plugins/startersynth/plugin/StarterSynthPresets.h`. Each preset is an array of key and plain value pairs. To give Warm Pad a longer tail, change its release:

```cpp
inline constexpr PresetValue kWarmPad[] {
    {"osc1Wave", 0.0F}, {"osc2Wave", 1.0F}, {"osc2Detune", 12.0F}, {"oscMix", 0.45F},
    {"cutoff", 1200.0F}, {"resonance", 0.15F}, {"envAmount", 1.0F},
    {"attack", 0.6F}, {"decay", 1.5F}, {"sustain", 0.8F}, {"release", 2.5F}, {"outputGain", -8.0F}};
```

A default lives in the parameter table, `dsp/StarterSynthParameters.h`, in the seventh column (`min`, `max`, then `default`). Changing a default changes the Init sound and every preset that doesn't set that parameter. Build, run the tests, and listen to the render (section 8).

#### Add a parameter end to end: a sub-oscillator

A **sub-oscillator** is a sine one octave below the note, a classic way to add weight to a bass. You'll add a `subLevel` parameter from 0 to 1, defaulting to 0, so existing songs and presets sound exactly as before.

**1. The parameter table** (`dsp/StarterSynthParameters.h`). Append a new id before `count`, with the next free number, and a row in the same position:

```cpp
enum class ParamId : std::uint32_t
{
    …
    glide = 11,
    outputGain = 12,
    subLevel = 13,
    count
};
…
    {ParamId::outputGain, "outputGain", "Output", "dB", -60.0F, 0.0F, -6.0F, ParamScale::linear, 0},
    {ParamId::subLevel, "subLevel", "Sub Level", "%", 0.0F, 1.0F, 0.0F, ParamScale::linear, 0},
}};
```

Never insert in the middle, renumber, or reuse an old id or key, even one you deleted: saved songs store them. A test checks that ids match their row positions and that keys are unique. Old songs that don't contain id 13 simply get the default (the state codec fills missing ids with defaults), which is why a new parameter's default should leave the sound unchanged.

**2. The engine** (`dsp/StarterSynthEngine.h`). Add an oscillator to the voice and a smoother for the level:

```cpp
    struct Voice
    {
        synth::Oscillator osc1;
        synth::Oscillator osc2;
        synth::Oscillator sub; // one octave below osc 1
        …
    };
    …
    core::LinearSmoother mix_, detune_, cutoffOctaves_, resonance_, envAmount_, gain_, sub_;
```

Then in `dsp/StarterSynthEngine.cpp`, follow the life of the new members through every stage. In `prepare()` (not real time):

```cpp
        voice.osc2.prepare (sampleRate_);
        voice.sub.prepare (sampleRate_);
        …
    for (auto* smoother : {&mix_, &detune_, &cutoffOctaves_, &resonance_, &envAmount_, &gain_, &sub_})
        smoother->prepare (sampleRate_, kSmoothingSeconds);
```

In `reset()` and in `noteOn()`, where a silent voice starts clean, reset it with the others: `voice.sub.reset();`.

In `setParameters()`, set the target, and add `&sub_` to the list of smoothers that snap after a reset:

```cpp
    gain_.setTarget (decibelsToGain (params.get (ParamId::outputGain)));
    sub_.setTarget (params.get (ParamId::subLevel));
    if (snapSmoothers_)
    {
        for (auto* smoother : {&mix_, &detune_, &cutoffOctaves_, &resonance_, &envAmount_, &gain_, &sub_})
            smoother->setImmediate (smoother->getTarget());
        snapSmoothers_ = false;
    }
```

In `render()`, advance the smoother once per sample (every smoother steps every sample, voices or not, so the result doesn't depend on the block size) and add the sub to the oscillator mix:

```cpp
        const float gain = gain_.next();
        const float subLevel = sub_.next();
        …
            voice.osc2.setFrequency (hz * detuneRatio);
            voice.sub.setFrequency (hz * 0.5);
            const float oscillators = voice.osc1.process (wave1) * (1.0F - mix) + voice.osc2.process (wave2) * mix
                                    + voice.sub.process (synth::Waveform::sine) * subLevel;
```

Nothing here allocates, locks or waits: the oscillator is a plain member, sized when the voice array is. The sub goes through the filter and envelope like the other oscillators, so it follows the note.

**3. Value text.** `plugin/StarterSynthValueText.h` formats values by the table's `unit` column, so a `"%"` parameter already shows "50 %" and parses "50" back. Only switches with named positions need a change here. The value-text test round-trips every parameter, including the new one.

**4. The panel.** Add a knob to `ui/make_startersynth_uidesc.py`:

```python
        [kp.Section ("OSCILLATORS", [kp.Select ("Osc 1", "osc1Wave", ["Saw", "Square", "Sine"]),
                                     kp.Select ("Osc 2", "osc2Wave", ["Saw", "Square", "Sine"]),
                                     kp.Knob ("Detune", "osc2Detune", bipolar=True),
                                     kp.Knob ("Mix", "oscMix"),
                                     kp.Knob ("Sub", "subLevel")]),
```

Then regenerate the panel and check it:

```sh
python3 plugins/startersynth/ui/make_startersynth_uidesc.py
python3 modules/ui/tools/check_panel.py plugins/startersynth/ui/startersynth.uidesc \
    --params-header plugins/startersynth/dsp/StarterSynthParameters.h \
    --generator plugins/startersynth/ui/make_startersynth_uidesc.py
```

The script rewrites `startersynth.uidesc` and `startersynth.panel.json`; commit all three files and never edit the generated ones by hand. Every parameter must appear on the panel exactly once. In a full build, the `ui: startersynth panel` test runs the same check.

**5. A preset.** Give Glide Bass some weight by appending `{"subLevel", 0.5F}` to `kGlideBass` in `plugin/StarterSynthPresets.h`.

**6. A measurable test.** Add a test to `tests/StarterSynthTests.cpp` that proves the feature does what it says, using the helpers already in that file (`render`, `sineVoice`, `magnitudeAt`, `peak`):

```cpp
TEST_CASE ("the sub oscillator adds a sine one octave below")
{
    auto p = sineVoice();
    p.set (ParamId::osc1Wave, static_cast<float> (Wave::saw));
    const auto without = render (p, {on (0.0, 69)}, 0.6);
    p.set (ParamId::subLevel, 1.0F);
    const auto with = render (p, {on (0.0, 69)}, 0.6);
    // 220 Hz is not in a 440 Hz saw at all, so it comes only from the sub.
    CHECK (magnitudeAt (without, 220.0, 0.1, 0.6) < 0.001);
    CHECK (magnitudeAt (with, 220.0, 0.1, 0.6) > 0.1);
    CHECK (peak (with) < 1.0F);
}
```

It measures the spectrum at the exact frequency, before and after, and checks the level. "It runs" is not a test of a sound.

**7. Build and run.** `cmake --build build/dsp && ctest --test-dir build/dsp --output-on-failure -R '^startersynth: '`. The new test, the realtime test (which counts allocations), the block-size test and the plug-in tests (state, presets, value text) should all pass.

### 6b. Starter Effect: add a reverb stage

The same steps, applied to `plugins/startereffect`. You'll add an `fx::Reverb` after the delay, with one new parameter, `reverbMix`.

**1. The table** (`dsp/StarterEffectParameters.h`):

```cpp
    outputGain = 7,
    reverbMix = 8,
    count
…
    {ParamId::reverbMix, "reverbMix", "Reverb", "%", 0.0F, 1.0F, 0.0F, ParamScale::linear, 0},
```

**2. The engine.** In `dsp/StarterEffectEngine.h`, include `<kraftonic/fx/Reverb.h>` and add a member, `fx::Reverb reverb_;`. In `dsp/StarterEffectEngine.cpp`, give it the same four calls as the other effects:

```cpp
void StarterEffectEngine::prepare (const ProcessSetup& setup)
{
    drive_.prepare (setup);
    delay_.prepare (setup); // allocates two seconds of delay line per channel
    reverb_.prepare (setup);
    …
}
// in reset():
    reverb_.reset();
// in setParameters(), after the delay:
    fx::Reverb::Parameters reverb;
    reverb.enabled = params.get (ParamId::reverbMix) > 0.0F;
    reverb.mix = params.get (ParamId::reverbMix);
    reverb_.setParameters (reverb);
// in process():
    drive_.process (block.left, block.right, block.numSamples);
    delay_.process (block.left, block.right, block.numSamples);
    reverb_.process (block.left, block.right, block.numSamples);
```

The reverb allocates its delay lines in `prepare()` and smooths its own controls; switching it on or off crossfades. Leaving the other `Reverb::Parameters` at their defaults keeps the change small; making size or decay a knob is the next step.

**3. The tail.** Hosts stop calling an effect some time after its input goes silent, using `tailSeconds()` in `plugin/StarterEffectTraits.h`. With a reverb, the tail must cover its decay as well as the echoes:

```cpp
        const double echoes = time * (repeats + 1.0) + 0.1;
        const double reverb = params.get (ParamId::reverbMix) > 0.0F ? 2.4 * 1.5 : 0.0; // fx::Reverb's default decay
        return std::min (std::max (echoes, reverb), 60.0);
```

**4. The panel and a preset.** In `ui/make_startereffect_uidesc.py`, change the mix section to `kp.Section ("MIX", [kp.Knob ("Dry/Wet", "mix"), kp.Knob ("Reverb", "reverbMix")])` and run the script (then `check_panel.py` as above). Add `{"reverbMix", 0.25F}` to `kDarkPingPong` in `plugin/StarterEffectPresets.h`.

**5. A test** in `tests/StarterEffectTests.cpp`, using its `run`, `sine` and `peak` helpers: a 100 ms burst, echoes off, and a check that only the reverb rings on afterwards.

```cpp
TEST_CASE ("the reverb adds a tail after the input stops")
{
    auto p = StarterEffectParameters::defaults();
    p.set (ParamId::drive, 0.0F);
    p.set (ParamId::mix, 0.0F); // no echoes: only the reverb rings on
    const auto burst = sine (440.0, 0.5F, 1.0, 0.1);
    CHECK (peak (run (p, burst).left, 0.3, 0.5) == 0.0F);
    p.set (ParamId::reverbMix, 0.5F);
    const auto wet = run (p, burst);
    CHECK (peak (wet.left, 0.3, 0.5) > 0.001F);
    CHECK (peak (wet.left) < 1.0F);
}
```

Then `cmake --build build/dsp && ctest --test-dir build/dsp --output-on-failure -R '^startereffect: '`. The existing "no drive and no mix is bit-exact pass-through" test still passes, because a disabled reverb is bit-exact too.

### 6c. Make it yours

When you're ready to build your own plug-in rather than change a starter, copy one:

```sh
scripts/new-product.sh "My Synth" --from synth --vendor "Your Name" --dry-run   # show the plan
scripts/new-product.sh "My Synth" --from synth --vendor "Your Name"
```

The dry run prints the plan: the folder (`plugins/mysynth`), the class names (`MySynthEngine`, `MySynthParameters`, `MySynthTraits`), the plug-in target, freshly generated processor and controller IDs, a new state magic and new Audio Unit codes. Without `--dry-run` it copies the folder, renames every file, class and display name, regenerates the panel and adds the product to the root `CMakeLists.txt`. Your plug-in can't be called "Kraftonic" ([TRADEMARKS.md](../TRADEMARKS.md)), and the script refuses that name.

Then read `plugins/mysynth/README.md` ("What to change first"), make the changes above in your copy, and build it with the plug-ins on (`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build`) to load it in your DAW. Remember that your new IDs become permanent the day you share a build.

## 7. The real-time rules, explained

The audio thread has a deadline. At 48 kHz with 128-sample blocks, your `process` call gets well under 2.7 ms, shared with every other plug-in in the project. Missing it once causes an audible dropout. So the rule isn't "be fast on average"; it's "never take an unpredictable amount of time". That rules out:

| Don't, on the audio thread | Why | Do instead |
|---|---|---|
| Allocate (`new`, `std::vector::push_back`, `std::string`) | The allocator can take a lock or ask the OS for memory, with no time limit | Size everything in `prepare()` / `setupProcessing`; use fixed arrays (`std::array`) |
| Lock a mutex or wait | If the UI thread holds the lock, the audio thread waits for it (priority inversion) | Atomics, or a lock-free single-producer single-consumer queue |
| File or network I/O | Disks and networks stall for milliseconds or seconds | Load files in `prepare()` or on another thread; hand over a ready pointer |
| Log or print | Logging allocates, locks and does I/O | Count events in an atomic and report them from the UI thread |
| Throw exceptions | Throwing allocates and unwinding takes unbounded time | `noexcept` everywhere on the audio path; clamp bad values instead |

In Studio, the audio-thread methods are `process`, `setParameters` and `reset`, and they are all `noexcept`. `prepare` is not real time, so it may allocate: the Starter Effect's delay allocates its two-second lines there.

**Talking between threads.** The UI and audio threads share data through atomics or lock-free queues. `formats/UiNoteQueue.h` has `SpscQueue`, a lock-free single-producer single-consumer queue used for notes from the on-screen keyboard. Output meters go the other way as hidden read-only parameters that the controller caches in relaxed atomics. Mutexes are allowed only on the controller and UI side (`formats/PendingParamUpdates.h`).

**Smoothing** is also a real-time rule: anything that can jump audibly must ramp (section 3).

**Block-size independence.** The host may call you with 64 samples, then 333, then 1. The result must be identical however the stream is cut up. That means: keep all state in members (no per-block resets), advance smoothers per sample rather than per block, and handle events at their sample offset. The Starter Synth's `process()` renders up to each event's offset, applies it, and continues, so a note always starts on the exact sample the host asked for. The test "output does not depend on the host block size" renders the same phrase with 64-sample and 333-sample blocks and requires bit-identical output.

**Any sample rate.** Never hard-code 44100 or 48000. Compute coefficients from the sample rate given to `prepare()` (filter cutoffs, smoothing times, delay lengths in samples), and recompute them when it changes. The starters test 22.05, 44.1, 96 and 384 kHz.

**How the rules are checked.** Each product has a `realtime and performance` test. The Starter Synth's (`tests/StarterSynthRealtime.cpp`) replaces the global `operator new` and `delete` to count every allocation while it plays eight voices and jumps every parameter from end to end on every block; it fails on a single allocation, or if rendering is under 3× faster than real time.

## 8. Testing, listening and looking

### The tests

`ctest` runs every test; filter by product with `-R`:

```sh
ctest --test-dir build/dsp --output-on-failure -R '^startersynth: '
```

The Starter Synth's tests measure the sound instead of just running it:

- `tests/StarterSynthTests.cpp`: silence without notes; a full chord finite and under 0 dBFS; note 69 at 440 Hz and detune in cents (by counting zero crossings); the low-pass removing harmonics above the cutoff (with a Goertzel filter, which measures one frequency); envelope amount, attack and release times; voices freeing themselves and the sustain pedal; glide; a smoothed level jump; block-size independence; every sample rate.
- `tests/StarterSynthPluginTests.cpp`: dense ids and unique keys; state round trip and rejection of corrupt data; preset keys exist and every preset changes the sound; value text round trips; original names only.
- `tests/StarterSynthRealtime.cpp`: no allocation, enough speed (section 7).

The Starter Effect has the same three, measuring pass-through, echo timing and level, ping-pong, damping, drive harmonics, tone and tail length.

### The render tool

Each starter has a renderer that plays its presets into a WAV file and prints peak and RMS levels:

```sh
build/dsp/plugins/startersynth/tests/startersynth_render build/listening/startersynth.wav
```

(In a full build with the plug-ins, it's at `build/<dir>/bin/Release/startersynth_render`.) The synth plays a chord and a short melody per preset; the effect runs a plucked phrase through each preset. Open the WAV in any audio editor to listen and to look at the waveform and spectrum. Use it before and after a change, at matched levels.

### A DAW and an analyser

Nothing replaces playing the plug-in. Build with the plug-ins on, copy the `.vst3`, `.clap` or `.component` to your plug-in folder ([BUILDING.md](BUILDING.md), "Install for testing") and load it. Put a spectrum analyser and an oscilloscope plug-in after yours; most DAWs include both, and free ones are easy to find. The analyser shows aliasing (peaks that move the wrong way as you play higher), DC (energy at 0 Hz) and too much low end; the oscilloscope shows clicks, clipping and the waveform itself.

### The check and the validators

`scripts/check.sh <product>` is the check to run once before a pull request. It builds the product, runs its tests and realtime test, and runs the validators; it prints `RESULT: PASS` or `RESULT: FAIL`. `scripts/check.sh all` checks everything, and `--full` adds pluginval, which is slow and heavy on the CPU (GitHub Actions always runs it for pull requests). The validators each test the plug-in the way hosts use it:

- **Steinberg validator:** the VST3 SDK's own conformance test for `.vst3` bundles.
- **pluginval:** a stress test of VST3 plug-ins (random parameters, odd block sizes, editor open and close, threading).
- **clap-validator:** the CLAP project's conformance test for `.clap` plug-ins.
- **auval:** Apple's Audio Unit validation tool, run on the `.component` on macOS.

## 9. Common problems and how to find them

When something sounds wrong, render it and measure it rather than guess. The `debugging` skill in `.agents/skills/debugging/SKILL.md` has the full procedure.

| Symptom | Likely cause | Where to look |
|---|---|---|
| Clicks or pops when notes start or stop | Envelope too short, voice reset while audible, oscillator phase jump | `Adsr` minimum times; what `noteOn()` resets for a voice that is still active |
| Clicks when you turn a knob | A parameter applied without smoothing | Is it in a `LinearSmoother`? Is `next()` called every sample? |
| Zipper noise on slow knob moves or automation | Value applied once per block | Smooth per sample (section 3) |
| Harsh, metallic tones that move the wrong way with pitch | Aliasing from an oscillator or distortion | Use `synth::Oscillator`, not a naive wave; ADAA or oversampling for shapers |
| Level jumps between presets or when switching a stage | Unmatched preset levels; no crossfade on enable | Render tool peak and RMS; the effect's `enabled` flag (it crossfades) |
| Thumps, lost headroom, asymmetric waveform | DC offset | Analyser at 0 Hz; add a high-pass after asymmetric stages |
| CPU rises when the sound fades out | Denormals in a feedback loop or filter tail | Flush tiny values to zero (`fx::detail::safe`) |
| Sudden silence, or a burst of noise | NaN or infinity | Unstable filter (resonance or cutoff out of range), division by zero; check `std::isfinite` in tests |
| Stuck notes | A note-off that never releases its voice; pedal state | `noteOff()`, `releaseVoice()`, `sustainPedal` and `allNotesOff` in `handleEvent()` |
| Sound changes with the buffer size | State reset or a smoother stepped per block; events not split at their offset | `process()` and the block-size test |
| Wrong pitch at some sample rates | Hard-coded rate | Everything computed from `prepare()`'s `sampleRate` |
| Plug-in doesn't show up in the DAW | Not in the plug-in folder, host hasn't rescanned, scan failed, duplicate class IDs | Install folders in [BUILDING.md](BUILDING.md); rescan; run the validator; never copy a starter by hand, use `new-product.sh` |
| Audio Unit missing on macOS | Not registered | `auval -a` in Terminal; restart the host |

For crashes and memory errors, an AddressSanitizer build (`-DCMAKE_CXX_FLAGS=-fsanitize=address`) usually points straight at the bug. Check [KNOWN_ISSUES.md](KNOWN_ISSUES.md) before chasing a validator problem on Linux.

## 10. Glossary

| Term | Meaning |
|---|---|
| ADSR | Attack, decay, sustain, release: the four stages of a common envelope |
| Aliasing | Content above Nyquist folding back as unrelated, inharmonic frequencies |
| All-pass filter | A filter that changes phase but not level; the core of a phaser |
| Band-limited | Containing no frequencies above Nyquist |
| Biquad | A two-pole, two-zero filter; the usual building block of an EQ |
| Block (buffer) | The group of samples processed in one call |
| Cents | Hundredths of a semitone |
| Cutoff | The frequency where a filter starts to cut |
| dBFS | Decibels relative to full scale; 0 dBFS is the largest sample value |
| DC offset | A constant shift of the waveform away from zero |
| Denormal | A tiny float near zero that is slow to compute on many CPUs |
| Dry/wet | The unprocessed and processed signals, and the blend of the two |
| Feedback | Output fed back into the input, as in a delay's repeats |
| Harmonic | A frequency at a whole-number multiple of the fundamental |
| Host | The application that loads and runs plug-ins, usually a DAW |
| LFO | Low-frequency oscillator, used for modulation |
| Normalized value | A parameter value from 0 to 1, as hosts see it |
| Nyquist frequency | Half the sample rate; the highest frequency it can represent |
| Plain value | A parameter value in real units, as the engine sees it |
| Polyphony | How many notes can sound at once |
| Resonance | A boost at a filter's cutoff |
| Sample rate | Samples per second, per channel |
| Smoothing | Ramping a parameter to a new value to avoid clicks |
| SPSC queue | A lock-free queue with one writer thread and one reader thread |
| Tail | How long a plug-in keeps sounding after its input stops |
| Voice | The set of oscillators, filter and envelope that plays one note |
| Waveshaper | A curve applied to each sample; the core of drive and distortion |

## 11. Where to learn more

Free online:

- [VST 3 Developer Portal](https://steinbergmedia.github.io/vst3_dev_portal/): Steinberg's documentation for the VST3 format and SDK.
- [CLAP](https://github.com/free-audio/clap): the CLAP headers and documentation; [clap-validator](https://github.com/free-audio/clap-validator) is next to it.
- [Audio Toolbox](https://developer.apple.com/documentation/audiotoolbox): Apple's documentation for Audio Units.
- [Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/): the standard biquad filter formulas, hosted by the W3C.
- [Julius O. Smith III's online books](https://ccrma.stanford.edu/~jos/) at Stanford's CCRMA: filters, spectral processing, physical modelling, digital waveguides. Deep, precise and free.
- [The Scientist and Engineer's Guide to Digital Signal Processing](https://www.dspguide.com/) by Steven W. Smith: the clearest gentle introduction to sampling, convolution and filters.
- [pluginval](https://github.com/Tracktion/pluginval): the plug-in stress tester, with its documentation.

Books:

- *Designing Audio Effect Plugins in C++* and *Designing Software Synthesizer Plugins in C++* by Will Pirkle: practical, code-first.
- *The Art of VA Filter Design* by Vadim Zavalishin: how analogue-style filters like the state-variable and ladder filters in `modules/synth` are built.
- *Musimathics* by Gareth Loy: the maths and physics of music, explained for musicians.

Inside this repository, read next: [ARCHITECTURE.md](ARCHITECTURE.md) for how the kit fits together, the module READMEs (`modules/core`, `modules/synth`, `modules/fx`, `modules/formats`, `modules/ui`) for what each provides, and [AGENTS.md](../AGENTS.md) for the rules every contribution follows.
