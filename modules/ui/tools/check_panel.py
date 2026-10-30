#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Checks a Kraftonic panel .uidesc. Used by the
# "ui: <product> panel" tests (see kraftonic_add_panel_test in
# modules/ui/CMakeLists.txt):
#
#     check_panel.py <file.uidesc> [--params-header H [--enum ParamId]] [--generator make_x_uidesc.py]
#
# Checks:
#   - the file is valid XML with one template;
#   - every view lies inside its parent and overlaps none of its siblings
#     (a CNinePartTiledBitmap frame may contain the views on top of it);
#   - every label's text fits its box (Barlow width estimates);
#   - every colour, gradient, font and bitmap a view names is declared, tag
#     names and numbers are unique, and every declared tag is used;
#   - every bitmap is in the kit manifest, its files exist at 1x and 2x,
#     and height-of-one-image x sub-pixmaps matches the image;
#   - with --params-header: <name>.panel.json lists every ParamId of the enum
#     exactly once (unless marked hidden), with the right number, and the
#     .uidesc control-tags agree;
#   - with --generator: a fresh run of the generator reproduces the committed
#     .uidesc and .panel.json byte for byte.
# Prints every problem and exits 1 if there is any.

import argparse
import json
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
import kraftonic_panel as kp  # noqa: E402


def _pair (text, what):
    a, b = (float (v) for v in text.split (","))
    return a, b


def _rect (el):
    x, y = _pair (el.get ("origin", "0, 0"), "origin")
    w, h = _pair (el.get ("size", "0, 0"), "size")
    return x, y, w, h


def _name (el):
    title = el.get ("title")
    tag = el.get ("control-tag") or el.get ("sub-controller")
    bits = [el.get ("class", el.tag)]
    if title:
        bits.append (repr (title))
    if tag:
        bits.append (f"<{tag}>")
    x, y, w, h = _rect (el)
    bits.append (f"@{x:g},{y:g} {w:g}x{h:g}")
    return " ".join (bits)


def _overlap (a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return min (ax + aw, bx + bw) > max (ax, bx) and min (ay + ah, by + bh) > max (ay, by)


def _contains (a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return ax <= bx and ay <= by and ax + aw >= bx + bw and ay + ah >= by + bh


def _kit_assets_dir ():
    return os.path.normpath (os.path.join (
        os.path.dirname (os.path.abspath (__file__)), "..", "kit", "assets"))


def _load_manifest (assets_dir=None):
    path = os.path.join (assets_dir or _kit_assets_dir (), "manifest.json")
    with open (path, encoding="utf-8") as f:
        return {e["name"]: e for e in json.load (f)}


def check_tree (root, manifest=None, assets_dir=None):
    """Structural checks on a parsed .uidesc. Returns a list of problems.

    With `manifest` (the curated kit manifest), also checks every bitmap:
    declared, file present at both scales, and height-of-one-image x
    sub-pixmaps matching the image.
    """
    errors = []
    if root.tag != "vstgui-ui-description":
        return [f"root element is <{root.tag}>, not <vstgui-ui-description>"]
    colors = {c.get ("name") for c in root.iter ("color")}
    gradients = {g.get ("name") for g in root.iter ("gradient")}
    fonts = {f.get ("name") for f in root.iter ("font")}
    bitmaps = {}
    for b in root.iter ("bitmap"):
        if b.get ("name") in bitmaps:
            errors.append (f"bitmap '{b.get ('name')}' declared twice")
        bitmaps[b.get ("name")] = b
    if manifest is None:
        try:
            manifest = _load_manifest (assets_dir)
        except OSError as e:
            errors.append (f"cannot load kit manifest: {e}")
            manifest = {}
    tags = {}
    numbers = {}
    for t in root.iter ("control-tag"):
        name, num = t.get ("name"), t.get ("tag")
        if name in tags:
            errors.append (f"control-tag name '{name}' declared twice")
        if num in numbers:
            errors.append (f"control-tag number {num} used by '{numbers[num]}' and '{name}'")
        tags[name] = num
        numbers[num] = name
    templates = root.findall ("template")
    if len (templates) != 1:
        errors.append (f"expected one <template>, found {len (templates)} (one page per panel)")
        return errors
    used_tags = set ()

    def walk (el, path):
        _, _, pw, ph = _rect (el)
        kids = [k for k in el if k.tag == "view"]
        rects = []
        for k in kids:
            r = _rect (k)
            x, y, w, h = r
            where = f"{path} > {_name (k)}"
            if w <= 0 or h <= 0:
                errors.append (f"{where}: empty size")
            if x < 0 or y < 0 or x + w > pw or y + h > ph:
                errors.append (f"{where}: outside its parent ({pw:g}x{ph:g})")
            for attr, value in k.attrib.items ():
                if attr.endswith ("color") and not value.startswith ("#") and value not in colors:
                    errors.append (f"{where}: unknown colour '{value}' in {attr}")
                if attr.startswith ("gradient") and value not in gradients:
                    errors.append (f"{where}: unknown gradient '{value}' in {attr}")
            font = k.get ("font")
            if font is not None and font not in fonts:
                errors.append (f"{where}: unknown font '{font}'")
            bmp = k.get ("bitmap")
            if bmp is not None:
                if bmp not in bitmaps:
                    errors.append (f"{where}: bitmap '{bmp}' is not declared")
                elif bmp in manifest:
                    base = assets_dir or _kit_assets_dir ()
                    for scale, want2x in ((1, False), (2, True)):
                        rels = [r for r in manifest[bmp]["files"]
                                if ("#2x" in r) == want2x]
                        if not rels or not any (os.path.isfile (os.path.join (base, r))
                                                for r in rels):
                            errors.append (f"{where}: bitmap '{bmp}' file missing at {scale}x "
                                           f"(want one of {rels or manifest[bmp]['files']})")
                    h1 = k.get ("height-of-one-image")
                    n = k.get ("sub-pixmaps")
                    if h1 is not None and n is not None:
                        try:
                            if int (h1) * int (n) != manifest[bmp]["frame_h"] * manifest[bmp]["frames"]:
                                errors.append (
                                    f"{where}: bitmap '{bmp}' is {manifest[bmp]['frame_w']}x"
                                    f"{manifest[bmp]['frame_h'] * manifest[bmp]['frames']} "
                                    f"but height-of-one-image {h1} x sub-pixmaps {n}")
                        except ValueError:
                            errors.append (f"{where}: bitmap '{bmp}' has a non-numeric frame size")
                else:
                    errors.append (f"{where}: bitmap '{bmp}' is not in the kit manifest")
            tag = k.get ("control-tag")
            if tag is not None:
                used_tags.add (tag)
                if tag not in tags:
                    errors.append (f"{where}: control-tag '{tag}' is not declared")
            title = k.get ("title")
            if title and k.get ("class") in ("CTextLabel", "CTextButton"):
                need = kp.text_width (title, k.get ("font", "Label"))
                if need > w:
                    errors.append (f"{where}: text needs ~{need:.0f}px, box is {w:g}px")
                px = kp.FONT_PX.get (k.get ("font", "Label"), 12)
                if px > h + 1:
                    errors.append (f"{where}: {px}px font in a {h:g}px box")
            rects.append ((r, k))
            walk (k, where)
        for i in range (len (rects)):
            for j in range (i + 1, len (rects)):
                a, b = rects[i], rects[j]
                if _overlap (a[0], b[0]):
                    frame = (a[1].get ("class") == "CNinePartTiledBitmap" and _contains (a[0], b[0])
                             or b[1].get ("class") == "CNinePartTiledBitmap" and _contains (b[0], a[0]))
                    if not frame:
                        errors.append (f"{path}: {_name (a[1])} overlaps {_name (b[1])}")

    walk (templates[0], f"template '{templates[0].get ('name')}'")
    for name in sorted (set (tags) - used_tags):
        errors.append (f"control-tag '{name}' is declared but no view uses it")
    return errors


def check_mapping (root, mapping, params):
    """Every parameter on the panel exactly once, with the right numbers."""
    errors = []
    hidden = set (mapping.get ("hidden", []))
    count = {}
    for c in mapping["controls"]:
        count[c["tag"]] = count.get (c["tag"], 0) + 1
        if c["tag"] not in params:
            errors.append (f"control '{c['label']}' binds '{c['tag']}', which is not a parameter")
        elif c["param_id"] != params[c["tag"]]:
            errors.append (f"'{c['tag']}' mapped to {c['param_id']}, header says {params[c['tag']]}")
    for name in sorted (params):
        n = count.get (name, 0)
        if name in hidden:
            if n:
                errors.append (f"'{name}' is marked hidden but is on the panel")
        elif n != 1:
            errors.append (f"parameter '{name}' is on the panel {n} times (want exactly 1)")
    declared = {t.get ("name"): int (t.get ("tag")) for t in root.iter ("control-tag")}
    for name, num in params.items ():
        if name in declared and declared[name] != num:
            errors.append (f"uidesc control-tag '{name}' = {declared[name]}, header says {num}")
    for name in declared:
        if name not in params and name not in mapping.get ("extra_tags", {}):
            errors.append (f"uidesc control-tag '{name}' is neither a parameter nor a known extra tag")
    return errors


def check_generator (uidesc, generator):
    errors = []
    base = os.path.basename (uidesc)[: -len (".uidesc")]
    with tempfile.TemporaryDirectory () as tmp:
        # A product in its own repository finds the kit through KRAFTONIC_UI_TOOLS.
        env = dict (os.environ)
        env.setdefault ("KRAFTONIC_UI_TOOLS", os.path.dirname (os.path.abspath (__file__)))
        run = subprocess.run ([sys.executable, "-B", generator, "--out", tmp],
                              capture_output=True, text=True, env=env)
        if run.returncode != 0:
            return [f"generator failed: {run.stderr.strip () or run.stdout.strip ()}"]
        for suffix in (".uidesc", ".panel.json"):
            fresh = os.path.join (tmp, base + suffix)
            committed = os.path.join (os.path.dirname (uidesc), base + suffix)
            if not os.path.exists (fresh):
                errors.append (f"generator did not write {base + suffix}")
            elif not os.path.exists (committed):
                errors.append (f"{committed} is missing; run {generator}")
            else:
                with open (fresh, "rb") as a, open (committed, "rb") as b:
                    if a.read () != b.read ():
                        errors.append (f"{committed} is out of date; run python3 {generator}")
    return errors


def main (argv=None):
    ap = argparse.ArgumentParser (description="Check a Kraftonic panel .uidesc")
    ap.add_argument ("uidesc")
    ap.add_argument ("--params-header", help="C++ header with the product's ParamId enum")
    ap.add_argument ("--enum", default="ParamId")
    ap.add_argument ("--generator", help="product script that writes the .uidesc")
    args = ap.parse_args (argv)

    try:
        root = ET.parse (args.uidesc).getroot ()
    except (ET.ParseError, OSError) as e:
        print (f"FAIL {args.uidesc}: not valid XML: {e}")
        return 1
    errors = check_tree (root)
    controls = "?"
    if args.params_header:
        map_path = args.uidesc[: -len (".uidesc")] + ".panel.json"
        try:
            with open (map_path, encoding="utf-8") as f:
                mapping = json.load (f)
            params = kp.params_from_header (args.params_header, args.enum)
            errors += check_mapping (root, mapping, params)
            controls = len (mapping["controls"])
        except (OSError, ValueError, KeyError) as e:
            errors.append (f"mapping check: {e}")
    if args.generator:
        errors += check_generator (args.uidesc, args.generator)

    views = sum (1 for _ in root.iter ("view"))
    for e in errors:
        print (f"FAIL {e}")
    status = "FAIL" if errors else "PASS"
    print (f"{status}: {args.uidesc}: {views} views, {controls} bound controls, {len (errors)} problems")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit (main ())
