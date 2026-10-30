# AGENTS.md: Kraftonic Studio

Kraftonic Studio is a free, MIT-licensed kit for building audio plug-ins (VST3 and CLAP on Windows, macOS and Linux, plus AU on macOS): shared C++ DSP modules, the plug-in format layer, a UI kit, and two starter templates, **Starter Synth** and **Starter Effect**. `scripts/new-product.sh <name> --from synth|effect` starts a new plug-in from one. Audio quality, usability, performance, stability and maintainability come before new features. These rules apply to every contributor, person or AI agent. How to open a pull request is in [CONTRIBUTING.md](CONTRIBUTING.md).

## Repository map

| Path | What |
|---|---|
| `modules/core`, `modules/fx`, `modules/sampler` | SDK-free shared DSP, plus the parameter, instrument and effect contracts |
| `modules/formats` | SDK-free helpers shared by all formats + the VST3 and CLAP adapters (templates over a plug-in's traits); AU wraps the VST3 |
| `modules/<other>` | other shared code promoted from products (synth, piano, wavetable, ui) |
| `plugins/<id>/` | one plug-in, not part of the kit: `CMakeLists.txt`, `dsp/` (SDK-free engine), `plugin/` (traits, presets, VST3 entry), `ui/`, `tests/`, `README.md` |
| `scripts/`, `cmake/`, `packaging/`, `.github/` | checks, CI, build helpers, installers; `cmake/KraftonicPlugin.cmake` is the public product helper |
| `docs/` | architecture, technical spec, building, known issues |

Read before editing: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/SPEC.md](docs/SPEC.md), the product's `README.md`, and every contract header your change touches.

## Build and test

```sh
# Fast DSP loop (no SDK download):
cmake -S . -B build/dsp -G Ninja -DKRAFTONIC_BUILD_PLUGINS=OFF -DKRAFTONIC_WARNINGS_AS_ERRORS=ON
cmake --build build/dsp && ctest --test-dir build/dsp --output-on-failure -R '^startersynth: '

# One product with its plug-in: the targets are in plugins/<p>/README.md.
# Before a pull request (must print RESULT: PASS):
scripts/check.sh startersynth
```

`scripts/check.sh <product>` builds the product, runs its tests and real-time test and the Steinberg validator (`--full` adds pluginval; CI always runs it); it adds module tests and other products by itself when shared code changes. Build only your own targets while iterating, and run the check once, at the end. Never claim a result you didn't produce. More commands: [docs/SPEC.md §4](docs/SPEC.md#4-build-commands-macos-dev-machine).

## Architecture rules

- Dependencies point one way: products → modules → core. A product never includes another product.
- Code moves into a module when a second product needs it, not before. Describe the move in the pull request.
- `modules/core`, `modules/fx`, `modules/sampler`, other DSP modules and `plugins/*/dsp` never include a plug-in SDK, UI toolkit or OS header.
- The shared contract headers ([docs/SPEC.md §1](docs/SPEC.md#1-contracts)) and the root `CMakeLists.txt` change only after a maintainer agrees in an issue.

## Code rules

- C++20. Every new file starts with `// SPDX-License-Identifier: MIT`.
- Formatting: 4-space indents, braces on their own lines, a space before call parentheses (`process (block)`). Match the surrounding code; never reformat code you didn't change.
- Parameter IDs and keys, plug-in class IDs, Audio Unit codes (`AU_SUBTYPE`, `AU_MANUFACTURER`), the `BUNDLE_IDENTIFIER` (also the CLAP plug-in id) and state magic numbers are permanent once shipped. Add new ones; never rename or reuse old ones. Every new product gets unique class IDs, AU codes and bundle id.
- The build is warning-free with `-DKRAFTONIC_WARNINGS_AS_ERRORS=ON` on Clang, GCC and MSVC. SDK sources are exempt; ours are not.
- Behaviour changes need tests in the product's or module's `tests/` folder. DSP changes need measurable tests (levels, spectra, timing), not just "it runs".
- Sign off every commit (`git commit -s`). Never commit `build/`, sample audio, secrets or machine-specific paths.

## Real-time audio rules

- **The audio thread** (`process`, `setParameters`, `reset`) never allocates, locks, waits, does I/O, logs or throws. Preallocate in `prepare` / `setupProcessing`.
- **Between threads:** lock-free SPSC queues or atomics only. Mutexes live on the controller/UI side.
- **Sound:** smooth anything that can jump audibly. Output is always finite. Normal playing stays under 0 dBFS.
- **Robustness:** results must not depend on how the host splits blocks. Any sample rate from 22.05 to 384 kHz works.

## Content, licensing and brand

- All code, presets, names, artwork and panel designs are original. A product may be **inspired by** a classic instrument's features and signal flow, but never by its look (colours, panel layout, knob styles, logos).
- No maker or model names anywhere: product names, code, presets, docs, commit messages (Rhodes, Wurlitzer, Hammond, Leslie, Nord, Moog, Roland, Sequential, Oberheim, Yamaha, Korg, …). Product tests enforce this.
- **Samples:** your own recordings, CC0 or CC BY only. Credit each source in `THIRD_PARTY_NOTICES.md`. Never commit sample audio, and never name the maker of a recorded instrument.
- **Third-party code** needs a compatible licence (MIT, BSD, Apache-2.0, zlib, ISC, or OFL for fonts) and an entry in `THIRD_PARTY_NOTICES.md`. No copyleft code.
- **Brand:** the Kraftonic name, logos and K mark are not covered by the code licence; [TRADEMARKS.md](TRADEMARKS.md) governs them. Brand assets live outside this repository.

## Skills

Step-by-step procedures for audio engineering, debugging and verification are in `.agents/skills/<name>/SKILL.md`.
