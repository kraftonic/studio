# Known issues

## Linux: no automated editor test

pluginval v1.0.4 crashes inside its own run loop while driving a VSTGUI editor under `xvfb`, and Steinberg's own AGain sample crashes the same way, so the cause isn't in Kraftonic code. Steinberg's `editorhost` crashes under `xvfb` too.

- On Linux, CI runs pluginval at strictness 10 with `--skip-gui-tests`. All audio, state, parameter and threading tests still run.
- macOS and Windows run the full pluginval suite, including GUI tests.
- Until a newer pluginval or VSTGUI fixes this, check the editor by hand in a Linux DAW such as REAPER or Ardour.

## Linux: pluginval can't run the Steinberg validator itself

On Linux, pluginval passes the inner `Contents/x86_64-linux/*.so` path to the Steinberg validator, which rejects it ("is not a module directory"). CI doesn't pass `--vst3validator` to pluginval on Linux. The Steinberg validator still runs on the bundle in its own CI step, on every platform.

## REAPER lists internal adapter parameters

"Sustain Pedal" and "All Notes Off" are flagged hidden and read-only (`kIsHidden | kIsReadOnly`), as the VST3 MIDI-mapping design requires, but REAPER still lists them. This is cosmetic.
