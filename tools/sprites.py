#!/usr/bin/env python3
"""Pixel-art source for PBL-7's ship, enemies and bosses, in the chunky
black-outline style of Pebble Peaks' climber.

Edit the art here, then run:
    python3 tools/sprites.py                     # writes src/c/sprites.h
    python3 tools/sprites.py --preview out.png   # also a 5x preview sheet

Sprites are drawn in landscape coordinates: x runs along u (the flight
direction), y along v. Every sprite has a centre (cx, cy): the point the game
positions and collides on. Colours "1", "2", "3" are tint slots (light, mid,
dark) that the game fills in at draw time, so one boss serves every planet.
"""
import math
import os
import sys

# char -> (GColor name, RGB for the preview)
PALETTE = {
    "K": ("GColorBlack", (0x00, 0x00, 0x00)),
    "W": ("GColorWhite", (0xFF, 0xFF, 0xFF)),
    "L": ("GColorLightGray", (0xAA, 0xAA, 0xAA)),
    "D": ("GColorDarkGray", (0x55, 0x55, 0x55)),
    "R": ("GColorRed", (0xFF, 0x00, 0x00)),
    "r": ("GColorDarkCandyAppleRed", (0xAA, 0x00, 0x00)),
    "o": ("GColorSunsetOrange", (0xFF, 0x55, 0x55)),
    "Y": ("GColorYellow", (0xFF, 0xFF, 0x00)),
    "y": ("GColorChromeYellow", (0xFF, 0xAA, 0x00)),
    "N": ("GColorWindsorTan", (0xAA, 0x55, 0x00)),
    "C": ("GColorCeleste", (0xAA, 0xFF, 0xFF)),
    "E": ("GColorMediumSpringGreen", (0x00, 0xFF, 0xAA)),
    "e": ("GColorJaegerGreen", (0x00, 0xAA, 0x55)),
    "p": ("GColorDarkGreen", (0x00, 0x55, 0x00)),
    "T": ("GColorMintGreen", (0xAA, 0xFF, 0xAA)),
    "Z": ("GColorInchworm", (0xAA, 0xFF, 0x55)),
    "M": ("GColorKellyGreen", (0x55, 0xAA, 0x00)),
    "g": ("GColorGreen", (0x00, 0xFF, 0x00)),
    "P": ("GColorIslamicGreen", (0x00, 0xAA, 0x00)),
    # Tint slots, filled in by the game (preview: Mercury's champion).
    "1": ("GColorClear", (0xFF, 0x55, 0x55)),
    "2": ("GColorClear", (0xFF, 0x00, 0x00)),
    "3": ("GColorClear", (0xAA, 0x00, 0x00)),
}
TINTS = "123"


# ---------------------------------------------------------------------------
# Drawing kit: sprites are built from shapes, the way a pixel artist layers
# them. Every part gets its own black outline and is painted back to front,
# so parts stay separated by a dark line (the Sega look of Peaks' climber).
# ---------------------------------------------------------------------------
def canvas(w, h):
    return [["."] * w for _ in range(h)]


def disc(g, cx, cy, r):
    return {(x, y) for y in range(len(g)) for x in range(len(g[0]))
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r}


def ell(g, cx, cy, rx, ry):
    return {(x, y) for y in range(len(g)) for x in range(len(g[0]))
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1}


def rect(x0, y0, x1, y1):
    return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}


def part(g, cells, shade, outline="K"):
    w, h = len(g[0]), len(g)
    if outline:
        for (x, y) in cells:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                p = (x + dx, y + dy)
                if p not in cells and 0 <= p[0] < w and 0 <= p[1] < h:
                    g[p[1]][p[0]] = outline
    for (x, y) in cells:
        if 0 <= x < w and 0 <= y < h:
            g[y][x] = shade(x, y) if callable(shade) else shade


def put(g, pixels):
    for (x, y), c in pixels.items():
        g[y][x] = c


def rows(g):
    return ["".join(r) for r in g]


# The ship flies right: a green hull with a white nose cone, two fins, and a
# glass canopy with the Pebblonian pilot inside (big Sega eyes). Every part
# gets its own black outline, painted back to front.


def ship():
    g = canvas(30, 18)
    # Fins, swept back
    top = {(x, y) for y in range(1, 6) for x in range(4 + (5 - y), 12 - (5 - y) // 2)}
    part(g, top, lambda x, y: "e" if y > 2 else "E")
    low = {(x, y) for y in range(11, 16) for x in range(4 + (y - 11), 13 - (y - 11) // 2)}
    part(g, low, lambda x, y: "e" if y < 14 else "p")
    # Engine nozzle
    part(g, rect(1, 7, 3, 11), lambda x, y: "L" if y < 9 else "D")
    # Hull
    hull = rect(4, 6, 21, 12) | rect(22, 7, 24, 11) | rect(25, 8, 26, 10) | {(27, 9)}
    part(g, hull, lambda x, y: ("W" if y < 9 else ("L" if y < 11 else "D")) if x > 21 else
          ("T" if y == 6 else ("E" if y < 10 else ("e" if y < 12 else "p"))))
    # Red racing stripe and a seam before the nose cone
    for x in range(5, 21):
        g[10][x] = "R"
    for y in range(7, 12):
        g[y][21] = "K"
    # Canopy with the pilot
    cx, cy = 15, 5.5
    can = {(x, y) for (x, y) in disc(g, cx, cy, 4.2) if y <= 7}
    part(g, can, lambda x, y: "C")
    pilot = {
        (14, 4): "Z", (15, 4): "Z", (16, 4): "Z",
        (13, 5): "Z", (14, 5): "W", (15, 5): "Z", (16, 5): "W", (17, 5): "Z",
        (13, 6): "Z", (14, 6): "K", (15, 6): "Z", (16, 6): "K", (17, 6): "Z",
        (13, 7): "M", (14, 7): "M", (15, 7): "M", (16, 7): "M", (17, 7): "M",
        (15, 2): "K", (15, 3): "K", (16, 2): "Y",
        (12, 3): "W", (12, 4): "W", (13, 2): "W",
    }
    for (x, y), c in pilot.items():
        if (x, y) in can:
            g[y][x] = c
    return rows(g)


# Exhaust flame, flickering between a long and a short burst.
FLAME0 = [
    "....KKK.",
    "..KKyyyK",
    "KKyyYYYK",
    "KYYYWWWK",
    "KKyyYYYK",
    "..KKyyyK",
    "....KKK.",
]
FLAME1 = [
    "........",
    ".....KK.",
    "...KKyyK",
    "..KYYWWK",
    "...KKyyK",
    ".....KK.",
    "........",
]



# ---------------------------------------------------------------------------
# Enemies. They all fly left (towards the ship), so they face left.
# ---------------------------------------------------------------------------
def drone(f):
    """Round scout bot with one red eye that blinks; antenna stub."""
    g = canvas(15, 15)
    part(g, rect(10, 1, 11, 3), "L")
    part(g, disc(g, 7, 7.5, 5.6), lambda x, y: "W" if (x - 5) ** 2 + (y - 5) ** 2 < 2 else
         ("L" if (x - 7) + (y - 7.5) < 2 else "D"))
    part(g, disc(g, 5.5, 7.5, 2.2), "R" if f == 0 else "o")
    put(g, {(5, 7): "W"})
    for x in range(9, 13):
        g[11][x] = "K"
    return rows(g)


def fighter(f):
    """Red dart with a yellow canopy, nose left, exhaust flickering right."""
    g = canvas(22, 13)
    # exhaust
    fl = rect(18, 5, 20, 7) if f == 0 else rect(18, 5, 19, 7)
    part(g, fl, lambda x, y: "W" if y == 6 and x == 18 else "Y")
    # wings
    part(g, {(x, y) for y in range(0, 5) for x in range(9, 17) if x - 9 >= (4 - y) * 1.2}, "r")
    part(g, {(x, y) for y in range(8, 13) for x in range(9, 17) if x - 9 >= (y - 8) * 1.2}, "r")
    # body
    body = {(x, y) for y in range(3, 10) for x in range(1, 18) if x >= 1 + abs(y - 6) * 2}
    part(g, body, lambda x, y: "o" if y == 4 else ("R" if y < 8 else "r"))
    # canopy + angry visor
    part(g, rect(7, 5, 10, 6), lambda x, y: "Y" if y == 5 else "y")
    put(g, {(7, 5): "W", (6, 4): "K", (7, 4): "K"})
    return rows(g)


def mine(f):
    """Spiky yellow sea-mine of space: black core with a blinking red light."""
    g = canvas(23, 23)
    c = 11
    spikes = set()
    for k in range(8):
        a = k * math.pi / 4
        for t in range(6, 11):
            spikes.add((round(c + math.cos(a) * t), round(c + math.sin(a) * t)))
    part(g, spikes, "y")
    part(g, disc(g, c, c, 6.4), lambda x, y: "W" if (x - 8) ** 2 + (y - 8) ** 2 < 2 else
         ("Y" if (x - c) + (y - c) < -1 else ("y" if (x - c) + (y - c) < 5 else "N")))
    part(g, disc(g, c, c, 2.4), "D")
    put(g, {(c, c): "R" if f == 0 else "K", (c - 1, c): "R" if f == 0 else "D"})
    return rows(g)


def turret():
    """Armoured grey gun block with its barrel to the left and a red sensor."""
    g = canvas(24, 20)
    part(g, rect(1, 8, 7, 11), lambda x, y: "L" if y < 10 else "D")
    part(g, rect(0, 7, 1, 12), "D")
    part(g, rect(7, 2, 22, 17), lambda x, y: "W" if (x, y) in ((8, 3), (9, 3), (8, 4)) else
         ("L" if (x - 7) + (y - 2) < 16 else "D"))
    for (x, y) in ((9, 15), (20, 4), (20, 15), (9, 4)):
        g[y][x] = "K"
    part(g, disc(g, 14.5, 9.5, 3.2), "R")
    put(g, {(13, 8): "W", (14, 8): "o"})
    for x in range(10, 20):
        g[13][x] = "D"
    return rows(g)


def acid(f):
    """Bubbling toxic cloud with mean eyes; bubbles rise between frames."""
    g = canvas(26, 26)
    blobs = [(13, 14, 9), (8, 10 + f, 6), (18, 9 - f, 6), (7, 18, 5), (19, 18 - f, 5)]
    cells = set()
    for (x, y, r) in blobs:
        cells |= disc(g, x, y, r)
    part(g, cells, lambda x, y: "T" if (x - 9) ** 2 + (y - 7) ** 2 < 6 else
         ("Z" if y < 13 else ("g" if y < 19 else "P")))
    for ex in (9, 16):
        part(g, rect(ex - 1, 11, ex + 1, 13), "W")
        put(g, {(ex - 1, 12): "K", (ex - 1, 13): "K", (ex, 13): "K"})
    put(g, {(7, 9): "K", (8, 9): "K", (9, 10): "K", (18, 9): "K", (17, 9): "K", (16, 10): "K",
            (11, 17): "K", (12, 16): "K", (13, 16): "K", (14, 16): "K", (15, 17): "K"})
    for (x, y) in (((4, 4), (21, 3)) if f == 0 else ((3, 2), (22, 5))):
        if g[y][x] == ".":
            part(g, {(x, y)}, "T")
    return rows(g)


def collect():
    """A lost Pebble: the watch with strap stubs and a green screen."""
    g = canvas(11, 15)
    part(g, rect(3, 0, 7, 1) | rect(3, 13, 7, 14), "D")
    part(g, rect(1, 2, 9, 12), "L")
    part(g, rect(2, 4, 8, 10), lambda x, y: "W" if (x, y) == (3, 5) else "E")
    put(g, {(0, 5): "K", (0, 8): "K", (10, 6): "K"})
    return rows(g)


def boss(f):
    """Planet champion: an armoured round core with plates, angry eyes and a
    grille mouth that opens on the second frame. Tint 1/2/3 = the planet's
    light, mid and dark; the game paints it per planet (red when overheated).
    """
    g = canvas(43, 43)
    c = 21
    # side plates / spikes
    for k in range(8):
        a = k * math.pi / 4 + math.pi / 8
        px, py = c + math.cos(a) * 18, c + math.sin(a) * 18
        part(g, disc(g, px, py, 3.3), "3")
    part(g, disc(g, c, c, 17.4), lambda x, y: "W" if (x - 13) ** 2 + (y - 12) ** 2 < 5 else
         ("1" if (x - c) + (y - c) < -8 else ("2" if (x - c) + (y - c) < 12 else "3")))
    # inner ring
    ring = {(x, y) for (x, y) in disc(g, c, c, 12.5) if (x - c) ** 2 + (y - c) ** 2 > 11.0 ** 2}
    for (x, y) in ring:
        g[y][x] = "K"
    # angry eyes glaring left, at the ship, under slanted brows
    for ex, side in ((c - 6, -1), (c + 6, 1)):
        part(g, ell(g, ex, c - 3, 3.4, 3.0), "W")
        put(g, {(ex - 2, c - 3): "K", (ex - 1, c - 3): "K", (ex - 2, c - 2): "K", (ex - 1, c - 2): "K"})
        for k in range(6):
            bx = ex - 3 + k
            by = c - 7 + (k // 2 if side < 0 else (5 - k) // 2)
            g[by][bx] = "K"
            g[by - 1][bx] = "K"
    # grille mouth, gaping on the second frame
    mouth = rect(c - 6, c + 5, c + 6, c + 7 + f * 2)
    part(g, mouth, lambda x, y: "R" if f and y > c + 6 else ("D" if x % 2 else "L"))
    return rows(g)


BOSS_FRAMES = [boss(0), boss(1)]

SPRITES = {
    "SHIP": (ship(), (15, 9)),
    "FLAME0": (FLAME0, (7, 3)),
    "FLAME1": (FLAME1, (7, 3)),
    "DRONE0": (drone(0), (7, 7)),
    "DRONE1": (drone(1), (7, 7)),
    "FIGHTER0": (fighter(0), (10, 6)),
    "FIGHTER1": (fighter(1), (10, 6)),
    "MINE0": (mine(0), (11, 11)),
    "MINE1": (mine(1), (11, 11)),
    "TURRET": (turret(), (13, 10)),
    "ACID0": (acid(0), (13, 13)),
    "ACID1": (acid(1), (13, 13)),
    "COLLECT": (collect(), (5, 7)),
    "BOSS0": (BOSS_FRAMES[0], (21, 21)),
    "BOSS1": (BOSS_FRAMES[1], (21, 21)),
}

ALL = {k: v[0] for k, v in SPRITES.items()}

def check():
    for name, rows_ in ALL.items():
        for i, row in enumerate(rows_):
            if len(row) != len(rows_[0]):
                sys.exit("%s row %d is %d wide, expected %d: %r"
                         % (name, i, len(row), len(rows_[0]), row))
            for ch in row:
                if ch != "." and ch not in PALETTE:
                    sys.exit("%s row %d: unknown colour %r" % (name, i, ch))


def runs(rows_):
    """Horizontal colour runs per row: (row, x, len, colour index)."""
    keys = list(PALETTE)
    out = []
    for y, row in enumerate(rows_):
        x = 0
        while x < len(row):
            ch = row[x]
            if ch == ".":
                x += 1
                continue
            n = 1
            while x + n < len(row) and row[x + n] == ch:
                n += 1
            out.append((y, x, n, keys.index(ch)))
            x += n
    return out


def write_header(path):
    keys = list(PALETTE)
    lines = [
        "// Generated by tools/sprites.py - edit the pixel art there, not here.",
        "#pragma once",
        "",
        "// One horizontal run of same-coloured pixels: row, x, length, colour.",
        "typedef struct { uint8_t y, x, len, col; } SpriteRun;",
        "typedef struct { const SpriteRun *runs; uint16_t count; uint8_t w, h, cx, cy; } Sprite;",
        "",
        "// Colours from SPRITE_TINT on are tint slots (light, mid, dark).",
        "#define SPRITE_TINT %d" % keys.index(TINTS[0]),
        "static GColor sprite_palette(uint8_t i) {",
        "  switch (i) {",
    ]
    for i, k in enumerate(keys):
        if k not in TINTS:
            lines.append("    case %d: return %s;" % (i, PALETTE[k][0]))
    lines += ["    default: return GColorClear;", "  }", "}", ""]
    for name, (rows_, (cx, cy)) in SPRITES.items():
        rs = runs(rows_)
        lines.append("static const SpriteRun RUNS_%s[] = {" % name)
        for y, x, n, c in rs:
            lines.append("  {%d, %d, %d, %d}," % (y, x, n, c))
        lines.append("};")
        lines.append("static const Sprite SPR_%s = { RUNS_%s, %d, %d, %d, %d, %d };"
                     % (name, name, len(rs), len(rows_[0]), len(rows_), cx, cy))
        lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def render(rows_, scale=1):
    from PIL import Image
    img = Image.new("RGBA", (len(rows_[0]) * scale, len(rows_) * scale), (0, 0, 0, 0))
    for y, row in enumerate(rows_):
        for x, ch in enumerate(row):
            if ch != ".":
                for j in range(scale):
                    for i in range(scale):
                        img.putpixel((x * scale + i, y * scale + j), PALETTE[ch][1] + (255,))
    return img


def preview(path, scale=5):
    """All sprites at 5x and 1x on deep space and a planet-orange sky."""
    from PIL import Image
    names = list(ALL)
    w = max(len(r[0]) for r in ALL.values())
    h = max(len(r) for r in ALL.values())
    cell_w, cell_h = (w + 2) * scale, (h + 2) * scale
    per = 7
    nrow = (len(names) + per - 1) // per
    img = Image.new("RGBA", (cell_w * per, cell_h * nrow * 2 + 2 * (h + 4)))
    for bi, bg in enumerate([(0x00, 0x00, 0x55), (0xFF, 0xAA, 0x00)]):
        img.paste(bg + (255,), (0, bi * cell_h * nrow, img.width, (bi + 1) * cell_h * nrow))
        sy = cell_h * nrow * 2 + bi * (h + 4)
        img.paste(bg + (255,), (0, sy, img.width, sy + h + 4))
        ox = 2
        for i, n in enumerate(names):
            img.alpha_composite(render(ALL[n], scale),
                                ((i % per) * cell_w + scale, (bi * nrow + i // per) * cell_h + scale))
            if ox + len(ALL[n][0]) < img.width:
                img.alpha_composite(render(ALL[n]), (ox, sy + 2))
            ox += len(ALL[n][0]) + 4
    img.save(path)


if __name__ == "__main__":
    check()
    here = os.path.dirname(os.path.abspath(__file__))
    write_header(os.path.join(here, "..", "src", "c", "sprites.h"))
    if "--preview" in sys.argv:
        preview(sys.argv[sys.argv.index("--preview") + 1])
