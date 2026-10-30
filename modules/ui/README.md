# modules/ui: the common Kraftonic GUI kit

Kit panels draw from one set of assets (`modules/ui/kit`) in the
instrument look: LED-collar knobs, rotary and LED selectors, rubber LED
buttons, OLED readouts, section panels with a header strip, Barlow Condensed
silkscreen and Barlow Semi Condensed values, amber `#FFB43C` LEDs on a
`#121315` chassis, and a header with the product name and an optional text wordmark. See
this README.

| Path | What |
|---|---|
| `tools/kraftonic_panel.py` | panel description → `<p>.uidesc` + `<p>.panel.json` (the control → ParamId table) |
| `tools/check_panel.py` | checks a `.uidesc` (run by the `ui:` tests) |
| `tools/kit_resources.py` | `.uidesc` → bundle resource list (bitmaps both scales, fonts) |
| `tools/snapshot/` | `kraftonic_editor_snapshot`: renders any built `.vst3` editor to PNG (macOS) |
| `kit/generator/render_kit.py` | renders every kit PNG and `manifest.json` from the tokens (developer step, never a build step) |
| `kit/assets/` | rendered PNGs (1× and `#2x`), `manifest.json`, OFL fonts |
| `tests/` | self-test of the kit (`ui: panel library self-test`) |

There is no C++ library: the stock `CAnimKnob`, `COnOffButton`,
`CVerticalSwitch`, `CNinePartTiledBitmap`, `COptionMenu`, `CTextEdit` and
`CTextLabel` plus kit bitmaps produce the look. Add shared C++ views (`kraftonic::ui`) only if a
product really needs one, and document it here.

## A new product panel

1. Write `plugins/<p>/ui/make_<p>_uidesc.py`. It lists sections and controls only:

   ```python
   #!/usr/bin/env python3
   # SPDX-License-Identifier: MIT
   import os, sys
   HERE = os.path.dirname (os.path.abspath (__file__))
   REPO = os.path.normpath (os.path.join (HERE, "..", "..", ".."))
   sys.path.insert (0, os.path.join (REPO, "modules", "ui", "tools"))
   import kraftonic_panel as kp

   PANEL = kp.Panel (
       title="KRAFTONIC STARTER SYNTH",
       params=kp.params_from_header (os.path.join (
           REPO, "plugins/startersynth/dsp/StarterSynthParameters.h")),
       header=kp.Header (preset_tag=1000, output="outputGain"),
       rows=[
           [kp.Section ("OSC", [kp.Menu ("Wave", "oscWave"), kp.Knob ("Fine", "oscFine", bipolar=True)]),
            kp.Section ("FILTER", [kp.Knob ("Cutoff", "cutoff"), kp.Knob ("Reso", "resonance")])],
           [kp.Section ("ENVELOPE", [kp.Knob ("A", "attack"), kp.Knob ("D", "decay"),
                                     kp.Knob ("S", "sustain"), kp.Knob ("R", "release")]),
            kp.Section ("VOICE", [kp.Toggle ("Legato", "legato"), kp.Value ("Bend", "bendRange")])],
       ],
   )

   if __name__ == "__main__":
       kp.main (PANEL, HERE, "startersynth")
   ```

2. Run it: `python3 plugins/<p>/ui/make_<p>_uidesc.py`. Commit the script, `<p>.uidesc` and `<p>.panel.json`. Never edit the generated files by hand.
3. Pass the panel to `kraftonic_add_plugin` in `plugins/<p>/CMakeLists.txt`, as the starters do:

   ```cmake
   kraftonic_add_plugin(KraftonicStarterSynth
       PRODUCT startersynth
       ...
       UIDESC ui/startersynth.uidesc
       UI_GENERATOR ui/make_startersynth_uidesc.py
       PARAMS_HEADER dsp/StarterSynthParameters.h)
   ```

   `UIDESC` adds the `.uidesc`, every bitmap it uses (1× and `#2x`) and the kit
   fonts to the bundle. The `.uidesc` declares flat basename paths because
   the SDK copies resources by file name only; VSTGUI picks up `name#2x.png`
   automatically. Each bundle holds only the assets its panel declares.
   It also adds the ctest `ui: startersynth panel`, which runs `check_panel.py` with the generator and parameter header. `scripts/check.sh` runs every `ui:` test. (Without `kraftonic_add_plugin`, call `kraftonic_add_ui_kit_resources` and `kraftonic_add_panel_test` yourself.)
4. Take a snapshot (below) and save it as `plugins/<p>/screenshot.png`.

### Controls

Every control fills one grid cell (`span=2` for two). Each one has a silkscreen label above it (uppercased by the generator). It binds one ParamId by its enum name. The plug-in formats the value (`getParamStringByValue`), so units and menu entries come from the parameter table, not from the panel.

| Control | Use for | Kit rendering |
|---|---|---|
| `Knob (label, param, size="small", bipolar=False)` | continuous values; value readout below (click to type). `bipolar=True` lights the collar from the centre for ± amounts (not available in `large`) | `CAnimKnob` over `knob_led` / `knob_led_bipolar` (44 px, 128 frames: dark knurled cap, white pointer, 13 amber LEDs over 270°); `size="large"` uses `knob_led_l` (56 px) |
| `Rotary (label, param, options)` | list parameters with 5 or 6 short options (octave range) | `CAnimKnob` over `rotary_5` / `rotary_6` (one frame per option, lit tick) with the option labels in silkscreen around it |
| `Select (label, param, options)` | list parameters with 2 or 3 options (trigger, priority) | `CVerticalSwitch` over `led_select_2` / `led_select_3` (one LED per option; click an LED) with each label to its right |
| `Menu (label, param)` | other list parameters (mode, shape, destination) | transparent `COptionMenu` on `oled` glass, OLED text |
| `Toggle (label, param, title="On")` | on/off parameters | `COnOffButton` over `btn_led` (rubber cap + LED); in a step grid, `led_toggle` (LED only) |
| `Value (label, param)` | a small typed number with no knob (steps, bend range) | transparent `CTextEdit` on `oled` glass |
| `SectionLabel (text)` | a sub-heading cell inside a section (no parameter) | |

`Rotary` and `Select` options are the silkscreen labels in parameter order; write them to match the plug-in's value strings (nothing checks this automatically). Other option counts need a new strip in `render_kit.py`.

`Section (title, controls, columns=None)` puts its controls on one line, or wraps them after `columns` cells. Each entry in `rows` is a row of sections. Sections in a row share one height. Rows are justified to the widest one: spare width is shared out per cell, so cells stay evenly spaced. A section is a `panel_section` panel (radius 10, edge, 26 px header strip) with its group label in the strip.

`step_grid (title, row_labels, rows)` is a compact table for sequencers: one column per step, one line per control kind (each cell a `Menu` or `Toggle`, with no label of its own; the row labels sit at the left and the step numbers on top). It is a section like any other, so put it in a row of `rows`; it takes the full row width.

The header is a floating `header_panel` bar: an optional plain-text wordmark (`Panel (wordmark="ACME")`; the starters use `KRAFTONIC`, and `new-product.sh` removes it, see TRADEMARKS.md), the product name in Barlow Condensed, the version label, the preset bar (rubber `<`, the preset menu on OLED glass, rubber `>`, the preset category in LED amber) and the output-level knob. An auto-sized panel widens itself if the header needs more room. A fixed `width` that is too narrow is an error.

A parameter that really has no place on the panel goes in `Panel (hidden=[...])`. Otherwise every ParamId must appear exactly once, or generation fails.

The generated `.uidesc` carries no licence line unless you set one, e.g. `Panel (licence="SPDX-License-Identifier: MIT")` as the starters do; change or remove it for your own plug-in.

## Theme tokens

They live in `THEME` at the top of `kraftonic_panel.py` (and the matching `T` in `kit/generator/render_kit.py`) and are shared by every product. Change them there, never per product.

| Token | Values |
|---|---|
| colours | `Chassis` #121315 (background) · `Panel` #26282C · `Silk` #E8E5DE (control labels, header text) · `Group` #A9A79F (group labels) · `Value` #B9B6AE (knob values) · `Led` #FFB43C · `Oled` #050506 · `OledText` #F4F5F7 · `OledDim` #8E8E95 |
| fonts | `Title` Barlow Condensed Bold 20 (product name) · `Header` / `Section` / `Label` Barlow Condensed Bold 12 / 11 / 11 · `Value` / `Oled` Barlow Semi Condensed 12 |
| bitmaps | `KIT_BITMAPS` in `kraftonic_panel.py`: knob filmstrips by size, rotary and LED selector strips by option count, `btn_led`, `led_toggle`, `btn_prev` / `btn_next`, `panel_section`, `header_panel`, `oled`, optional text wordmark |
| spacing | margin 12 · gap 10 · section pad 8 · header strip 26 · label 13 · value 15 · line gap 4 · header 56 · menu 21 · preset row 28 · selector row 16 |
| knob sizes | small 44 (cell 52) · medium 44 (cell 56) · large 56 (cell 68); labelled cells widen past this only when their label needs it |

## Asset rules

- **Only what products use.** `kit/assets/` holds the rendered controls and materials some product uses (about 2.2 MB with the fonts), never previews or unused looks, and never a generated logo.
- **The build only copies.** Rendering is a developer step, never a build step.
- **Bundles stay lean.** The CMake helper adds exactly the bitmaps a `.uidesc` declares (both scales) plus the fonts — nothing else.
- **Header wordmark:** optional plain text in the kit font and colours, never a logo image. Brand assets are kept outside this repository.

## Re-rendering kit assets

Prerequisites: Python 3.9+, `pip install numpy pillow`. No browser.

```sh
python3 modules/ui/kit/generator/render_kit.py   # rewrites kit/assets/{controls,materials} + manifest.json
```

The renderer draws every shape from the tokens as signed-distance fields at 4× supersampling, for 1× and `#2x`, and writes `manifest.json`. Add a new control or option count to `catalogue ()`, then re-run the product generators plus the `ui:` tests. Delete any asset no product uses.

## Fonts and platform behaviour

VSTGUI loads bundle fonts from `Resources/Fonts/` on every platform: it registers every TTF there when the editor opens and resolves `font-name` against the registered families, falling back to the declared `alternative-font-names` only when a family is missing (Arial Narrow / Helvetica Neue / Arial). Checked in the pinned VSTGUI sources: macOS (`vstgui/lib/platform/mac/cfontmac.mm`, `RegisterBundleFonts`), Windows (`vstgui/lib/platform/win32/direct2d/d2dfont.cpp`, `Fonts\` under the module folder), Linux (`vstgui/lib/platform/linux/cairofont.cpp`, `resourcePath + "Fonts/"`).

- **Names.** macOS matches `font-name` against the *typographic* family (`Barlow Condensed`, `Barlow Semi Condensed`), then applies `bold="true"` as a trait. A weight name in `font-name` (`Barlow Condensed Bold`) is not a family and falls back. The kit therefore declares the family plus bold, which picks the Bold file; the non-bold fonts take the family's regular-weight file.
- **macOS:** confirmed in the shipped snapshots.
- **Windows / Linux:** same registration path; untested here — snapshots are needed before release.

## Known limitation: long value strings

Menus and value fields size to their grid cell, but the strings come from the plug-in at runtime and can be arbitrarily long; `COptionMenu` / `CTextEdit` clip instead of overlapping. Barlow Semi Condensed is narrower than the previous fonts, so most value strings fit; check new long strings in the snapshot.

## Rules

- **Labels never overlap.** No view may overlap a sibling or leave its parent, and label text must fit its box. `check_panel.py` enforces all three. A `CNinePartTiledBitmap` frame may contain the control drawn on top of it; anything else overlapping is an error.
- **One page.** One template, no tabs or hidden pages; every control is visible at once.
- **Signal-flow order.** Sections run left to right, top to bottom, in the order sound flows: sources → mixer → filter → amp/envelopes → modulation → voice/performance. Output sits in the header.
- **Short labels.** About 10 characters for a medium cell (`Vel>Cutoff` is the longest that fits). Use the plug-in's value text for detail.
- **Original look.** Shared kit only; no colours, layouts or names taken from any existing instrument.

## What the checks cover

`check_panel.py <p>.uidesc [--params-header H] [--generator script]` fails when:

- the file is not valid XML, or has more than one template;
- a view lies outside its parent, overlaps a sibling (except a nine-part frame containing its control), or a label's text is wider than its box (Barlow estimate);
- a view names an undeclared colour, gradient, font or bitmap, a tag name or number is declared twice, or a declared tag is unused;
- a bitmap is missing from the kit manifest, its 1× or `#2x` file is missing, or `height-of-one-image` × `sub-pixmaps` doesn't match the image;
- a ParamId in the header enum is missing from `<p>.panel.json`, appears twice, or has the wrong number;
- with `--generator`, the committed files differ from a fresh run of the script.

## Snapshot (macOS)

```sh
cmake --build build/check --target kraftonic_editor_snapshot KraftonicStarterSynth
build/check/bin/Release/kraftonic_editor_snapshot build/check/VST3/Release/KraftonicStarterSynth.vst3 plugins/startersynth/screenshot.png
```

`kraftonic_editor_snapshot <bundle.vst3> <out.png> [scale]` loads the bundle as a host would (component + controller, state synced). It opens the editor in an off-screen window and writes a PNG at `scale` pixels per point (default 2). It needs no host and no screen-recording permission. It works for any VST3 bundle with an NSView editor, not only Kraftonic ones.
