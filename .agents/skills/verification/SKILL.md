---
name: verification
description: Use before opening, reviewing or merging any Kraftonic Studio pull request, to produce real pass/fail evidence with the right build, test and validation commands for that change type.
---

# Verification

## When to use
Before opening or updating a pull request, a review verdict, or a merge. A claim without command output counts as unverified.

## Steps
Keep the machine responsive: set `CMAKE_BUILD_PARALLEL_LEVEL` to what it can spare. While iterating, run only step 1 (or your product's own targets and tests). Run the merge check (step 3) and pluginval **once**, at the end, and don't repeat them unless code changed after a failure.

Pick the steps that match the change and run them from the repo root. Paths for the shared SDK and pluginval caches are in docs/SPEC.md §4.

1. **Fast DSP loop** (any change under `modules/core|fx|sampler`, `plugins/*/dsp`):
   ```sh
   cmake -S . -B build/dsp -G Ninja -DKRAFTONIC_BUILD_PLUGINS=OFF -DKRAFTONIC_WARNINGS_AS_ERRORS=ON
   cmake --build build/dsp
   ctest --test-dir build/dsp --output-on-failure -R '^<prefix>: '   # core: fx: sampler: formats: piano: synth: wavetable: ui: startersynth: startereffect:
   ctest --test-dir build/dsp --output-on-failure -R 'realtime'
   ```
2. **Full plug-in build** (adapter, UI, presets, state, `modules/formats|ui`):
   ```sh
   cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DKRAFTONIC_WARNINGS_AS_ERRORS=ON \
     -DFETCHCONTENT_SOURCE_DIR_VST3SDK=$HOME/.cache/kraftonic/sdk381/vst3sdk
   cmake --build build/mac && ctest --test-dir build/mac --output-on-failure
   build/mac/bin/Release/validator build/mac/VST3/Release/Kraftonic<Product>.vst3
   ```
   Then run pluginval on the same bundle (command in docs/SPEC.md §4).
3. **Merge check** (before opening or updating a pull request; the reviewer re-runs it or reuses the author's result): `scripts/check.sh <product>`, which must print `RESULT: PASS`.
   - It runs the product's tests, real-time test and the Steinberg validator; `--full` adds pluginval at strictness 10 (run it for changes to `plugin/`, `ui/` or `modules/formats`). Module tests run only when `modules/` changed.
   - It widens to every product by itself when `modules/core|fx|sampler|formats`, `cmake/`, the root `CMakeLists.txt`, CI or packaging change.
   - Commit first: the output's `Commit:` line names the commit it checked, and a result with uncommitted changes can't be reused by the reviewer.
   - GitHub Actions runs the macOS, Windows and Linux suite on every pull request.
4. **Audible changes:** render WAVs (`<product>_render`). Report their paths along with measured peak/RMS, before and after.
5. **UI changes:** regenerate the product screenshot with its snapshot tool (`plugins/<p>/README.md`), save it as `plugins/<p>/screenshot.png`, and look at it.

## Evidence
For each command: the command itself, the exit status, and the summary lines (ctest's `100% tests passed, 0 tests failed out of N`, the validator's passed/failed counts, the pluginval result, `RESULT: PASS`). Also note the commit hash that was tested (the `Commit:` line).

## Stop and escalate
- A check fails and the failure isn't caused by your change: report it with its output, and don't disable or skip the check.
- A required check can't run here (Windows, Linux Docker, no DAW): mark it "not run" and give the reason.
- Never change a test's expectations just to make it pass unless the task says the behaviour should change.
