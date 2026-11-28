#!/usr/bin/env python3
"""Bakes the icon atlas (icon-atlas/) from icons.json with the engine's storm_ui_icon_bake (lunasvg).

    python3 mobile-ui/design/bake_icons.py [--tool PATH]   # run after changing icons.json or the bake tool

The phone draws icons from the atlas, tinted; nothing is rasterised at run time. The tool is built by the UI's
stand-alone host build (engine: cmake -S src/libs/ui -B build/ui-host; target storm_ui_icon_bake). Writes
icon-atlas/{atlas.json, coverage.png, colour.png}; atlas.json records the digest build.py checks.
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import build


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    engine = build.REPO.parent / "engine"
    parser.add_argument("--tool", type=Path, default=engine / "build/ui-host/storm_ui_icon_bake")
    args = parser.parse_args()
    icon_pack = build.build_icons()
    target = build.HERE / "icon-atlas"
    target.mkdir(exist_ok=True)
    for old in target.iterdir():
        old.unlink()
    with tempfile.TemporaryDirectory() as scratch:
        source = Path(scratch) / "symbols.json"
        source.write_text(json.dumps({"digest": build.icon_digest(icon_pack, engine), **icon_pack}), encoding="utf-8")
        subprocess.run([str(args.tool), "--in", str(source), "--out", str(target)], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
