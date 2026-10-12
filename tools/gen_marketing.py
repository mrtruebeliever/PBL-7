#!/usr/bin/env python3
"""Generates PBL-7's Rebble appstore art from the in-game ship sprite
(tools/sprites.py), scaled up with NEAREST so it stays crisp pixel art, in
the same style as Pebble Peaks' store art. The banner's tagline keeps the
game's key quirk in view: the watch is held sideways.

Outputs:
  store/icon-80.png            80x80    (40x40 canvas, x2)
  store/icon-144.png           144x144  (36x36 canvas, x4)
  store/banner-720x320.png     720x320  (180x80 canvas, x4)

Run from the project root (needs Pillow):  python3 tools/gen_marketing.py
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sprites import ALL, PALETTE  # noqa: E402

C = {k: v[1] + (255,) for k, v in PALETTE.items()}
C.update({
    "O": (0xFF, 0xAA, 0x00, 255),   # ChromeYellow (Mercury, lit)
    "Q": (0xFF, 0x55, 0x00, 255),   # Orange
    "q": (0xAA, 0x55, 0x00, 255),   # WindsorTan (craters)
    "r": (0xAA, 0x00, 0x00, 255),   # DarkCandyAppleRed
    "V": (0xAA, 0x55, 0xFF, 255),   # LavenderIndigo (far stars)
})
SPACE = [(0x00, 0x00, 0x00), (0x00, 0x00, 0x55), (0x00, 0x00, 0xAA)]
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def space(img):
    w, h = img.size
    px = img.load()
    n = len(SPACE)
    for y in range(h):
        t = (y / max(1, h - 1)) * (n - 1)
        i = min(int(t), n - 2)
        frac = t - i
        for x in range(w):
            # ordered 2x2 dither between neighbouring palette bands
            thr = ((x & 1) + 2 * (y & 1)) / 4.0 + 0.125
            px[x, y] = SPACE[i + (1 if frac > thr else 0)] + (255,)


def stars(img, rng, n):
    for _ in range(n):
        x, y = rng.randrange(img.width), rng.randrange(img.height)
        img.putpixel((x, y), C["W"] if rng.random() < 0.6 else C["V"])


def twinkle(img, x, y):
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
        img.putpixel((x + dx, y + dy), C["W"])


def blit(img, name, x, y):
    stamp(img, ALL[name], x, y)


def stamp(img, rows, x, y):
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch != "." and 0 <= x + i < img.width and 0 <= y + j < img.height:
                img.putpixel((x + i, y + j), C[ch])


def planet(img, cx, cy, r):
    """Mercury: lit upper-left, shaded lower-right, a few craters, outlined."""
    px = img.load()
    craters = [(cx - r * 0.45, cy - r * 0.55, r * 0.14), (cx - r * 0.1, cy - r * 0.8, r * 0.09),
               (cx - r * 0.75, cy - r * 0.2, r * 0.1), (cx + r * 0.2, cy - r * 0.45, r * 0.07)]
    for y in range(img.height):
        for x in range(img.width):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if d <= r:
                lit = (x - cx) * 0.7 + (y - cy) * 0.7
                c = "O" if lit < -r * 0.55 else ("Q" if lit < r * 0.1 else "q")
                for (kx, ky, kr) in craters:
                    kd = ((x - kx) ** 2 + (y - ky) ** 2) ** 0.5
                    if kd <= kr:
                        c = "q" if (x - kx) + (y - ky) < 0 else "O"
                px[x, y] = C[c]
            elif d <= r + 1:
                px[x, y] = C["K"]


BOLT = ["KKKKKKK", "KyYYWWK", "KKKKKKK"]


def corona(img, cx, cy, r):
    """The Solar Sentinel's rays: 12 yellow spokes."""
    import math
    for k in range(12):
        a = k * math.pi / 6
        for t in range(14, r + 1):
            x, y = round(cx + math.cos(a) * t), round(cy + math.sin(a) * t)
            if 0 <= x < img.width and 0 <= y < img.height:
                img.putpixel((x, y), C["y"] if t % 3 else C["Y"])


def text(img, xy, s, size, fill, shade=None, shadow=(0, 0, 0x55, 255)):
    """Thresholded bold text with a black outline and a drop shadow."""
    W, H = img.size
    f = ImageFont.truetype(FONT, size)
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).text(xy, s, font=f, fill=255)
    mp = mask.point(lambda v: 255 if v > 127 else 0).load()
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ol = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    op, olp, shp = out.load(), ol.load(), sh.load()
    for y in range(H):
        for x in range(W):
            if mp[x, y]:
                op[x, y] = C[shade] if shade and y > xy[1] + size * 0.52 else C[fill]
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        if 0 <= x + dx < W and 0 <= y + dy < H:
                            olp[x + dx, y + dy] = C["K"]
    for y in range(H):
        for x in range(W):
            if olp[x, y][3] and x + 1 < W and y + 2 < H:
                shp[x + 1, y + 2] = shadow
    img.alpha_composite(sh)
    img.alpha_composite(ol)
    img.alpha_composite(out)


def icon_canvas(n):
    img = Image.new("RGBA", (n, n))
    space(img)
    stars(img, random.Random(3), n // 3)
    planet(img, n + 2, n + 4, n * 0.55)
    blit(img, "FLAME0", (n - 30) // 2 - 7, n // 2 - 6)
    blit(img, "SHIP", (n - 30) // 2 + 1, n // 2 - 9)
    twinkle(img, 5, 5)
    return img


def banner():
    W, H = 180, 80
    img = Image.new("RGBA", (W, H))
    space(img)
    stars(img, random.Random(7), 90)
    planet(img, 176, 92, 46)
    for (x, y) in ((110, 6), (136, 46), (122, 72), (64, 4)):
        twinkle(img, x, y)
    # Mercury's champion, in its in-game colours, with its corona
    C["1"], C["2"], C["3"] = (0xFF, 0xAA, 0x55, 255), (0xFF, 0x55, 0x00, 255), (0xAA, 0x55, 0x00, 255)
    corona(img, 158, 30, 26)
    blit(img, "BOSS0", 137, 9)
    blit(img, "FLAME0", 100, 30)
    blit(img, "SHIP", 107, 24)
    stamp(img, BOLT, 132, 33)
    blit(img, "FIGHTER0", 118, 52)
    blit(img, "DRONE0", 112, 4)
    text(img, (6, 3), "PBL-7", 30, "E", "e")
    text(img, (8, 38), "PEBBLONIAN EXPLORER", 8, "W")
    text(img, (8, 51), "Deliver the Time Core!", 8, "Y")
    text(img, (8, 65), "Hold your watch sideways.", 8, "C")
    return img


def main():
    os.makedirs("store", exist_ok=True)
    icon_canvas(40).resize((80, 80), Image.NEAREST).save("store/icon-80.png")
    icon_canvas(36).resize((144, 144), Image.NEAREST).save("store/icon-144.png")
    banner().resize((720, 320), Image.NEAREST).save("store/banner-720x320.png")
    print("wrote store/icon-80.png, icon-144.png, banner-720x320.png")


if __name__ == "__main__":
    main()
