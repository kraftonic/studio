#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Self-test of the panel kit (ctest "ui: panel library self-test"): builds a
# demo panel with every control type, checks it with check_panel, and proves
# the checks catch overlaps, out-of-bounds views, unknown tags and missing or
# duplicated parameters.

import contextlib
import io
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert (0, os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "tools"))
import check_panel  # noqa: E402
import kraftonic_panel as kp  # noqa: E402

HEADER = """
enum class ParamId : std::uint32_t
{
    outputGain = 0,
    mode = 1, // 0 a, 1 b
    cutoff, /* implicit 2 */
    amount = 5,
    sync,
    steps,
    range,
    voice,
    count
};
"""
PARAMS = kp.params_from_text (HEADER)


def demo (**kw):
    args = dict (
        title="KRAFTONIC DEMO",
        wordmark="KRAFTONIC",
        params=PARAMS,
        header=kp.Header (preset_tag=1000, output="outputGain", sub_controller_prefix="Demo"),
        rows=[[
            kp.Section ("SOURCE", [kp.Menu ("Mode", "mode"), kp.Toggle ("Sync", "sync"),
                                   kp.Value ("Steps", "steps")]),
            kp.Section ("SWITCHES", [kp.Rotary ("Range", "range", ("32\u2032", "16\u2032", "8\u2032",
                                                                   "4\u2032", "2\u2032")),
                                     kp.Select ("Voice", "voice", ("Mono", "Poly"))]),
            kp.Section ("FILTER", [kp.SectionLabel ("LP"), kp.Knob ("Cutoff", "cutoff", size="large"),
                                   kp.Knob ("Amount", "amount", bipolar=True, size="small")],
                       columns=2),
        ]],
        width=1100,
    )
    args.update (kw)
    return kp.Panel (**args)


class PanelTests (unittest.TestCase):
    def test_header_parse (self):
        self.assertEqual (PARAMS, {"outputGain": 0, "mode": 1, "cutoff": 2, "amount": 5,
                                   "sync": 6, "steps": 7, "range": 8, "voice": 9})

    def test_demo_passes_all_checks (self):
        text, mapping = kp.build (demo ())
        root = ET.fromstring (text)
        self.assertEqual (check_panel.check_tree (root), [])
        self.assertEqual (check_panel.check_mapping (root, mapping, PARAMS), [])
        self.assertEqual (mapping["size"][0], 1100)
        kinds = sorted ({c["kind"] for c in mapping["controls"]})
        self.assertEqual (kinds, ["knob", "menu", "rotary", "select", "toggle", "value"])

    def test_sections_in_a_row_share_height_and_fill_width (self):
        text, _ = kp.build (demo ())
        root = ET.fromstring (text)
        sections = [v for v in root.find ("template") if v.get ("class") == "CViewContainer"]
        rects = [check_panel._rect (s) for s in sections]
        self.assertEqual (len ({r[3] for r in rects}), 1)
        margin = kp.THEME["spacing"]["margin"]
        self.assertEqual (rects[-1][0] + rects[-1][2], 1100 - margin)

    def test_step_grid_binds_every_cell_and_passes_checks (self):
        header = "enum class ParamId : std::uint32_t { a0, a1, a2, b0, b1, b2, out, count };"
        params = kp.params_from_text (header)
        grid = kp.step_grid ("STEPS", ["A", "B"], [
            [kp.Menu ("A", f"a{i}") for i in range (3)],
            [kp.Toggle ("B", f"b{i}", title="B") for i in range (3)]])
        panel = kp.Panel (title="T", params=params, rows=[[grid]],
                          header=kp.Header (output="out"), width=400)
        text, mapping = kp.build (panel)
        root = ET.fromstring (text)
        self.assertEqual (check_panel.check_tree (root), [])
        self.assertEqual (check_panel.check_mapping (root, mapping, params), [])
        self.assertEqual (len ([c for c in mapping["controls"] if c["section"].startswith ("STEPS")]), 6)
        with self.assertRaisesRegex (ValueError, "needs"):
            kp.step_grid ("S", ["A"], [[kp.Menu ("A", "a0")], [kp.Menu ("A", "a1")]])

    def test_generator_output_is_deterministic (self):
        with tempfile.TemporaryDirectory () as tmp:
            kp.write (demo (), tmp, "demo")
            first = open (os.path.join (tmp, "demo.uidesc"), "rb").read ()
            kp.write (demo (), tmp, "demo")
            self.assertEqual (first, open (os.path.join (tmp, "demo.uidesc"), "rb").read ())

    def test_missing_and_duplicate_params_are_refused (self):
        rows = [[kp.Section ("ONE", [kp.Knob ("Cutoff", "cutoff")])]]
        with self.assertRaisesRegex (ValueError, "missing"):
            kp.build (demo (rows=rows))
        rows = [[kp.Section ("ONE", [kp.Menu ("Mode", "mode"), kp.Menu ("Mode", "mode"),
                                     kp.Knob ("C", "cutoff"), kp.Knob ("A", "amount"),
                                     kp.Toggle ("S", "sync"), kp.Value ("St", "steps"),
                                     kp.Menu ("R", "range"), kp.Menu ("V", "voice")])]]
        with self.assertRaisesRegex (ValueError, "more than once"):
            kp.build (demo (rows=rows))
        with self.assertRaisesRegex (ValueError, "unknown parameter"):
            kp.build (demo (rows=[[kp.Section ("X", [kp.Knob ("X", "nope")])]]))

    def test_hidden_params_may_stay_off_the_panel (self):
        rows = [[kp.Section ("ONE", [kp.Knob ("Cutoff", "cutoff")])]]
        _, mapping = kp.build (demo (rows=rows, hidden=["mode", "amount", "sync", "steps",
                                                        "range", "voice"]))
        self.assertEqual (len (mapping["controls"]), 2)

    def test_too_narrow_panel_is_refused (self):
        with self.assertRaisesRegex (ValueError, "width"):
            kp.build (demo (width=200))

    def test_checker_catches_layout_and_tag_errors (self):
        text, mapping = kp.build (demo ())
        root = ET.fromstring (text)
        sections = [v for v in root.find ("template") if v.get ("class") == "CViewContainer"]
        section = next (s for s in sections if any (v.get ("title") == "CUTOFF" for v in s))
        knob = next (v for v in section if v.get ("class") == "CAnimKnob")
        label = next (v for v in section if v.get ("class") == "CTextLabel" and v.get ("title") == "CUTOFF")
        knob.set ("origin", label.get ("origin"))           # overlap a sibling
        label.set ("title", "A label far too long for any cell " * 3)  # text too wide
        menu = next (v for s in sections for v in s if v.get ("class") == "COptionMenu")
        menu.set ("origin", "5000, 0")                       # out of bounds
        menu.set ("control-tag", "ghost")                    # undeclared tag
        menu.set ("font-color", "NoSuchColour")              # undeclared colour
        errors = "\n".join (check_panel.check_tree (root))
        for needle in ("overlaps", "text needs", "outside its parent", "'ghost' is not declared",
                       "unknown colour", "declared but no view uses it"):
            self.assertIn (needle, errors)

    def test_mapping_check_catches_drift (self):
        text, mapping = kp.build (demo ())
        root = ET.fromstring (text)
        params = dict (PARAMS, newParam=9)
        errors = "\n".join (check_panel.check_mapping (root, mapping, params))
        self.assertIn ("'newParam' is on the panel 0 times", errors)
        params = dict (PARAMS, cutoff=3)
        errors = "\n".join (check_panel.check_mapping (root, mapping, params))
        self.assertIn ("header says 3", errors)

    def test_invalid_xml_fails (self):
        with tempfile.TemporaryDirectory () as tmp:
            path = os.path.join (tmp, "bad.uidesc")
            with open (path, "w") as f:
                f.write ("<vstgui-ui-description><template>")
            out = io.StringIO ()
            with contextlib.redirect_stdout (out):
                self.assertEqual (check_panel.main ([path]), 1)
            self.assertIn ("not valid XML", out.getvalue ())

    def test_demo_uses_filmstrips_and_kit_bitmaps (self):
        text, _ = kp.build (demo ())
        root = ET.fromstring (text)
        classes = {v.get ("class") for v in root.iter ("view")}
        self.assertIn ("CAnimKnob", classes)
        self.assertIn ("COnOffButton", classes)
        self.assertIn ("CNinePartTiledBitmap", classes)
        self.assertIn ("CVerticalSwitch", classes)
        self.assertNotIn ("CKnob", classes)
        self.assertNotIn ("CTextButton", classes)
        knobs = [v for v in root.iter ("view") if v.get ("class") == "CAnimKnob"]
        for k in knobs:
            stem = k.get ("bitmap")
            self.assertTrue (stem.startswith (("knob_led", "rotary_")), stem)
            self.assertEqual (k.get ("height-of-one-image"), {"knob_led": "44",
                             "knob_led_l": "56", "knob_led_bipolar": "44",
                             "rotary_5": "46"}[stem])
        # Toggles are rubber LED buttons; menus and fields sit on OLED glass.
        buttons = {v.get ("bitmap") for v in root.iter ("view") if v.get ("control-tag") == "sync"}
        self.assertEqual (buttons, {"btn_led"})
        frames = {v.get ("bitmap") for v in root.iter ("view")
                  if v.get ("class") == "CNinePartTiledBitmap"}
        self.assertEqual (frames, {"header_panel", "panel_section", "oled"})
        # The header uses plain text; the product title strips its prefix.
        wordmarks = [v for v in root.iter ("view") if v.get ("title") == "KRAFTONIC"]
        self.assertEqual (len (wordmarks), 1)
        self.assertEqual (wordmarks[0].get ("class"), "CTextLabel")
        self.assertEqual (wordmarks[0].get ("size"), "98, 24")
        # No wordmark unless the panel asks for one.
        bare, _ = kp.build (demo (wordmark=None))
        self.assertNotIn ('title="KRAFTONIC"', bare)
        self.assertFalse (any (b.get ("name", "").startswith ("brand_")
                              for b in root.iter ("bitmap")))
        titles = [v.get ("title") for v in root.iter ("view")
                  if v.get ("class") == "CTextLabel" and v.get ("font") == "Title"]
        self.assertEqual (titles, ["KRAFTONIC", "DEMO"])

    def test_large_bipolar_knob_is_refused (self):
        rows = [[kp.Section ("ONE", [kp.Knob ("Big", "cutoff", size="large", bipolar=True),
                                     kp.Menu ("Mode", "mode"), kp.Toggle ("Sync", "sync"),
                                     kp.Value ("Steps", "steps"), kp.Knob ("Amt", "amount"),
                                     kp.Rotary ("R", "range", ("A", "B", "C", "D", "E")),
                                     kp.Select ("V", "voice", ("A", "B"))])]]
        with self.assertRaisesRegex (ValueError, "no bipolar kit filmstrip"):
            kp.build (demo (rows=rows))

    def test_checker_catches_bitmap_problems (self):
        import shutil
        text, _ = kp.build (demo ())
        root = ET.fromstring (text)
        knob = next (v for v in root.iter ("view") if v.get ("class") == "CAnimKnob")
        with tempfile.TemporaryDirectory () as tmp:
            assets = os.path.join (tmp, "assets")
            shutil.copytree (check_panel._kit_assets_dir (), assets)
            manifest = check_panel._load_manifest (assets)
            check = lambda: "\n".join (check_panel.check_tree (root, manifest, assets))
            # Undeclared bitmap.
            knob.set ("bitmap", "knob_missing")
            self.assertIn ("not declared", check ())
            # Wrong frame size (the demo declares knob_led: 44px frames).
            knob.set ("bitmap", "knob_led")
            knob.set ("height-of-one-image", "41")
            self.assertIn ("height-of-one-image 41 x sub-pixmaps", check ())
            # Missing 2x file (hidden in the temp copy; the tree is untouched).
            knob.set ("height-of-one-image", "44")
            os.remove (os.path.join (assets, "controls", "knobs", "knob_led#2x.png"))
            self.assertIn ("at 2x", check ())
        # Declared but not in the kit manifest.
        with self.assertRaises (ValueError):
            kp._Builder (demo ()).use_bitmap ("controls/knobs/knob_missing")

    def test_checker_catches_unknown_font (self):
        text, _ = kp.build (demo ())
        root = ET.fromstring (text)
        label = next (v for v in root.iter ("view") if v.get ("class") == "CTextLabel")
        label.set ("font", "NoSuchFont")
        self.assertIn ("unknown font", "\n".join (check_panel.check_tree (root)))

    def test_option_count_needs_a_kit_strip (self):
        rows = [[kp.Section ("ONE", [kp.Rotary ("R", "range", ("A", "B", "C"))])]]
        with self.assertRaisesRegex (ValueError, "not in the curated manifest"):
            kp.build (demo (rows=rows, hidden=["mode", "cutoff", "amount", "sync", "steps", "voice"]))

    def test_nine_part_frame_may_contain_its_control (self):
        text, _ = kp.build (demo ())
        root = ET.fromstring (text)
        # The generated menu frame contains its menu: no overlap is reported.
        self.assertEqual (check_panel.check_tree (root), [])
        # But a section frame shifted so it only partly covers its controls
        # is an overlap.
        section = next (v for v in root.find ("template")
                        if v.get ("class") == "CViewContainer")
        frame = next (v for v in section
                      if v.get ("class") == "CNinePartTiledBitmap")
        x, y = (float (p) for p in frame.get ("origin").split (","))
        w, h = (float (p) for p in frame.get ("size").split (","))
        frame.set ("origin", f"{x + 20}, {y}")
        frame.set ("size", f"{w - 20}, {h}")
        self.assertIn ("overlaps", "\n".join (check_panel.check_tree (root)))


if __name__ == "__main__":
    unittest.main (verbosity=2)
