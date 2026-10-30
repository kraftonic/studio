#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Generates startersynth.uidesc and startersynth.panel.json from this
# description with the shared Kraftonic panel kit (modules/ui/README.md).
# Run it after changing the parameter table or the layout; commit all three.
import os, sys
HERE = os.path.dirname (os.path.abspath (__file__))
REPO = os.path.normpath (os.path.join (HERE, "..", "..", ".."))
# The kit's tools: in Kraftonic Studio itself, or from KRAFTONIC_UI_TOOLS when
# this product lives in its own repository (cmake --build <build> --target
# startersynth_panel sets it).
sys.path.insert (0, os.environ.get ("KRAFTONIC_UI_TOOLS") or os.path.join (REPO, "modules", "ui", "tools"))
import kraftonic_panel as kp

PANEL = kp.Panel (
    licence="SPDX-License-Identifier: MIT",
    wordmark="KRAFTONIC",
    title="KRAFTONIC STARTER SYNTH",
    # Every ParamId in the header must appear on the panel exactly once.
    params=kp.params_from_header (os.path.join (
        REPO, "plugins/startersynth/dsp/StarterSynthParameters.h")),
    # Header: the preset menu (program parameter 1000) and the output knob.
    header=kp.Header (preset_tag=1000, output="outputGain"),
    # Sections follow the signal: oscillators -> filter -> envelope.
    rows=[
        [kp.Section ("OSCILLATORS", [kp.Select ("Osc 1", "osc1Wave", ["Saw", "Square", "Sine"]),
                                     kp.Select ("Osc 2", "osc2Wave", ["Saw", "Square", "Sine"]),
                                     kp.Knob ("Detune", "osc2Detune", bipolar=True),
                                     kp.Knob ("Mix", "oscMix")]),
         kp.Section ("FILTER", [kp.Knob ("Cutoff", "cutoff"), kp.Knob ("Reso", "resonance"),
                                kp.Knob ("Env Amt", "envAmount", bipolar=True)])],
        [kp.Section ("ENVELOPE", [kp.Knob ("Attack", "attack"), kp.Knob ("Decay", "decay"),
                                  kp.Knob ("Sustain", "sustain"), kp.Knob ("Release", "release")]),
         kp.Section ("VOICE", [kp.Knob ("Glide", "glide")])],
    ],
)

if __name__ == "__main__":
    kp.main (PANEL, HERE, "startersynth")
