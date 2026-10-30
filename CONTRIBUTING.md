# Contributing to Kraftonic Studio

Kraftonic Studio is an open-source kit for building audio plug-ins: the shared modules, the plug-in format layer (VST3, CLAP, AU), the UI kit, and two starter templates (Starter Synth, Starter Effect).

Thanks for helping build tools for making instruments and effects. Everyone is welcome: people, and people working with AI agents, follow the same rules. This is a small, early project: keep things simple, and ask in an issue before starting large changes.

New to DSP, sound design or plug-ins? [docs/GUIDE.md](docs/GUIDE.md) is a beginner's guide that walks through the starters.

## Quick start

```sh
git clone https://github.com/kraftonic/studio && cd studio
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Per-platform requirements are in [docs/BUILDING.md](docs/BUILDING.md). For a fast DSP-only loop with no SDK download, add `-DKRAFTONIC_BUILD_PLUGINS=OFF`.

## Starting a new product

Start from a template instead of an empty folder: `scripts/new-product.sh "My Synth" --from synth` (or `--from effect`) copies `plugins/startersynth` or `plugins/startereffect`, renames it, gives it new permanent plug-in IDs and registers it in the build. Each template's README explains how it is put together and what to change first. In your fork, the new product lives next to the starters; to keep a plug-in in a repository of its own, see [docs/BUILDING.md](docs/BUILDING.md).

## Ground rules

- **Licence:** code, docs and presets are contributed under the MIT License. Each new source file starts with `// SPDX-License-Identifier: MIT`. The Kraftonic name and logos are not part of that licence (see [TRADEMARKS.md](TRADEMARKS.md)).
- **Original work only.** Do not contribute samples, presets, firmware, artwork or code copied from commercial products or instruments. Hardware may inspire you; do not copy it. New third-party code needs a compatible licence and an entry in `THIRD_PARTY_NOTICES.md`.
- **Behaviour:** follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Sign your commits (DCO)

Every commit must carry a `Signed-off-by:` line (`git commit -s`). It certifies the [Developer Certificate of Origin 1.1](https://developercertificate.org/): you wrote the change, or you have the right to submit it under the project's licence. You keep the copyright to your contribution. There is no CLA.

## Contributing sounds and presets

Sounds are judged as carefully as code.

- **Allowed sources:** your own recordings or patches, CC0, CC BY 3.0 or CC BY 4.0.
- **Not allowed:** NC (non-commercial), ND (no-derivatives) or SA (share-alike) licences; "free for any use" pages that name no licence; anything sampled from commercial instruments, libraries or presets.
- **Provenance:** say where every sample came from (the recording setup, or the source URL with its licence and a SHA-256 hash). Third-party sources are credited in `THIRD_PARTY_NOTICES.md`. Sample audio is never committed to git.
- **Names:** never use a maker's name for a sound ("Rhodes", "Hammond", "Nord", …), and never name the maker of a recorded instrument.
- **Quality:** clean edits (no clicks, DC offset or clipped peaks), consistent levels, and a short demo render with the pull request.

## Engineering rules

The rules for every contributor, human or AI, are in [AGENTS.md](AGENTS.md): architecture, code style, real-time audio safety and content. How the codebase fits together is in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md); the contracts are in [docs/SPEC.md](docs/SPEC.md).

## Pull requests

1. Fork the repository and create a branch from `main`. Open an issue first for a new module, a change to a shared contract ([docs/SPEC.md §1](docs/SPEC.md#1-contracts)), a new dependency, or anything that changes shipped sounds.
2. Keep each pull request to one logical change, inside one product where possible. Sign off every commit.
3. Commit, then run `scripts/check.sh <product>`. Paste its output in the template, from the `Check:` and `Commit:` lines to `RESULT: PASS`.
4. A maintainer who isn't the author reviews it; [.github/CODEOWNERS](.github/CODEOWNERS) requests the review automatically. "Changes requested" means pushing fixes to the same branch and re-running the check.
5. Maintainers merge into `main`, tag and release. GitHub Actions builds and validates every pull request on macOS, Windows and Linux.

For audible changes, describe how it sounds and attach a short clip or WAV renders with peak/RMS. For UI changes, include a fresh `plugins/<p>/screenshot.png`.

AI-assisted contributions are welcome under the same rules. You are responsible for what you submit: read the diff, run the check yourself, and sign off only work you have the right to contribute.

### What the check runs

`scripts/check.sh <product>` is sized to the change, measured from the merge base with `main` (`KRAFTONIC_CHECK_BASE=<ref>` overrides it):

- **Only `plugins/<p>/` changed:** build, that product's tests and real-time test, and the Steinberg validator. `--full` adds pluginval at strictness 10; GitHub Actions always runs it.
- **A module other than core, fx, sampler or formats changed:** the above for every product that uses it, plus all module tests.
- **`modules/core`, `fx`, `sampler` or `plugin`, `cmake/`, the root `CMakeLists.txt`, `scripts/ci/` or `packaging/` changed:** everything, as `scripts/check.sh all`.

Its first lines name the commit it checked (`Commit: <hash>`). When that hash equals the pull request's head commit and the line doesn't mention uncommitted changes, the reviewer may reuse the author's result instead of re-running it. The summary is also saved in `build/check/logs/summary-<product>.txt`. The check holds a machine-wide lock, so two checks on one computer run one after the other.
