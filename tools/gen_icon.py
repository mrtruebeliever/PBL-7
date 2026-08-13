#!/usr/bin/env python3
"""Generates PBL-7's launcher menu icon: the little explorer ship sprite,
matching the in-game craft (see draw_ship() in src/c/main.c) — a green rounded
hull, a white nose pointing right, a cyan canopy and an orange thruster flame.

Output:
  resources/images/menu_icon.png   25x25   ship on transparent

Rendered supersampled and downsampled for clean edges. Needs Pillow; run with
the pebble-tool venv python (it already has Pillow):
    ~/.local/share/uv/tools/pebble-tool/bin/python tools/gen_icon.py
"""
import os
from PIL import Image, ImageDraw

SS = 8

GREEN = (0x00, 0xFF, 0xAA, 255)     # GColorMediumSpringGreen (hull)
WHITE = (255, 255, 255, 255)        # nose
CYAN = (0x00, 0xFF, 0xFF, 255)      # canopy
ORANGE = (0xFF, 0xAA, 0x00, 255)    # thruster flame
BLACK = (0, 0, 0, 255)              # outline, so the ship reads on a light launcher bg


def draw_ship(W):
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def s(v):
        return v * W

    cx, cy = s(0.46), s(0.50)
    ol = s(0.04)   # outline margin -- about 1px once downsampled from the 8x supersample

    # Each shape is drawn twice: once black and enlarged by `o` (the outline),
    # then at its normal size and colour on top. Where shapes touch, the black
    # halos merge into one contiguous outline around the whole silhouette
    # instead of outlining each piece separately.
    def flame(o):
        d.polygon([(cx - s(0.42) - o, cy), (cx - s(0.26) + o, cy - s(0.10) - o),
                   (cx - s(0.26) + o, cy + s(0.10) + o)], fill=BLACK if o else ORANGE)

    def hull(o):
        d.rounded_rectangle([cx - s(0.28) - o, cy - s(0.18) - o, cx + s(0.20) + o, cy + s(0.18) + o],
                            radius=s(0.10) + o, fill=BLACK if o else GREEN)

    def nose(o):
        d.polygon([(cx + s(0.20) - o, cy - s(0.12) - o), (cx + s(0.42) + o, cy),
                   (cx + s(0.20) - o, cy + s(0.12) + o)], fill=BLACK if o else WHITE)

    def canopy(o):
        cr = s(0.10) + o
        d.ellipse([cx - cr, cy - s(0.05) - cr, cx + cr, cy - s(0.05) + cr], fill=BLACK if o else CYAN)

    for shape in (flame, hull, nose, canopy):
        shape(ol)
    for shape in (flame, hull, nose, canopy):
        shape(0)

    return img


def main():
    os.makedirs("resources/images", exist_ok=True)
    W = 25 * SS
    draw_ship(W).resize((25, 25), Image.LANCZOS).save("resources/images/menu_icon.png")
    print("wrote resources/images/menu_icon.png (25x25)")


if __name__ == "__main__":
    main()
