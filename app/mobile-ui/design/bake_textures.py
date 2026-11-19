#!/usr/bin/env python3
"""Bakes the static shade of the screen textures (textures-src/bake.json) into textures/.

    python3 mobile-ui/design/bake_textures.py      # needs Pillow; run after changing a source or bake.json

C-Screen and C-Header drew a linear-gradient over their cover image on every frame: one more
full-screen blended pass for the GPU. The shade never changes, so it is part of the picture.
Writes textures/<name> and textures/baked.json (the digest build.py checks without Pillow).
"""
import json
import sys
from pathlib import Path

import build

SOURCES = build.HERE / "textures-src"
TARGET = build.HERE / "textures"


def bake(name, spec, colours):
    from PIL import Image

    image = Image.open(SOURCES / name).convert("RGB")
    width, height = image.size
    box_w, box_h = spec["box"]
    # `cover`: the picture scaled to cover the box, centred; element y -> picture row.
    scale = max(box_w / width, box_h / height)  # dp per picture pixel
    top = (box_h - height * scale) / 2  # dp of picture row 0 in the box
    (c0, a0), (c1, a1) = [(build.rgb(colours[token]), round(alpha * 255) / 255) for token, alpha in spec["shade"]]
    pixels = image.load()
    for row in range(height):
        t = ((row + 0.5) * scale + top) / box_h  # position in the gradient, past 0..1 outside the box
        alpha = min(1.0, max(0.0, a0 + (a1 - a0) * t))
        # RmlUi interpolates premultiplied stops and blends source-over.
        shade = [min(255.0, max(0.0, c0[i] * a0 + (c1[i] * a1 - c0[i] * a0) * t)) for i in range(3)]
        for column in range(width):
            r, g, b = pixels[column, row]
            pixels[column, row] = tuple(round(v * (1 - alpha) + s) for v, s in zip((r, g, b), shade))
    image.save(TARGET / name, quality=93)


def main():
    tokens = build.load_tokens()
    specs = build.load_bake_specs()
    TARGET.mkdir(exist_ok=True)
    digests = {}
    for name, spec in specs.items():
        bake(name, spec, tokens["color"])
        digests[name] = build.bake_digest(name, spec, tokens)
        print(f"baked textures/{name}")
    (TARGET / "baked.json").write_text(json.dumps(digests, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
