#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# kraftonic_panel: turns a declarative panel description into a VSTGUI
# .uidesc. Kit look (the instrument design):
# LED-collar CAnimKnob filmstrips, rotary selectors, LED selectors, rubber
# LED buttons, OLED-glass menus and fields, section panels with a header
# strip and a header bar with an optional text wordmark, all from
# modules/ui/kit/assets.
# Every Kraftonic product panel shares the THEME below, so all products look
# alike. See modules/ui/README.md for a how-to.
#
# A product script lists its sections and controls only:
#
#     sys.path.insert (0, str (REPO / "modules" / "ui" / "tools"))
#     import kraftonic_panel as kp
#     panel = kp.Panel (
#         title="KRAFTONIC DEMO",
#         params=kp.params_from_header (REPO / "plugins/demo/dsp/DemoParameters.h"),
#         header=kp.Header (preset_tag=1000, output="outputGain", sub_controller_prefix="Demo"),
#         rows=[[kp.Section ("FILTER", [kp.Knob ("Cutoff", "cutoff")])]],
#     )
#     kp.main (panel, HERE, "demo")
#
# Besides <name>.uidesc it writes <name>.panel.json, a mapping table
# (control -> ParamId) that check_panel.py uses to prove every parameter is
# on the panel exactly once.
#
# Python 3.9+, standard library only.

import argparse
import json
import math
import os
import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple
from xml.sax.saxutils import quoteattr

# ---------------------------------------------------------------------------
# Theme tokens: shared by every product. Change them here, never per product.
# ---------------------------------------------------------------------------

THEME = {
    # Named colours written into every .uidesc (VSTGUI rgba: #rrggbbaa).
    # The instrument design tokens.
    "colors": {
        "Chassis": "#121315ff",   # editor background
        "Panel": "#26282cff",     # section body (drawn by the panel_section bitmap)
        "Silk": "#e8e5deff",      # silkscreen: control labels, header text
        "Group": "#a9a79fff",     # group (section) labels, header labels
        "Value": "#b9b6aeff",     # knob value readouts
        "Led": "#ffb43cff",       # LED on: preset category
        "Oled": "#050506ff",      # OLED glass (drawn by the oled bitmap)
        "OledText": "#f4f5f7ff",  # OLED text: menus, value fields, preset name
        "OledDim": "#8e8e95ff",   # OLED secondary text: version
    },
    # Two-stop gradients (none used by the kit views; kept for products).
    "gradients": {},
    # Named VSTGUI fonts (declared in every .uidesc; the families ship in
    # kit/assets/fonts and fall back to system condensed/sans when missing).
    "fonts": {
        "title": "Title",      # Barlow Condensed 20: product name in the header
        "header": "Header",    # Barlow Condensed Bold 12: header labels
        "section": "Section",  # Barlow Condensed Bold 11: group labels
        "label": "Label",      # Barlow Condensed Bold 11: silkscreen control labels
        "value": "Value",      # Barlow Semi Condensed 12: knob value readouts
        "oled": "Oled",        # Barlow Semi Condensed 12: OLED menus and fields
    },
    # VSTGUI resolves the family name, plus bold, against the bundle's
    # registered TTFs; alternatives apply only when a family is missing.
    "font_defs": [
        ("Title", "Barlow Condensed", 20, True, False, "Arial Narrow, Helvetica Neue, Arial"),
        ("Header", "Barlow Condensed", 12, True, False, "Arial Narrow, Helvetica Neue, Arial"),
        ("Section", "Barlow Condensed", 11, True, False, "Arial Narrow, Helvetica Neue, Arial"),
        ("Label", "Barlow Condensed", 11, True, False, "Arial Narrow, Helvetica Neue, Arial"),
        ("Value", "Barlow Semi Condensed", 12, False, False, "Arial Narrow, Helvetica Neue, Arial"),
        ("Oled", "Barlow Semi Condensed", 12, False, False, "Arial Narrow, Helvetica Neue, Arial"),
    ],
    # Spacing (px): the design's 16 px outer padding and 12 px panel gap,
    # tightened to 12 / 10 so the widest panels stay laptop-sized.
    "spacing": {
        "margin": 12,     # editor edge to content
        "gap": 10,        # between panels (header and sections)
        "pad": 8,         # section edge to cells
        "title_h": 26,    # section header strip (baked into panel_section)
        "label_h": 13,    # silkscreen label above a control
        "value_h": 15,    # value readout below a knob
        "line_gap": 4,    # between lines of cells inside one section
        "header_h": 56,   # header bar (baked into header_panel)
        "menu_h": 21,     # OLED menu / value field height
        "row_h": 28,      # preset bar height
        "select_row": 16, # one LED selector option
    },
    # Knob filmstrip frame size and cell width per knob size.
    "knob": {"small": 44, "medium": 44, "large": 56},
    "cell_w": {"small": 52, "medium": 56, "large": 68},
    # StepGrid: width of the row-label column and the narrowest step column.
    "grid": {"label_w": 64, "col_w": 48},
    # Rotary selector: filmstrip size and the radius of its option labels.
    "rotary": {"size": 46, "radius": 36},
}

# Kit bitmaps by role. Paths are relative to modules/ui/kit/assets and are
# written into the .uidesc the same way, so the CMake helper can collect
# them. The knob map is size -> (normal filmstrip, bipolar filmstrip):
# small and medium share the 44 px strip, large uses the 56 px strip. No
# large bipolar strip exists (no product uses one), so large + bipolar is
# refused in _Builder.knob_filmstrip. Rotary and LED selector strips are
# per option count: rotary_<n>, led_select_<n>.
KIT_BITMAPS = {
    "knob": {
        "small": ("controls/knobs/knob_led", "controls/knobs/knob_led_bipolar"),
        "medium": ("controls/knobs/knob_led", "controls/knobs/knob_led_bipolar"),
        "large": ("controls/knobs/knob_led_l", None),
    },
    "rotary": "controls/switches/rotary_{n}",
    "select": "controls/switches/led_select_{n}",
    "toggle": "controls/buttons/btn_led",
    "led": "controls/buttons/led_toggle",
    "prev": "controls/buttons/btn_prev",
    "next": "controls/buttons/btn_next",
    "section": "materials/nine/panel_section",
    "oled": "materials/nine/oled",
    "topbar": "materials/nine/header_panel",
}

# Nominal pixel size of the kit's named fonts (THEME["font_defs"]). Used for
# text-fit estimates only.
FONT_PX = {
    "Title": 20,
    "Header": 12,
    "Section": 11,
    "Label": 11,
    "Value": 12,
    "Oled": 12,
}


def text_width (text: str, font: str) -> float:
    """Rough rendered width of `text` in a kit font.

    Advances are measured per glyph from the vendored TTFs (ratios of the
    point size): Barlow Condensed Bold for the condensed fonts and Barlow
    Semi Condensed SemiBold for values (each the widest weight a name can
    resolve to), with a 6% margin for kerning and rounding, so estimates err
    slightly wide and panels never overflow at runtime.
    """
    px = FONT_PX.get (font, 12)
    table = _SEMI_ADV if font in ("Value", "Oled") else _COND_ADV
    total = 0.0
    for ch in text:
        total += table.get (ch, 0.55 if ch.isupper () else 0.48)
    return total * 1.06 * px


# Measured advance ratios (PIL, 100 px em). Cover every glyph used in
# product labels; anything else falls back per case above.
_COND_ADV = {
    "A": 0.48, "B": 0.47, "C": 0.46, "D": 0.48, "E": 0.44, "F": 0.42, "G": 0.47, "H": 0.48,
    "I": 0.23, "J": 0.45, "K": 0.49, "L": 0.43, "M": 0.55, "N": 0.51, "O": 0.47, "P": 0.47,
    "Q": 0.46, "R": 0.47, "S": 0.44, "T": 0.47, "U": 0.48, "V": 0.49, "W": 0.69, "X": 0.48,
    "Y": 0.47, "Z": 0.41,
    "a": 0.45, "b": 0.45, "c": 0.43, "d": 0.45, "e": 0.44, "f": 0.30, "g": 0.44, "h": 0.45,
    "i": 0.22, "j": 0.22, "k": 0.45, "l": 0.21, "m": 0.68, "n": 0.45, "o": 0.44, "p": 0.45,
    "q": 0.45, "r": 0.32, "s": 0.41, "t": 0.29, "u": 0.45, "v": 0.44, "w": 0.61, "x": 0.44,
    "y": 0.42, "z": 0.37,
    "0": 0.45, "1": 0.28, "2": 0.44, "3": 0.44, "4": 0.48, "5": 0.44, "6": 0.44, "7": 0.41,
    "8": 0.44, "9": 0.44,
    " ": 0.20, ".": 0.22, ",": 0.21, ";": 0.23, ":": 0.28, "!": 0.28, "?": 0.44, "|": 0.19,
    "(": 0.31, ")": 0.31, "'": 0.17, '"': 0.35, "-": 0.33, "/": 0.42, "\\": 0.42, ">": 0.44,
    "<": 0.44, "+": 0.44, "=": 0.44, "*": 0.36, "#": 0.62, "%": 0.77, "@": 0.76, "&": 0.60,
    "~": 0.49, "_": 0.42, "[": 0.35, "]": 0.35, "\u2032": 0.45, "\u00b0": 0.36, "\u00b1": 0.44,
    "\u00b7": 0.23,
}
_SEMI_ADV = {
    "A": 0.55, "B": 0.54, "C": 0.53, "D": 0.55, "E": 0.51, "F": 0.49, "G": 0.54, "H": 0.56,
    "I": 0.24, "J": 0.51, "K": 0.55, "L": 0.49, "M": 0.63, "N": 0.59, "O": 0.54, "P": 0.53,
    "Q": 0.52, "R": 0.54, "S": 0.51, "T": 0.52, "U": 0.56, "V": 0.54, "W": 0.77, "X": 0.54,
    "Y": 0.53, "Z": 0.48,
    "a": 0.48, "b": 0.50, "c": 0.48, "d": 0.50, "e": 0.48, "f": 0.33, "g": 0.49, "h": 0.49,
    "i": 0.24, "j": 0.23, "k": 0.48, "l": 0.22, "m": 0.75, "n": 0.49, "o": 0.49, "p": 0.50,
    "q": 0.50, "r": 0.34, "s": 0.45, "t": 0.33, "u": 0.49, "v": 0.46, "w": 0.68, "x": 0.47,
    "y": 0.45, "z": 0.41,
    "0": 0.51, "1": 0.31, "2": 0.49, "3": 0.48, "4": 0.52, "5": 0.48, "6": 0.48, "7": 0.44,
    "8": 0.48, "9": 0.48,
    " ": 0.20, ".": 0.24, ",": 0.23, ";": 0.27, ":": 0.30, "!": 0.30, "?": 0.46, "|": 0.18,
    "(": 0.30, ")": 0.30, "'": 0.15, '"': 0.32, "-": 0.36, "/": 0.42, "\\": 0.42, ">": 0.47,
    "<": 0.47, "+": 0.47, "=": 0.47, "*": 0.38, "#": 0.64, "%": 0.80, "@": 0.79, "&": 0.64,
    "~": 0.50, "_": 0.46, "[": 0.37, "]": 0.37, "\u2032": 0.49, "\u00b0": 0.37, "\u00b1": 0.47,
    "\u00b7": 0.22,
}


# ---------------------------------------------------------------------------
# Panel description (data only).
# ---------------------------------------------------------------------------

@dataclass
class Control:
    label: str
    param: Optional[str] = None   # ParamId key (the enum name); None for SectionLabel
    span: int = 1                 # cells this control occupies
    size: str = "medium"          # small | medium | large (sets cell width/height)
    kind: str = ""


def Knob (label, param, size="small", bipolar=False, span=1):
    """Knob with a label above and an editable value readout below.
    bipolar=True draws the arc from the centre (for +/- amounts)."""
    c = Control (label, param, span, size, "knob")
    c.bipolar = bipolar
    return c


def Menu (label, param, size="medium", span=1):
    """Option menu (list parameters); entries come from the plug-in."""
    return Control (label, param, span, size, "menu")


def Toggle (label, param, title="On", size="medium", span=1):
    """On/off button for a two-step parameter."""
    c = Control (label, param, span, size, "toggle")
    c.title = title
    return c


def Value (label, param, size="medium", span=1):
    """Small editable text value without a knob (e.g. a number of steps)."""
    return Control (label, param, span, size, "value")


def Rotary (label, param, options, size="medium", span=1):
    """Rotary selector for a list parameter with a few short options (5 or 6;
    the kit ships rotary_5 and rotary_6). `options` are the silkscreen labels
    around it, in parameter order; the pointer and the lit tick show the
    current one."""
    c = Control (label, param, span, size, "rotary")
    c.options = tuple (options)
    return c


def Select (label, param, options, size="medium", span=1):
    """LED selector for a list parameter with 2 or 3 options (led_select_2,
    led_select_3): one LED per option, stacked, with its silkscreen label to
    the right. Click an LED to pick its option."""
    c = Control (label, param, span, size, "select")
    c.options = tuple (options)
    return c


def SectionLabel (text, size="medium", span=1):
    """A sub-heading cell inside a section (no parameter)."""
    return Control (text, None, span, size, "label")


@dataclass
class Section:
    title: str
    controls: List[Control]
    columns: Optional[int] = None   # cells per line; None = one line


@dataclass
class StepGrid (Section):
    """A compact table: one column per step, one line per control kind.
    `controls` is row-major (all steps of the first line, then the next line);
    `row_labels` names each line at the left; `steps` is the column count.
    Cells have no label of their own: only Menu and Toggle controls fit."""
    row_labels: Sequence[str] = ()
    steps: int = 0


def step_grid (title, row_labels, rows):
    """Builds a StepGrid from `rows`: one list of Menu/Toggle controls per line,
    all of the same length (the number of steps)."""
    steps = len (rows[0])
    if len (rows) != len (row_labels) or any (len (r) != steps for r in rows):
        raise ValueError (f"{title}: every step grid line needs {steps} cells and a label")
    return StepGrid (title, [c for r in rows for c in r], None, tuple (row_labels), steps)


@dataclass
class Header:
    preset_tag: Optional[int] = None          # program-change ParamId (preset menu)
    output: Optional[str] = None              # ParamId key of the output level knob
    output_label: str = "Output"
    sub_controller_prefix: Optional[str] = None  # e.g. "Mono" -> MonoPrev/MonoNext/...
    category: bool = True                     # <prefix>Category label after the preset bar
    version: bool = True                      # <prefix>Version label after the title


@dataclass
class Panel:
    title: str
    params: Dict[str, int]                    # ParamId key -> number
    rows: List[List[Section]]
    header: Header = field (default_factory=Header)
    width: Optional[int] = None               # fixed width; None = fit content
    hidden: Sequence[str] = ()                # params deliberately left off the panel
    theme: dict = field (default_factory=lambda: THEME)
    licence: Optional[str] = None             # header comment, e.g. "SPDX-License-Identifier: MIT"
    wordmark: Optional[str] = None            # text drawn left of the title, e.g. your brand; None = none


# ---------------------------------------------------------------------------
# Parameter table from the product's C++ contract header.
# ---------------------------------------------------------------------------

def params_from_header (path, enum="ParamId") -> Dict[str, int]:
    """Reads `enum class <enum> { a = 0, b, c = 5, count };` from a C++ header.
    Returns {name: number}, skipping a trailing `count` entry."""
    with open (path, encoding="utf-8") as f:
        text = f.read ()
    return params_from_text (text, enum)


def params_from_text (text: str, enum="ParamId") -> Dict[str, int]:
    text = re.sub (r"//[^\n]*", "", text)
    text = re.sub (r"/\*.*?\*/", "", text, flags=re.S)
    m = re.search (r"enum\s+class\s+" + re.escape (enum) + r"\b[^{]*\{(.*?)\}", text, re.S)
    if not m:
        raise ValueError (f"enum class {enum} not found")
    out: Dict[str, int] = {}
    nxt = 0
    for entry in m.group (1).split (","):
        entry = entry.strip ()
        if not entry:
            continue
        if "=" in entry:
            name, value = (s.strip () for s in entry.split ("=", 1))
            nxt = int (value, 0)
        else:
            name = entry
        if name != "count":
            if name in out:
                raise ValueError (f"duplicate enum entry {name}")
            out[name] = nxt
        nxt += 1
    return out


# ---------------------------------------------------------------------------
# Layout.
# ---------------------------------------------------------------------------

def _attrs (d):
    return " ".join (f"{k}={quoteattr (str (v))}" for k, v in d.items ())


def _overlap (a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return min (ax + aw, bx + bw) > max (ax, bx) and min (ay + ah, by + bh) > max (ay, by)


def _kit_manifest ():
    """The curated kit manifest (modules/ui/kit/assets/manifest.json)."""
    global _MANIFEST
    try:
        return _MANIFEST
    except NameError:
        path = os.path.normpath (os.path.join (
            os.path.dirname (os.path.abspath (__file__)), "..", "kit", "assets", "manifest.json"))
        with open (path, encoding="utf-8") as f:
            _MANIFEST = {e["name"]: e for e in json.load (f)}
        return _MANIFEST


class _Builder:
    def __init__ (self, panel: Panel):
        self.p = panel
        self.t = panel.theme
        self.sp = self.t["spacing"]
        self.colors = self.t["colors"]
        self.fonts = self.t["fonts"]
        self.mapping: List[dict] = []
        self.used_bitmaps: List[str] = []
        self.manifest = _kit_manifest ()

    def use_bitmap (self, stem: str) -> dict:
        """Registers a kit asset and returns its manifest entry."""
        name = stem.split ("/")[-1]
        if name not in self.manifest:
            raise ValueError (f"kit asset '{name}' is not in the curated manifest")
        if stem not in self.used_bitmaps:
            self.used_bitmaps.append (stem)
        return self.manifest[name]

    # -- geometry helpers ---------------------------------------------------
    def cell_w (self, c: Control):
        return self.t["cell_w"][c.size] * c.span

    def cell_min_w (self, c: Control):
        """A labelled cell is at least wide enough for its silkscreen label
        (plus 2 px), so only the cells that need it grow past cell_w."""
        w = self.cell_w (c)
        if c.kind in ("knob", "menu", "toggle", "value", "rotary", "select") and c.label:
            need = int (math.ceil (text_width (c.label.upper (), self.fonts["label"]))) + 2
            w = max (w, need)
        if c.kind == "rotary":
            x0, _, x1, _ = self.rotary_layout (c)[0]
            w = max (w, int (math.ceil (x1 - x0)) + 4)
        elif c.kind == "select":
            w = max (w, 14 + 4 + max (self.option_w (o) for o in c.options) + 4)
        return w

    def cell_h (self, c: Control):
        sp = self.sp
        top = sp["label_h"] + 2
        if c.kind == "rotary":
            _, y0, _, y1 = self.rotary_layout (c)[0]
            return top + int (math.ceil (y1 - y0))
        if c.kind == "select":
            return top + max (len (c.options) * sp["select_row"],
                              self.t["knob"][c.size] + 2 + sp["value_h"])
        return top + self.t["knob"][c.size] + 2 + sp["value_h"]

    def option_w (self, text):
        return int (math.ceil (text_width (text.upper (), self.fonts["label"]))) + 4

    def rotary_layout (self, c: Control):
        """Geometry of a rotary selector relative to its centre: (bbox, label
        rects). The option labels sit on a circle (radius pushed out until no
        label touches the knob) at the filmstrip's tick angles."""
        n = len (c.options)
        if n < 2:
            raise ValueError (f"rotary '{c.label}' needs at least 2 options")
        half = self.t["rotary"]["size"] / 2
        sweep = min (240, 60 * (n - 1))
        lh = self.sp["select_row"]
        radius = self.t["rotary"]["radius"]
        while True:
            rects = []
            for i, o in enumerate (c.options):
                a = math.radians (-sweep / 2 + sweep * i / (n - 1))
                w = self.option_w (o)
                lx = round (radius * math.sin (a) - w / 2)
                ly = round (-radius * math.cos (a) - lh / 2)
                rects.append ((lx, ly, w, lh))
            knob = (-half, -half, 2 * half, 2 * half)
            clash = any (_overlap (r, knob) for r in rects) or any (
                _overlap (rects[i], rects[j]) for i in range (n) for j in range (i + 1, n))
            if not clash:
                break
            radius += 1
        x0 = min ([-half] + [r[0] for r in rects])
        y0 = min ([-half] + [r[1] for r in rects])
        x1 = max ([half] + [r[0] + r[2] for r in rects])
        y1 = max ([half] + [r[1] + r[3] for r in rects])
        return (x0, y0, x1, y1), rects

    def lines (self, s: Section):
        cols = s.columns or len (s.controls)
        out, cur, used = [], [], 0
        for c in s.controls:
            if cur and used + c.span > cols:
                out.append (cur)
                cur, used = [], 0
            cur.append (c)
            used += c.span
        if cur:
            out.append (cur)
        return out

    def section_natural (self, s: Section) -> Tuple[int, int]:
        if isinstance (s, StepGrid):
            sp, g = self.sp, self.t["grid"]
            n = len (s.row_labels)
            h = sp["label_h"] + 2 + n * sp["menu_h"] + (n - 1) * sp["line_gap"]
            return (2 * sp["pad"] + g["label_w"] + s.steps * g["col_w"],
                    sp["title_h"] + h + sp["pad"])
        lines = self.lines (s)
        w = max (sum (self.cell_min_w (c) for c in line) for line in lines)
        h = sum (max (self.cell_h (c) for c in line) for line in lines)
        h += self.sp["line_gap"] * (len (lines) - 1)
        return 2 * self.sp["pad"] + w, self.sp["title_h"] + h + self.sp["pad"]

    # -- view dictionaries --------------------------------------------------
    def label (self, x, y, w, h, text, font=None, color="Silk", align="center", extra=None):
        d = {
            "class": "CTextLabel", "origin": f"{x}, {y}", "size": f"{w}, {h}", "title": text,
            "font": font or self.fonts["label"], "font-color": color, "text-alignment": align,
            "transparent": "true", "style-no-frame": "true", "mouse-enabled": "false",
            "font-antialias": "true",
        }
        if extra:
            d.update (extra)
        return d

    def knob_filmstrip (self, size, bipolar):
        """(stem, frame, frames) for a knob; refuses large + bipolar."""
        normal, bipolar_stem = KIT_BITMAPS["knob"][size]
        stem = bipolar_stem if bipolar else normal
        if stem is None:
            raise ValueError (f"no {'bipolar ' if bipolar else ''}kit filmstrip for size '{size}'")
        e = self.use_bitmap (stem)
        return stem, e["frame_w"], e["frames"]

    def knob (self, x, y, size, tag, bipolar):
        stem, frame, frames = self.knob_filmstrip (size, bipolar)
        k = self.t["knob"][size]
        assert frame == k, f"{stem}: manifest frame {frame} != theme knob {k}"
        return {
            "class": "CAnimKnob", "origin": f"{x}, {y}", "size": f"{k}, {k}",
            "bitmap": stem.split ("/")[-1],
            "height-of-one-image": str (frame), "sub-pixmaps": str (frames),
            "control-tag": tag,
            "transparent": "true", "mouse-enabled": "true",
            "mode": "free click", "zoom-factor": "10", "wheel-inc-value": "0.01",
            "min-value": "0", "max-value": "1",
        }

    def backdrop (self, x, y, w, h, role):
        """A nine-part frame behind a control. check_panel treats a
        CNinePartTiledBitmap that fully contains a sibling as its frame,
        not an overlap."""
        stem = KIT_BITMAPS[role]
        self.use_bitmap (stem)
        return {
            "class": "CNinePartTiledBitmap", "origin": f"{x}, {y}", "size": f"{w}, {h}",
            "bitmap": stem.split ("/")[-1],
            "transparent": "true", "mouse-enabled": "false",
        }

    def framed (self, x, y, w, h, role, view):
        """A control on top of its nine-part frame (two sibling views)."""
        return [self.backdrop (x, y, w, h, role), view]

    def value_edit (self, x, y, w, h, tag, align="center"):
        """A knob's value readout: plain text under the knob (click to type)."""
        return {
            "class": "CTextEdit", "origin": f"{x}, {y}", "size": f"{w}, {h}",
            "control-tag": tag, "font": self.fonts["value"], "font-color": "Value",
            "back-color": "Panel", "text-alignment": align,
            "style-no-frame": "true", "transparent": "true",
            "immediate-text-change": "false",
            "value-precision": "2", "font-antialias": "true", "mouse-enabled": "true",
            "min-value": "0", "max-value": "1",
        }

    def oled_edit (self, x, y, w, h, tag, align="center"):
        """A typed value on OLED glass (Value controls)."""
        return self.framed (x, y, w, h, "oled", {
            "class": "CTextEdit", "origin": f"{x + 2}, {y}", "size": f"{w - 4}, {h}",
            "control-tag": tag, "font": self.fonts["oled"], "font-color": "OledText",
            "back-color": "Oled", "text-alignment": align,
            "style-no-frame": "true", "transparent": "true",
            "immediate-text-change": "false",
            "value-precision": "2", "font-antialias": "true", "mouse-enabled": "true",
            "min-value": "0", "max-value": "1",
        })

    def menu (self, x, y, w, h, tag, font=None):
        """An option menu on OLED glass. Text spans nearly the full frame
        (inset 2) so long entries keep every point the cell offers."""
        return self.framed (x, y, w, h, "oled", {
            "class": "COptionMenu", "origin": f"{x + 2}, {y}", "size": f"{w - 4}, {h}",
            "control-tag": tag, "font": font or self.fonts["oled"], "font-color": "OledText",
            "back-color": "Oled", "text-alignment": "center",
            "menu-popup-style": "true", "style-no-frame": "true",
            "transparent": "true", "font-antialias": "true", "mouse-enabled": "true",
            "min-value": "0", "max-value": "1",
        })

    def filmstrip_button (self, x, y, w, h, tag, role):
        """An on/off filmstrip button centred in (x, y, w, h)."""
        e = self.use_bitmap (KIT_BITMAPS[role])
        bw, bh = e["frame_w"], e["frame_h"]
        return {
            "class": "COnOffButton",
            "origin": f"{x + (w - bw) // 2}, {y + (h - bh) // 2}",
            "size": f"{bw}, {bh}", "bitmap": KIT_BITMAPS[role].split ("/")[-1],
            "height-of-one-image": str (bh), "sub-pixmaps": str (e["frames"]),
            "control-tag": tag,
            "transparent": "true", "mouse-enabled": "true",
            "min-value": "0", "max-value": "1",
        }

    def toggle_button (self, x, y, w, h, tag):
        """Rubber cap button with its LED; the cell draws its label above."""
        return self.filmstrip_button (x, y, w, h, tag, "toggle")

    def rotary (self, c: Control, cx, top, cw, tag):
        """Rotary selector (CAnimKnob, one frame per option) with its option
        labels in silkscreen around it."""
        n = len (c.options)
        stem = KIT_BITMAPS["rotary"].format (n=n)
        e = self.use_bitmap (stem)
        (x0, y0, x1, _), rects = self.rotary_layout (c)
        mx = cx + cw // 2
        my = top - y0
        k = e["frame_w"]
        out = [{
            "class": "CAnimKnob", "origin": f"{mx - k // 2}, {my - k // 2}", "size": f"{k}, {k}",
            "bitmap": stem.split ("/")[-1],
            "height-of-one-image": str (e["frame_h"]), "sub-pixmaps": str (e["frames"]),
            "control-tag": tag,
            "transparent": "true", "mouse-enabled": "true",
            "mode": "free click", "zoom-factor": "10", "wheel-inc-value": str (round (1 / (n - 1), 4)),
            "min-value": "0", "max-value": "1",
        }]
        for (lx, ly, lw, lh), text in zip (rects, c.options):
            out.append (self.label (mx + lx, my + ly, lw, lh, text.upper (), color="Silk"))
        return out

    def select (self, c: Control, x, top, cw, tag):
        """LED selector (CVerticalSwitch over a column of LEDs) with each
        option's silkscreen label to the right of its LED."""
        n = len (c.options)
        stem = KIT_BITMAPS["select"].format (n=n)
        e = self.use_bitmap (stem)
        row = self.sp["select_row"]
        w = 14 + 4 + max (self.option_w (o) for o in c.options)
        x0 = x + (cw - w) // 2
        out = [{
            "class": "CVerticalSwitch", "origin": f"{x0}, {top}",
            "size": f"{e['frame_w']}, {e['frame_h']}", "bitmap": stem.split ("/")[-1],
            "height-of-one-image": str (e["frame_h"]), "sub-pixmaps": str (e["frames"]),
            "control-tag": tag,
            "transparent": "true", "mouse-enabled": "true",
            "min-value": "0", "max-value": "1",
        }]
        for i, text in enumerate (c.options):
            out.append (self.label (x0 + 18, top + i * row, w - 18, row, text.upper (),
                                    color="Silk", align="left"))
        return out

    def icon_button (self, x, y, role, sub):
        """A rubber icon button wired to a header sub-controller (no tag)."""
        stem = KIT_BITMAPS[role]
        e = self.use_bitmap (stem)
        bw, bh = e["frame_w"], e["frame_h"]
        return {
            "class": "COnOffButton", "origin": f"{x}, {y}", "size": f"{bw}, {bh}",
            "bitmap": stem.split ("/")[-1],
            "height-of-one-image": str (bh), "sub-pixmaps": str (e["frames"]),
            "sub-controller": sub,
            "transparent": "true", "mouse-enabled": "true",
            "min-value": "0", "max-value": "1",
        }

    def bind (self, c: Control, where: str, kind: str):
        if c.param not in self.p.params:
            raise ValueError (f"{where}: unknown parameter '{c.param}'")
        self.mapping.append ({
            "tag": c.param, "param_id": self.p.params[c.param], "kind": kind,
            "section": where, "label": c.label,
        })
        return c.param

    # -- one control in its cell -------------------------------------------
    def cell (self, c: Control, cx, cy, cw, where):
        sp = self.sp
        k = self.t["knob"][c.size]
        top = cy + sp["label_h"] + 2   # control row
        out = []
        if c.kind != "label":
            out.append (self.label (cx, cy, cw, sp["label_h"], c.label.upper (), color="Silk"))
        mh = sp["menu_h"]
        my = top + (k - mh) // 2
        if c.kind == "knob":
            tag = self.bind (c, where, "knob")
            out.append (self.knob (cx + (cw - k) // 2, top, c.size, tag, c.bipolar))
            out.append (self.value_edit (cx, top + k + 2, cw, sp["value_h"], tag))
        elif c.kind == "menu":
            out.extend (self.menu (cx + 2, my, cw - 4, mh, self.bind (c, where, "menu")))
        elif c.kind == "toggle":
            out.append (self.toggle_button (cx + 2, my, cw - 4, mh,
                                            self.bind (c, where, "toggle")))
        elif c.kind == "value":
            out.extend (self.oled_edit (cx + 2, my, cw - 4, mh,
                                        self.bind (c, where, "value")))
        elif c.kind == "rotary":
            out.extend (self.rotary (c, cx, top, cw, self.bind (c, where, "rotary")))
        elif c.kind == "select":
            out.extend (self.select (c, cx, top, cw, self.bind (c, where, "select")))
        elif c.kind == "label":
            out.append (self.label (cx, my, cw, mh, c.label.upper (), font=self.fonts["section"],
                                    color="Group"))
        else:
            raise ValueError (f"{where}: unknown control kind '{c.kind}'")
        return out

    def grid_section (self, s: StepGrid, w, views):
        sp, g = self.sp, self.t["grid"]
        inner = w - 2 * sp["pad"]
        col_w = (inner - g["label_w"]) // s.steps
        x0 = sp["pad"] + inner - col_w * s.steps   # the label column takes the rounding
        y = sp["title_h"] + 4
        for i in range (s.steps):
            views.append (self.label (x0 + i * col_w, y, col_w, sp["label_h"], str (i + 1),
                                      color="Group"))
        y += sp["label_h"] + 2
        for r, name in enumerate (s.row_labels):
            views.append (self.label (sp["pad"], y, x0 - sp["pad"], sp["menu_h"], name.upper (),
                                      color="Silk", align="left"))
            for i in range (s.steps):
                c = s.controls[r * s.steps + i]
                where = f"{s.title} {i + 1}"
                cx, cw = x0 + i * col_w + 1, col_w - 2
                if c.kind == "menu":
                    views.extend (self.menu (cx, y, cw, sp["menu_h"], self.bind (c, where, "menu")))
                elif c.kind == "toggle":
                    views.append (self.filmstrip_button (cx, y, cw, sp["menu_h"],
                                                         self.bind (c, where, "toggle"), "led"))
                else:
                    raise ValueError (f"{where}: a step grid holds menus and toggles, not '{c.kind}'")
            y += sp["menu_h"] + sp["line_gap"]

    def section (self, s: Section, x, y, w, h):
        """A section panel: the panel_section bitmap draws the body, edge and
        header strip; the group label sits in the strip."""
        sp = self.sp
        out = ["\t\t" + "<view " + _attrs ({
            "class": "CViewContainer", "origin": f"{x}, {y}", "size": f"{w}, {h}",
            "transparent": "true", "mouse-enabled": "true"}) + ">"]
        frame = self.backdrop (0, 0, w, h, "section")
        views = [frame,
                 self.label (sp["pad"] + 4, 6, w - 2 * sp["pad"] - 8, sp["title_h"] - 12,
                             s.title.upper (), font=self.fonts["section"], color="Group",
                             align="left")]
        inner = w - 2 * sp["pad"]
        cy = sp["title_h"] + 6
        if isinstance (s, StepGrid):
            self.grid_section (s, w, views)
        for line in ([] if isinstance (s, StepGrid) else self.lines (s)):
            units = sum (c.span for c in line)
            extra = inner - sum (self.cell_min_w (c) for c in line)
            per_unit = extra // units
            cx = sp["pad"]
            for i, c in enumerate (line):
                cw = self.cell_min_w (c) + per_unit * c.span
                if i == len (line) - 1:
                    cw = sp["pad"] + inner - cx   # last cell takes the rounding
                views.extend (self.cell (c, cx, cy, cw, s.title))
                cx += cw
            cy += max (self.cell_h (c) for c in line) + sp["line_gap"]
        out.extend ("\t\t\t<view " + _attrs (v) + "/>" for v in views)
        out.append ("\t\t</view>")
        return out

    # -- header ---------------------------------------------------------------
    def header (self, width):
        views, slack = self._header (width)
        if slack < 0:
            raise ValueError (f"header does not fit: {-slack}px short; widen the panel")
        return views

    def header_slack (self, width):
        """Spare px in the header at `width` (negative: it does not fit)."""
        saved = list (self.mapping)
        _, slack = self._header (width)
        self.mapping = saved
        return slack

    def display_title (self):
        """The product name as shown in the header: the Panel title without
        a leading wordmark (the wordmark already says it)."""
        title = self.p.title
        mark = self.p.wordmark
        if mark and title.startswith (mark + " "):
            title = title[len (mark) + 1:]
        return title

    def _header (self, width):
        """The header bar: a floating panel (header_panel) inside the outer
        margin with the wordmark, product name and version at the left, the
        preset bar (rubber prev/next around an OLED menu) and the output knob
        at the right."""
        sp, hd, f = self.sp, self.p.header, self.fonts
        m, hh, rh = sp["margin"], sp["header_h"], sp["row_h"]
        top = m
        views = [self.backdrop (m, top, width - 2 * m, hh, "topbar")]
        # Optional plain-text wordmark in the header font and silkscreen colour.
        x = m + 18
        if self.p.wordmark:
            mark_w = max (98, int (math.ceil (text_width (self.p.wordmark, f["title"]))) + 6)
            views.append (self.label (x, top + (hh - 24) // 2, mark_w, 24, self.p.wordmark,
                                      font=f["title"], color="Silk", align="left"))
            x += mark_w + 16
        title = self.display_title ()
        title_w = int (math.ceil (text_width (title, f["title"]))) + 6
        views.append (self.label (x, top + (hh - 26) // 2, title_w, 26, title,
                                  font=f["title"], color="Silk", align="left"))
        left_end = x + title_w
        pre = hd.sub_controller_prefix
        if hd.version and pre:
            views.append (self.label (left_end + 6, top + (hh - 26) // 2 + 8, 64, 14, "",
                                      font=f["value"], color="OledDim", align="left",
                                      extra={"sub-controller": pre + "Version"}))
            left_end += 6 + 64

        # Right to left: output level, then the preset bar.
        x = width - m - 14
        if hd.output:
            k = self.t["knob"]["small"]
            vw = 60
            ctl = Control (hd.output_label, hd.output, kind="knob")
            tag = self.bind (ctl, "HEADER", "knob")
            x -= vw
            views.append (self.value_edit (x, top + (hh - sp["value_h"]) // 2, vw, sp["value_h"],
                                           tag, align="left"))
            x -= k + 6
            views.append (self.knob (x, top + (hh - k) // 2, "small", tag, False))
            lw = int (math.ceil (text_width (hd.output_label.upper (), f["header"]))) + 6
            x -= lw + 6
            views.append (self.label (x, top + (hh - rh) // 2, lw, rh, hd.output_label.upper (),
                                      font=f["header"], color="Group", align="right"))
            x -= 24
        if hd.preset_tag is not None:
            y = top + (hh - rh) // 2
            if hd.category and pre:
                x -= 80
                views.append (self.label (x, y, 80, rh, "", font=f["header"], color="Led",
                                          align="left", extra={"sub-controller": pre + "Category"}))
                x -= 10
            mw = 240
            if pre:
                views.append (self.icon_button (x - 34, y, "next", pre + "Next"))
                x -= 34 + 6
            x -= mw
            views.extend (self.menu (x, y, mw, rh, "Preset"))
            if pre:
                views.append (self.icon_button (x - 6 - 34, y, "prev", pre + "Prev"))
                x -= 34 + 6
            x -= 6 + 52
            views.append (self.label (x, y, 52, rh, "PRESET", font=f["header"], color="Group",
                                      align="right"))
        return ["\t\t<view " + _attrs (v) + "/>" for v in views], x - (left_end + sp["gap"])

    # -- whole document -------------------------------------------------------
    def build (self):
        sp = self.sp
        m, gap = sp["margin"], sp["gap"]
        naturals = [[self.section_natural (s) for s in row] for row in self.p.rows]
        row_w = [sum (w for w, _ in row) + gap * (len (row) - 1) for row in naturals]
        content_w = max (row_w)
        if self.p.width is not None:
            if self.p.width - 2 * m < content_w:
                raise ValueError (f"width {self.p.width} < content {content_w + 2 * m}")
            content_w = self.p.width - 2 * m
        else:
            # An auto-sized panel is at least as wide as its header needs.
            content_w -= min (0, self.header_slack (content_w + 2 * m))
        width = content_w + 2 * m

        body = []
        y = m + sp["header_h"] + gap
        for row, nat in zip (self.p.rows, naturals):
            rh = max (h for _, h in nat)
            extra = content_w - (sum (w for w, _ in nat) + gap * (len (row) - 1))
            units = sum (sum (c.span for c in s.controls) for s in row)
            x = m
            for i, (s, (w, _)) in enumerate (zip (row, nat)):
                sw = w + extra * sum (c.span for c in s.controls) // units
                if i == len (row) - 1:
                    sw = m + content_w - x   # last section takes the rounding
                body.extend (self.section (s, x, y, sw, rh))
                x += sw + gap
            y += rh + gap
        height = y - gap + m
        body = self.header (width) + body

        self.check_mapping ()
        tags = {e["tag"]: e["param_id"] for e in self.mapping}
        if self.p.header.preset_tag is not None:
            tags["Preset"] = self.p.header.preset_tag

        out = ['<?xml version="1.0" encoding="UTF-8"?>']
        if self.p.licence:
            out.append (f"<!-- {self.p.licence} -->")
        out += [f"<!-- {self.p.title} editor. GENERATED by modules/ui/tools/kraftonic_panel.py",
               "     from the product's make_*_uidesc.py: edit the script, not this file. -->",
               '<vstgui-ui-description version="1">',
               "\t<bitmaps>"]
        for stem in self.used_bitmaps:
            # Flat basename paths: smtg_target_add_plugin_resources copies by
            # file name only, so every bundle holds the PNGs next to the
            # .uidesc and VSTGUI picks up name#2x.png automatically.
            # (Curated asset names are unique across folders.)
            e = self.manifest[stem.split ("/")[-1]]
            attrs = f'name="{stem.split ("/")[-1]}" path="{stem.split ("/")[-1]}.png"'
            if e.get ("nine"):
                attrs += ' nineparttiled-offsets="' + ", ".join (str (o) for o in e["nine"]) + '"'
            out.append (f"\t\t<bitmap {attrs}/>")
        out.append ("\t</bitmaps>")
        out.append ("\t<fonts>")
        for name, family, size, bold, italic, alt in self.t["font_defs"]:
            extra = (" bold=\"true\"" if bold else "") + (" italic=\"true\"" if italic else "")
            extra += f' alternative-font-names="{alt}"'
            out.append (f'\t\t<font name="{name}" font-name="{family}" size="{size}"{extra}/>')
        out.append ("\t</fonts>")
        out.append ("\t<colors>")
        for name, rgba in self.colors.items ():
            out.append (f'\t\t<color name="{name}" rgba="{rgba}"/>')
        out.append ("\t</colors>")
        out.append ("\t<gradients>")
        for name, (top, bottom) in self.t["gradients"].items ():
            out.append (f'\t\t<gradient name="{name}">')
            out.append (f'\t\t\t<color-stop rgba="{top}" start="0"/>')
            out.append (f'\t\t\t<color-stop rgba="{bottom}" start="1"/>')
            out.append ("\t\t</gradient>")
        out.append ("\t</gradients>")
        out.append ("\t<control-tags>")
        for name, tag in sorted (tags.items (), key=lambda kv: kv[1]):
            out.append (f'\t\t<control-tag name="{name}" tag="{tag}"/>')
        out.append ("\t</control-tags>")
        out.append ("\t<template " + _attrs ({
            "name": "view", "class": "CViewContainer", "origin": "0, 0",
            "size": f"{width}, {height}", "minSize": f"{width}, {height}",
            "maxSize": f"{width}, {height}", "background-color": "Chassis",
            "background-color-draw-style": "filled", "transparent": "false",
            "mouse-enabled": "true"}) + ">")
        out.extend (body)
        out.append ("\t</template>")
        out.append ("\t<custom>")
        out.append ('\t\t<attributes name="UIAttributesController"/>')
        out.append ('\t\t<attributes SelectedTemplate="view" name="UITemplateController"/>')
        out.append ("\t</custom>")
        out.append ("</vstgui-ui-description>")
        mapping = {
            "title": self.p.title, "size": [width, height],
            "params": self.p.params, "hidden": list (self.p.hidden),
            "controls": self.mapping, "extra_tags": {k: v for k, v in tags.items ()
                                                     if k not in self.p.params},
        }
        return "\n".join (out) + "\n", mapping

    def check_mapping (self):
        seen: Dict[str, int] = {}
        for e in self.mapping:
            seen[e["tag"]] = seen.get (e["tag"], 0) + 1
        dup = sorted (k for k, n in seen.items () if n > 1)
        missing = sorted (set (self.p.params) - set (seen) - set (self.p.hidden))
        if dup:
            raise ValueError (f"parameters on the panel more than once: {dup}")
        if missing:
            raise ValueError (f"parameters missing from the panel: {missing}")


def build (panel: Panel):
    """Returns (uidesc_text, mapping_dict)."""
    return _Builder (panel).build ()


def write (panel: Panel, out_dir, name):
    """Writes <name>.uidesc and <name>.panel.json into out_dir."""
    text, mapping = build (panel)
    uidesc = os.path.join (out_dir, name + ".uidesc")
    with open (uidesc, "w", encoding="utf-8", newline="\n") as f:
        f.write (text)
    with open (os.path.join (out_dir, name + ".panel.json"), "w", encoding="utf-8",
              newline="\n") as f:
        json.dump (mapping, f, indent=1, sort_keys=True)
        f.write ("\n")
    return uidesc, mapping


def main (panel: Panel, default_dir, name):
    """Command-line entry for product scripts: [--out DIR]."""
    ap = argparse.ArgumentParser (description=f"Generate {name}.uidesc")
    ap.add_argument ("--out", default=default_dir, help="output folder (default: next to the script)")
    args = ap.parse_args ()
    path, mapping = write (panel, args.out, name)
    w, h = mapping["size"]
    print (f"wrote {path} ({w}x{h}, {len (mapping['controls'])} controls, "
           f"{len (panel.params)} parameters)")
