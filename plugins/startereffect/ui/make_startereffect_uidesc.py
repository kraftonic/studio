#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Generates startereffect.uidesc and startereffect.panel.json from this
# description with the shared Kraftonic panel kit (modules/ui/README.md).
# Run it after changing the parameter table or the layout; commit all three.
import os, sys
HERE = os.path.dirname (os.path.abspath (__file__))
REPO = os.path.normpath (os.path.join (HERE, "..", "..", ".."))
# The kit's tools: in Kraftonic Studio itself, or from KRAFTONIC_UI_TOOLS when
# this product lives in its own repository (cmake --build <build> --target
# startereffect_panel sets it).
sys.path.insert (0, os.environ.get ("KRAFTONIC_UI_TOOLS") or os.path.join (REPO, "modules", "ui", "tools"))
import kraftonic_panel as kp

PANEL = kp.Panel (
    licence="SPDX-License-Identifier: MIT",
    wordmark="KRAFTONIC",
    title="KRAFTONIC STARTER EFFECT",
    # Every ParamId in the header must appear on the panel exactly once.
    params=kp.params_from_header (os.path.join (
        REPO, "plugins/startereffect/dsp/StarterEffectParameters.h")),
    # Header: the preset menu (program parameter 1000) and the output knob.
    header=kp.Header (preset_tag=1000, output="outputGain"),
    # Sections follow the signal: drive -> delay -> dry/wet mix.
    rows=[
        [kp.Section ("DRIVE", [kp.Knob ("Drive", "drive"), kp.Knob ("Tone", "tone")]),
         kp.Section ("DELAY", [kp.Knob ("Time", "delayTime"), kp.Knob ("Feedback", "feedback"),
                               kp.Knob ("Damping", "damping"), kp.Toggle ("Ping-Pong", "pingPong")]),
         kp.Section ("MIX", [kp.Knob ("Dry/Wet", "mix")])],
    ],
)

if __name__ == "__main__":
    kp.main (PANEL, HERE, "startereffect")
