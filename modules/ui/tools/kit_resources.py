#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Lists the kit files a .uidesc needs in its bundle: every declared bitmap at
# both scales plus the kit fonts. Prints ;-separated absolute paths for
# kraftonic_add_ui_kit_resources() in modules/ui/CMakeLists.txt.
#
#     kit_resources.py --uidesc <file.uidesc> [--assets <kit/assets>]
#
# Python 3.9+, standard library only.

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET


def main (argv=None):
    ap = argparse.ArgumentParser (description="List kit bundle resources for a .uidesc")
    ap.add_argument ("--uidesc", required=True)
    ap.add_argument ("--assets", default=os.path.normpath (os.path.join (
        os.path.dirname (os.path.abspath (__file__)), "..", "kit", "assets")))
    ap.add_argument ("--kind", choices=("bitmaps", "fonts"), default="bitmaps")
    args = ap.parse_args (argv)
    with open (os.path.join (args.assets, "manifest.json"), encoding="utf-8") as f:
        manifest = {e["name"]: e for e in json.load (f)}
    out = []
    if args.kind == "bitmaps":
        root = ET.parse (args.uidesc).getroot ()
        for b in root.iter ("bitmap"):
            name = b.get ("name")
            if name not in manifest:
                print (f"kit_resources: bitmap '{name}' is not in the kit manifest", file=sys.stderr)
                return 1
            for rel in manifest[name]["files"]:
                path = os.path.join (args.assets, rel)
                if not os.path.isfile (path):
                    print (f"kit_resources: missing {path}", file=sys.stderr)
                    return 1
                out.append (path)
    else:
        fonts = os.path.join (args.assets, "fonts")
        for leaf in sorted (os.listdir (fonts)):
            if leaf.endswith (".ttf"):
                out.append (os.path.join (fonts, leaf))
    print (";".join (out))
    return 0


if __name__ == "__main__":
    sys.exit (main ())
