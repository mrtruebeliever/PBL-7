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


def draw_ship(W):
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def s(v):
        return v * W

    cx, cy = s(0.46), s(0.50)

    # Thruster flame (left).
    d.polygon([(cx - s(0.42), cy), (cx - s(0.26), cy - s(0.10)),
               (cx - s(0.26), cy + s(0.10))], fill=ORANGE)
    # Hull (rounded body).
    d.rounded_rectangle([cx - s(0.28), cy - s(0.18), cx + s(0.20), cy + s(0.18)],
                        radius=s(0.10), fill=GREEN)
    # Nose (right, pointing forward).
    d.polygon([(cx + s(0.20), cy - s(0.12)), (cx + s(0.42), cy),
               (cx + s(0.20), cy + s(0.12))], fill=WHITE)
    # Canopy.
    cr = s(0.10)
    d.ellipse([cx - cr, cy - s(0.05) - cr, cx + cr, cy - s(0.05) + cr], fill=CYAN)

    return img


def main():
    os.makedirs("resources/images", exist_ok=True)
    W = 25 * SS
    draw_ship(W).resize((25, 25), Image.LANCZOS).save("resources/images/menu_icon.png")
    print("wrote resources/images/menu_icon.png (25x25)")


if __name__ == "__main__":
    main()
