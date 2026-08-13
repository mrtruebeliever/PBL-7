#!/usr/bin/env python3
"""Generates PBL-7's Rebble appstore marketing art: the two store icons and the
listing banner. Style matches the in-game explorer ship (see draw_ship() in
src/c/main.c) on a deep-space starfield.

The banner makes the game's key quirk obvious: the Pebble Time 2 is held
SIDEWAYS (a quarter turn, buttons along the bottom) so the display becomes a
landscape 228x200 playfield. It shows a rotated PT2 with a mini gameplay scene
on its screen plus a rotation arrow and an upright-watch hint.

Outputs:
  store/icon-80.png            80x80
  store/icon-144.png           144x144
  store/banner-720x320.png     720x320

Needs Pillow; run with the pebble-tool venv python (it already has Pillow):
    ~/.local/share/uv/tools/pebble-tool/bin/python tools/gen_marketing.py
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFont, ImageFilter

SS = 4

# In-game palette (Pebble 64-colour RGB).
GREEN = (0x00, 0xFF, 0xAA, 255)     # ship hull (MediumSpringGreen)
CYAN = (0x00, 0xFF, 0xFF, 255)      # canopy
WHITE = (255, 255, 255, 255)
ORANGE = (0xFF, 0xAA, 0x00, 255)    # thruster / Mercury
RED = (0xFF, 0x00, 0x00, 255)       # fighter
GRAY = (0xAA, 0xAA, 0xAA, 255)      # drone
CHROME = (0xFF, 0xAA, 0x00, 255)
OXFORD = (0x00, 0x00, 0x55)         # space background
DEEP = (0x02, 0x02, 0x14)
BLACK = (0, 0, 0, 255)               # ship outline, matching tools/gen_icon.py

FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def vgrad(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        row = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = row
    return img.convert("RGBA")


def starfield(canvas, n, seed=1):
    rnd = random.Random(seed)
    d = ImageDraw.Draw(canvas)
    W, H = canvas.size
    for _ in range(n):
        x, y = rnd.random() * W, rnd.random() * H
        r = rnd.choice([1, 1, 1, 2])
        c = rnd.choice([WHITE, (0xAA, 0xAA, 0xAA, 255), (0xDD, 0xDD, 0xFF, 255)])
        d.ellipse([x - r, y - r, x + r, y + r], fill=c)


def draw_ship(d, cx, cy, scale):
    # Matches tools/gen_icon.py's draw_ship: same outline treatment, so the
    # launcher icon, store icons and banner all show the same silhouette.
    def s(v):
        return v * scale
    ol = s(0.045)

    def thruster(o):
        d.polygon([(cx - s(0.42) - o, cy), (cx - s(0.24) + o, cy - s(0.11) - o),
                   (cx - s(0.24) + o, cy + s(0.11) + o)], fill=BLACK if o else ORANGE)

    def hull(o):
        d.rounded_rectangle([cx - s(0.28) - o, cy - s(0.18) - o, cx + s(0.20) + o, cy + s(0.18) + o],
                            radius=s(0.10) + o, fill=BLACK if o else GREEN)

    def nose(o):
        d.polygon([(cx + s(0.20) - o, cy - s(0.12) - o), (cx + s(0.44) + o, cy),
                   (cx + s(0.20) - o, cy + s(0.12) + o)], fill=BLACK if o else WHITE)

    def canopy(o):
        cr = s(0.10) + o
        d.ellipse([cx - cr, cy - s(0.05) - cr, cx + cr, cy - s(0.05) + cr], fill=BLACK if o else CYAN)

    for shape in (thruster, hull, nose, canopy):
        shape(ol)
    for shape in (thruster, hull, nose, canopy):
        shape(0)


def draw_planet(canvas, cx, cy, r, col):
    d = ImageDraw.Draw(canvas)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col)


def store_icon(size):
    W = size * SS
    canvas = vgrad(W, W, OXFORD, DEEP)
    starfield(canvas, int(W * 0.05))
    # a planet at a corner
    draw_planet(canvas, W * 0.82, W * 0.86, W * 0.30, (*ORANGE[:3], 255))
    d = ImageDraw.Draw(canvas)
    draw_ship(d, W * 0.44, W * 0.46, W * 0.72)
    # a couple of shots
    for i in range(3):
        x = W * (0.60 + i * 0.10)
        d.rounded_rectangle([x, W * 0.45, x + W * 0.06, W * 0.48], radius=W*0.01, fill=CYAN)
    return canvas.resize((size, size), Image.LANCZOS)


def draw_watch_landscape(canvas, x, y, w, h):
    """A stylised Pebble Time 2 held SIDEWAYS: body wider than tall, three
    buttons along the bottom (DOWN/SELECT/UP), BACK on top, band stubs left and
    right, and a mini gameplay scene on the landscape screen."""
    d = ImageDraw.Draw(canvas)
    # band stubs (were top/bottom, now left/right after the quarter turn)
    bandc = (0x33, 0x33, 0x3A, 255)
    d.rounded_rectangle([x - w * 0.10, y + h * 0.24, x + w * 0.03, y + h * 0.76], radius=10, fill=bandc)
    d.rounded_rectangle([x + w * 0.97, y + h * 0.24, x + w * 1.10, y + h * 0.76], radius=10, fill=bandc)
    # body / bezel
    d.rounded_rectangle([x, y, x + w, y + h], radius=int(h * 0.14), fill=(0x0E, 0x0E, 0x14, 255))
    d.rounded_rectangle([x + 4, y + 4, x + w - 4, y + h - 4], radius=int(h * 0.12),
                        outline=(0x44, 0x44, 0x55, 255), width=3)
    # buttons: three on the bottom edge, one on top
    btnc = (0x66, 0x66, 0x70, 255)
    for i, bx in enumerate((0.24, 0.48, 0.72)):
        d.rounded_rectangle([x + w * bx, y + h + 2, x + w * bx + w * 0.10, y + h + 12], radius=4, fill=btnc)
    d.rounded_rectangle([x + w * 0.44, y - 12, x + w * 0.44 + w * 0.12, y - 2], radius=4, fill=btnc)

    # ---- screen (landscape playfield) ----
    sx, sy, sw, sh = x + 12, y + 12, w - 24, h - 24
    screen = Image.new("RGBA", (int(sw), int(sh)), (0, 0, 0, 0))
    sd = ImageDraw.Draw(screen)
    sd.rounded_rectangle([0, 0, sw, sh], radius=8, fill=OXFORD + (255,))
    # HUD bar
    sd.rectangle([0, 0, sw, sh * 0.12], fill=(0, 0, 0, 255))
    sd.rectangle([6, sh * 0.03, sw * 0.30, sh * 0.09], fill=(0x00, 0xFF, 0x00, 255))   # HP
    sd.rectangle([sw * 0.70, sh * 0.03, sw * 0.95, sh * 0.09], fill=(0x00, 0x77, 0xEE, 255))  # special
    # starfield
    rnd = random.Random(7)
    for _ in range(40):
        px, py = rnd.random() * sw, sh * 0.15 + rnd.random() * sh * 0.8
        sd.ellipse([px, py, px + 2, py + 2], fill=WHITE)
    # planet at horizon
    sd.ellipse([sw * 0.72, sh * 0.78, sw * 1.15, sh * 1.35], fill=ORANGE)
    # enemies
    sd.ellipse([sw * 0.60, sh * 0.35, sw * 0.60 + 14, sh * 0.35 + 14], fill=GRAY)
    sd.rounded_rectangle([sw * 0.78, sh * 0.50, sw * 0.78 + 20, sh * 0.50 + 12], radius=3, fill=RED)
    # player shots
    for i in range(3):
        bx = sw * (0.30 + i * 0.10)
        sd.rectangle([bx, sh * 0.47, bx + 10, sh * 0.50], fill=CYAN)
    # ship
    draw_ship(sd, sw * 0.16, sh * 0.48, sh * 0.34)
    canvas.alpha_composite(screen, (int(sx), int(sy)))


def rotation_hint(canvas, cx, cy):
    """Small upright watch + curved arrow: 'turn it a quarter to play'."""
    d = ImageDraw.Draw(canvas)
    # tiny upright watch
    d.rounded_rectangle([cx - 16, cy - 22, cx + 16, cy + 22], radius=6, fill=(0x22, 0x22, 0x2A, 255))
    d.rounded_rectangle([cx - 12, cy - 18, cx + 12, cy + 18], radius=4, fill=OXFORD + (255,))
    # curved arrow (quarter turn, clockwise)
    bbox = [cx - 34, cy - 34, cx + 34, cy + 34]
    d.arc(bbox, start=-80, end=20, fill=WHITE, width=4)
    # arrowhead
    ax, ay = cx + 32, cy - 6
    d.polygon([(ax, ay), (ax - 12, ay - 4), (ax - 4, ay - 14)], fill=WHITE)


def banner():
    W, H = 720, 320
    canvas = vgrad(W, H, OXFORD, DEEP)
    starfield(canvas, 130, seed=3)

    # rotated PT2 with gameplay, left of centre
    draw_watch_landscape(canvas, 70, 96, 300, 150)
    rotation_hint(canvas, 150, 292)

    d = ImageDraw.Draw(canvas)
    title_font = ImageFont.truetype(FONT_PATH, 66)
    sub_font = ImageFont.truetype(FONT_PATH, 21)
    small_font = ImageFont.truetype(FONT_PATH, 18)

    tx = 418
    d.text((tx, 74), "PBL-7", font=title_font, fill=GREEN)
    d.text((tx, 150), "PEBBLONIAN EXPLORER", font=sub_font, fill=WHITE)
    d.text((tx, 184), "Deliver the Time Core", font=small_font, fill=(0xFF, 0xCC, 0x55, 255))
    d.text((tx, 206), "to Earth.", font=small_font, fill=(0xFF, 0xCC, 0x55, 255))
    d.text((tx, 250), "Hold your watch sideways", font=small_font, fill=(0xAA, 0xDD, 0xFF, 255))
    d.text((tx, 272), "for a landscape shmup.", font=small_font, fill=(0xAA, 0xDD, 0xFF, 255))

    return canvas


def main():
    os.makedirs("store", exist_ok=True)
    for sz in (80, 144):
        store_icon(sz).save("store/icon-%d.png" % sz)
        print("wrote store/icon-%d.png (%dx%d)" % (sz, sz, sz))
    banner().save("store/banner-720x320.png")
    print("wrote store/banner-720x320.png (720x320)")


if __name__ == "__main__":
    main()
