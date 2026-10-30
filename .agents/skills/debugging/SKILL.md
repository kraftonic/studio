---
name: debugging
description: Use when a Kraftonic build or test fails, a plug-in crashes or fails validation, a host misbehaves, or something sounds wrong (clicks, NaNs, wrong level, stuck notes).
---

# Debugging

## When to use
Any failure or unexpected behaviour, before attempting a fix.

## Steps
1. **Reproduce** with one exact command and capture the failing output. Narrow it down with `ctest -R '<name>'`, or a single plug-in under the validator or pluginval.
2. **Check known issues** first: docs/KNOWN_ISSUES.md (pluginval GUI tests on Linux, REAPER hidden parameters, and others).
3. **Isolate:**
   - DSP vs adapter vs UI: does it fail in the fast DSP build (`-DKRAFTONIC_BUILD_PLUGINS=OFF`)?
   - Platform: macOS only, or Linux too?
   - Block size or sample rate dependent?
4. **Use the right tool:**
   - **Memory errors and crashes:** an AddressSanitizer build (`-DCMAKE_CXX_FLAGS=-fsanitize=address`).
   - **Real-time violations:** the `*: realtime and performance` tests.
   - **Bad sound:** render a WAV and measure it (peak, RMS, spectrum) instead of guessing.
5. **Explain the root cause** in one sentence that names the file and the mechanism, then fix it.
6. **Add a regression test** that fails before the fix and passes after it.

## Evidence
The failing command and output before the fix, the root-cause sentence, the same command passing after the fix, and the new regression test.

## Stop and escalate
- The cause is in third-party code (the SDK, VSTGUI, a host or pluginval): document it with a control experiment, like the one in KNOWN_ISSUES.md.
- It only reproduces on a platform or host you can't run.
- The fix needs a contract or state-format change.
