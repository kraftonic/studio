#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Renders the Kraftonic instrument kit: every control
# filmstrip and panel material in modules/ui/kit/assets, at 1x and 2x, plus
# assets/manifest.json. Original artwork drawn from the kit tokens below
# Header wordmarks are text in the panel generators.
#
#     python3 modules/ui/kit/generator/render_kit.py [--out DIR]
#
# Developer step, never a build step. Needs Python 3.9+, numpy and Pillow.
# Shapes are signed-distance fields sampled 4x per pixel and box-filtered,
# so the output is deterministic for a given numpy/Pillow.

import argparse
import json
import math
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname (os.path.abspath (__file__))
ASSETS = os.path.normpath (os.path.join (HERE, "..", "assets"))
SS = 4  # samples per pixel and axis

# Kit tokens (the instrument design). kraftonic_panel.THEME mirrors the
# colours the .uidesc needs.
T = {
    "panel": "#26282C", "strip": "#1D1E21", "edge": "#36383D", "shade": "#141517",
    "silk": "#E8E5DE", "led_on": "#FFB43C", "led_off": "#26221C", "collar_off": "#3A352C",
    "oled": "#050506", "rubber_hi": "#4A4C52", "rubber_mid": "#33353A", "rubber_lo": "#2B2D31",
    "tick": "#7D7B75", "pointer": "#F4F1EA",
    "cap": [(0.0, "#4A4C52"), (0.55, "#202125"), (1.0, "#0E0E10")],
    "face": [(0.0, "#34363B"), (1.0, "#17181B")],
    "header": [(0.0, "#2B2D31"), (1.0, "#232529")],
}
KNOB_FRAMES = 128
COLLAR_LEDS = 13


def rgba (hex_, a=1.0):
    h = hex_.lstrip ("#")
    return np.array ([int (h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)] + [a])


def lerp_stops (stops, t):
    """Colour along gradient `stops` [(pos, hex)] at t (array) -> (..., 4)."""
    t = np.clip (t, 0.0, 1.0)
    pos = [p for p, _ in stops]
    cols = np.stack ([rgba (c) for _, c in stops])
    out = np.empty (t.shape + (4,))
    for ch in range (4):
        out[..., ch] = np.interp (t, pos, cols[:, ch])
    return out


class Canvas:
    """Premultiplied RGBA, supersampled. Coordinates are output pixels."""

    def __init__ (self, w, h):
        self.w, self.h = w, h
        self.px = np.zeros ((h * SS, w * SS, 4))
        ys, xs = np.mgrid[0:h * SS, 0:w * SS]
        self.x = (xs + 0.5) / SS
        self.y = (ys + 0.5) / SS

    def paint (self, sdf, colour, alpha=1.0):
        """Fills where sdf < 0 (sdf in output px). colour: rgba or (..., 4)."""
        cov = np.clip (0.5 - sdf * SS, 0.0, 1.0) * alpha
        col = colour if np.ndim (colour) == 3 else np.broadcast_to (colour, self.px.shape)
        a = col[..., 3] * cov
        self.px[..., :3] = col[..., :3] * a[..., None] + self.px[..., :3] * (1 - a[..., None])
        self.px[..., 3] = a + self.px[..., 3] * (1 - a)

    # -- signed distance fields --------------------------------------------
    def circle (self, cx, cy, r):
        return np.hypot (self.x - cx, self.y - cy) - r

    def ring (self, cx, cy, r, width):
        return np.abs (np.hypot (self.x - cx, self.y - cy) - r) - width / 2

    def dashed_ring (self, cx, cy, r, width, dash):
        """A ring stroked with equal dashes of arc length `dash`."""
        d = self.ring (cx, cy, r, width)
        ang = np.arctan2 (self.y - cy, self.x - cx)
        n = max (2, int (round (2 * math.pi * r / (2 * dash))))
        phase = (ang / (2 * math.pi) * n) % 1.0
        gap = (np.abs (phase - 0.25) - 0.25) * 2 * math.pi * r / n  # <0 inside the dash
        return np.maximum (d, gap)

    def segment (self, x1, y1, x2, y2, width):
        px, py = self.x - x1, self.y - y1
        dx, dy = x2 - x1, y2 - y1
        t = np.clip ((px * dx + py * dy) / (dx * dx + dy * dy), 0.0, 1.0)
        return np.hypot (px - t * dx, py - t * dy) - width / 2

    def rrect (self, x, y, w, h, r):
        cx, cy = x + w / 2, y + h / 2
        qx = np.abs (self.x - cx) - (w / 2 - r)
        qy = np.abs (self.y - cy) - (h / 2 - r)
        return np.hypot (np.maximum (qx, 0), np.maximum (qy, 0)) + np.minimum (np.maximum (qx, qy), 0) - r

    def vgrad (self, y0, y1, stops):
        return lerp_stops (stops, (self.y - y0) / max (1e-6, y1 - y0))

    def image (self):
        h, w = self.h, self.w
        p = self.px.reshape (h, SS, w, SS, 4).mean (axis=(1, 3))
        a = p[..., 3:4]
        rgb = np.where (a > 1e-6, p[..., :3] / np.maximum (a, 1e-6), 0.0)
        out = np.concatenate ([rgb, a], axis=-1)
        return Image.fromarray (np.round (np.clip (out, 0, 1) * 255).astype (np.uint8))


# ---------------------------------------------------------------------------
# Controls.
# ---------------------------------------------------------------------------

def led (c, cx, cy, d, on):
    """The 7 px amber LED (diameter d) with its glow when lit."""
    r = d / 2
    if on:
        dist = np.hypot (c.x - cx, c.y - cy)
        glow = np.clip (1 - (dist - r) / (r * 1.6), 0, 1) ** 2 * 0.55
        col = np.broadcast_to (rgba ("#FFAA32"), c.px.shape).copy ()
        col[..., 3] = glow
        c.paint (dist - r * 2.6, col)
        c.paint (c.circle (cx, cy, r), rgba (T["led_on"]))
        c.paint (c.circle (cx - r * 0.25, cy - r * 0.3, r * 0.35), rgba ("#FFE2A8"), 0.8)
    else:
        c.paint (c.circle (cx, cy, r + 0.5), rgba ("#000000"), 0.45)
        c.paint (c.circle (cx, cy, r), rgba (T["led_off"]))


def knob_frame (size, v, bipolar):
    """One frame of the LED-collar knob: dark cap with knurled edge, white
    pointer, 13 amber LEDs over 270 degrees (from the centre when bipolar)."""
    c = Canvas (size, size)
    u = size / 100.0          # the design's 100-unit view box
    m = size / 2

    def P (a, r):
        t = math.radians (a)
        return m + r * u * math.sin (t), m - r * u * math.cos (t)

    c.paint (c.circle (m, m, 49 * u), rgba ("#141517"))
    c.paint (c.ring (m, m, 48.5 * u, max (1 * u, 0.5)), rgba ("#FFFFFF"), 0.07)
    for i in range (COLLAR_LEDS):
        t = i / (COLLAR_LEDS - 1)
        x, y = P (-135 + 270 * t, 42.5)
        lit = (min (0.5, v) - 0.001 <= t <= max (0.5, v) + 0.001) if bipolar else t <= v + 0.001
        if lit:
            c.paint (c.circle (x, y, 5 * u), rgba ("#FFB03A"), 0.28)
            c.paint (c.circle (x, y, 2.6 * u), rgba (T["led_on"]))
        else:
            c.paint (c.circle (x, y, 2.4 * u), rgba (T["collar_off"]))
    c.paint (c.circle (m, m + 3 * u, 34 * u), rgba ("#000000"), 0.65)
    # Cap: radial gradient centred high (cx 50%, cy 28%, r 80% of its box).
    gx, gy, gr = m, m - 33.5 * u + 0.28 * 67 * u, 0.8 * 67 * u
    cap = lerp_stops (T["cap"], np.hypot (c.x - gx, c.y - gy) / gr)
    c.paint (c.circle (m, m, 33.5 * u), cap)
    c.paint (c.dashed_ring (m, m, 33 * u, 2.6 * u, 1.7 * u), rgba ("#070708"))
    c.paint (c.circle (m, m, 26.5 * u), c.vgrad (m - 26.5 * u, m + 26.5 * u, T["face"]))
    c.paint (c.ring (m, m, 26.5 * u, max (1 * u, 0.5)), rgba ("#FFFFFF"), 0.08)
    a = -135 + 270 * v
    x1, y1 = P (a, 10)
    x2, y2 = P (a, 24)
    c.paint (c.segment (x1, y1, x2, y2, 4.5 * u), rgba (T["pointer"]))
    return c.image ()


def rotary_frame (size, n, i):
    """One position of the rotary selector: cap, knurl, face, pointer and
    position ticks over sweep = min (240, 60 * (n - 1)) degrees."""
    c = Canvas (size, size)
    s = size / 46.0
    m = size / 2
    k = 15 * s
    sweep = min (240, 60 * (n - 1))

    def ang (j):
        return -sweep / 2 + sweep * j / (n - 1) if n > 1 else 0

    def P (a, r):
        t = math.radians (a)
        return m + r * math.sin (t), m - r * math.cos (t)

    for j in range (n):
        x1, y1 = P (ang (j), k + 3 * s)
        x2, y2 = P (ang (j), k + 7 * s)
        c.paint (c.segment (x1, y1, x2, y2, 1.5 * s), rgba (T["led_on"] if j == i else T["tick"]))
    c.paint (c.circle (m, m + 2 * s, k + 1.5 * s), rgba ("#000000"), 0.6)
    gx, gy, gr = m, m - k + 0.28 * 2 * k, 0.8 * 2 * k
    c.paint (c.circle (m, m, k), lerp_stops (T["cap"], np.hypot (c.x - gx, c.y - gy) / gr))
    c.paint (c.dashed_ring (m, m, k - 0.5 * s, 2 * s, 1.4 * s), rgba ("#070708"))
    kf = k * 0.78
    c.paint (c.circle (m, m, kf), c.vgrad (m - kf, m + kf, T["face"]))
    c.paint (c.ring (m, m, kf, max (s, 0.5)), rgba ("#FFFFFF"), 0.08)
    x1, y1 = P (ang (i), k * 0.25)
    x2, y2 = P (ang (i), k * 0.72)
    c.paint (c.segment (x1, y1, x2, y2, 3 * s), rgba (T["pointer"]))
    return c.image ()


def rubber (c, x, y, w, h, r, pressed):
    """A rubber cap: graded body, dark rim, top highlight, drop shadow."""
    if not pressed:
        c.paint (c.rrect (x, y + 2, w, h, r), rgba ("#000000"), 0.55)
    dy = 1 if pressed else 0
    c.paint (c.rrect (x, y + dy, w, h, r), rgba (T["shade"]))
    stops = ([(0.0, "#3F4146"), (0.55, "#2E3034"), (1.0, "#27292D")] if pressed else
             [(0.0, T["rubber_hi"]), (0.55, T["rubber_mid"]), (1.0, T["rubber_lo"])])
    c.paint (c.rrect (x + 1, y + 1 + dy, w - 2, h - 2, max (0.5, r - 1)), c.vgrad (y + dy, y + h + dy, stops))
    hi = np.maximum (c.rrect (x + 1, y + 1 + dy, w - 2, h - 2, max (0.5, r - 1)),
                     -(c.rrect (x + 1, y + 2 + dy, w - 2, h - 2, max (0.5, r - 1))))
    c.paint (hi, rgba ("#FFFFFF"), 0.14)


def led_button (scale, on):
    """Rubber cap button with its LED to the right (30x17 cap, 7 px LED)."""
    w, h = 46, 22
    c = Canvas (w * scale, h * scale)
    rubber (c, 1 * scale, 1 * scale, 30 * scale, 17 * scale, 3 * scale, on)
    led (c, 41.5 * scale, 9.5 * scale, 7 * scale, on)
    return c.image ()


def led_only (scale, on):
    c = Canvas (14 * scale, 14 * scale)
    led (c, 7 * scale, 7 * scale, 7 * scale, on)
    return c.image ()


def led_select (scale, n, i):
    """A column of n LEDs, 16 px apart; frame i lights LED i."""
    c = Canvas (14 * scale, 16 * n * scale)
    for j in range (n):
        led (c, 7 * scale, (8 + 16 * j) * scale, 7 * scale, j == i)
    return c.image ()


def chevron_button (scale, direction, pressed):
    """34x28 rubber button with a chevron (prev/next)."""
    c = Canvas (34 * scale, 28 * scale)
    rubber (c, 0, 0, 34 * scale, 26 * scale, 4 * scale, pressed)
    cx, cy = 17 * scale, (13 + (1 if pressed else 0)) * scale
    sx = -1 if direction == "prev" else 1
    a = (cx - sx * 1.75 * scale, cy - 3.5 * scale)
    b = (cx + sx * 1.75 * scale, cy)
    d = (cx - sx * 1.75 * scale, cy + 3.5 * scale)
    sdf = np.minimum (c.segment (*a, *b, 1.5 * scale), c.segment (*b, *d, 1.5 * scale))
    c.paint (sdf, rgba (T["silk"]))
    return c.image ()


# ---------------------------------------------------------------------------
# Materials (nine-part).
# ---------------------------------------------------------------------------

def panel_section (scale):
    """Section panel: #26282C body, 1 px edge, radius 10, 26 px header strip."""
    w, h, strip = 48, 52, 26
    c = Canvas (w * scale, h * scale)
    s = scale
    c.paint (c.rrect (0, 0, w * s, h * s, 10 * s), rgba (T["edge"]))
    inner = c.rrect (1 * s, 1 * s, (w - 2) * s, (h - 2) * s, 9 * s)
    c.paint (inner, rgba (T["panel"]))
    top = np.maximum (inner, c.y - (strip - 1) * s)
    c.paint (top, rgba (T["strip"]))
    line = np.maximum (inner, np.abs (c.y - (strip - 0.5) * s) - 0.5 * s)
    c.paint (line, rgba (T["shade"]))
    glint = np.maximum (inner, np.abs (c.y - (strip + 0.5) * s) - 0.5 * s)
    c.paint (glint, rgba ("#FFFFFF"), 0.04)
    hi = np.maximum (inner, np.abs (c.y - 1.5 * s) - 0.5 * s)
    c.paint (hi, rgba ("#FFFFFF"), 0.05)
    return c.image ()


def header_panel (scale):
    """Header bar: graded panel, 1 px edge, radius 10 (56 px tall; the
    nine-part offsets leave one middle row, so the grade never tiles)."""
    w, h = 48, 56
    c = Canvas (w * scale, h * scale)
    s = scale
    c.paint (c.rrect (0, 0, w * s, h * s, 10 * s), rgba (T["edge"]))
    inner = c.rrect (1 * s, 1 * s, (w - 2) * s, (h - 2) * s, 9 * s)
    c.paint (inner, c.vgrad (0, h * s, T["header"]))
    hi = np.maximum (inner, np.abs (c.y - 1.5 * s) - 0.5 * s)
    c.paint (hi, rgba ("#FFFFFF"), 0.06)
    return c.image ()


def oled (scale):
    """Black OLED glass: #050506, black rim, faint lower and top glints."""
    w = h = 16
    c = Canvas (w * scale, h * scale)
    s = scale
    c.paint (c.rrect (0, 0.5 * s, w * s, (h - 0.5) * s, 5 * s), rgba ("#FFFFFF"), 0.07)
    c.paint (c.rrect (0, 0, w * s, (h - 1) * s, 5 * s), rgba ("#000000"))
    inner = c.rrect (1 * s, 1 * s, (w - 2) * s, (h - 3) * s, 4 * s)
    c.paint (inner, c.vgrad (s, (h - 2) * s, [(0.0, "#0C0C0E"), (1.0, T["oled"])]))
    hi = np.maximum (inner, np.abs (c.y - 1.5 * s) - 0.5 * s)
    c.paint (hi, rgba ("#FFFFFF"), 0.06)
    return c.image ()


# ---------------------------------------------------------------------------
# Catalogue.
# ---------------------------------------------------------------------------

def strip (frames):
    w = frames[0].width
    out = Image.new ("RGBA", (w, sum (f.height for f in frames)), (0, 0, 0, 0))
    y = 0
    for f in frames:
        out.paste (f, (0, y))
        y += f.height
    return out


def catalogue ():
    """(name, folder, kind, vstgui, frame_w, frame_h, frames, nine, note, render(scale))."""
    out = []

    def knob (name, size, bipolar, note):
        out.append ((name, "controls/knobs", "filmstrip", "CAnimKnob", size, size, KNOB_FRAMES, None,
                     note, lambda s: strip ([knob_frame (size * s, f / (KNOB_FRAMES - 1), bipolar)
                                             for f in range (KNOB_FRAMES)])))

    knob ("knob_led", 44, False, "44 px dark knob, knurled edge, white pointer, 13-LED amber collar.")
    knob ("knob_led_bipolar", 44, True, "44 px LED-collar knob lighting from the centre (+/- amounts).")
    knob ("knob_led_l", 56, False, "56 px LED-collar knob (the most-played control of a panel).")
    for n in (5, 6):
        out.append ((f"rotary_{n}", "controls/switches", "filmstrip", "CAnimKnob", 46, 46, n, None,
                     f"{n}-position rotary selector; frame i points at position i.",
                     lambda s, n=n: strip ([rotary_frame (46 * s, n, i) for i in range (n)])))
    for n in (2, 3):
        out.append ((f"led_select_{n}", "controls/switches", "filmstrip", "CVerticalSwitch", 14, 16 * n, n,
                     None, f"{n} stacked LEDs 16 px apart; frame i lights LED i.",
                     lambda s, n=n: strip ([led_select (s, n, i) for i in range (n)])))
    out.append (("btn_led", "controls/buttons", "filmstrip", "COnOffButton", 46, 22, 2, None,
                 "Rubber cap button with LED: frame 0 off, frame 1 on.",
                 lambda s: strip ([led_button (s, False), led_button (s, True)])))
    out.append (("led_toggle", "controls/buttons", "filmstrip", "COnOffButton", 14, 14, 2, None,
                 "LED-only toggle: frame 0 off, frame 1 on.",
                 lambda s: strip ([led_only (s, False), led_only (s, True)])))
    for d in ("prev", "next"):
        out.append ((f"btn_{d}", "controls/buttons", "filmstrip", "COnOffButton", 34, 28, 2, None,
                     f"Rubber {d} button: frame 0 up, frame 1 down.",
                     lambda s, d=d: strip ([chevron_button (s, d, False), chevron_button (s, d, True)])))
    out.append (("panel_section", "materials/nine", "nine-part", "CNinePartTiledBitmap", 48, 52, 1,
                 [12, 28, 12, 12], "Section panel with its 26 px header strip.", panel_section))
    out.append (("header_panel", "materials/nine", "nine-part", "CNinePartTiledBitmap", 48, 56, 1,
                 [12, 27, 12, 28], "Header bar (56 px tall, stretches horizontally).", header_panel))
    out.append (("oled", "materials/nine", "nine-part", "CNinePartTiledBitmap", 16, 16, 1,
                 [6, 6, 6, 6], "OLED glass behind menus and value fields.", oled))
    return out


def main ():
    ap = argparse.ArgumentParser (description="Render the Kraftonic kit assets")
    ap.add_argument ("--out", default=ASSETS)
    args = ap.parse_args ()
    manifest = []
    for name, folder, kind, vstgui, fw, fh, frames, nine, note, render in catalogue ():
        files = []
        os.makedirs (os.path.join (args.out, folder), exist_ok=True)
        for scale, suffix in ((1, ""), (2, "#2x")):
            img = render (scale)
            assert img.size == (fw * scale, fh * frames * scale), (name, img.size)
            rel = f"{folder}/{name}{suffix}.png"
            img.save (os.path.join (args.out, rel), optimize=True)
            files.append (rel)
        entry = {"name": name, "folder": folder, "type": kind, "vstgui": vstgui,
                 "frame_w": fw, "frame_h": fh, "frames": frames, "files": files, "note": note}
        if nine:
            entry["nine"] = nine
        manifest.append (entry)
        print (f"wrote {name} ({fw}x{fh} x{frames})")
    path = os.path.join (args.out, "manifest.json")
    with open (path, "w", encoding="utf-8", newline="\n") as f:
        json.dump (manifest, f, indent=1)
        f.write ("\n")


if __name__ == "__main__":
    main ()
