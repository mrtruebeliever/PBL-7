#!/usr/bin/env python3
"""Generates PBL-7's launcher menu icon: the ship with the Pebblonian pilot
in its canopy, in the chunky black-outline pixel style of tools/sprites.py.

The launcher draws menu icons in four greys (black, dark grey, light grey,
white) on white, so the icon is crisp pixel art in Pebble palette colours
chosen for their grey level, with every part outlined in black; pale colours
without an outline would vanish.

Output:
  resources/images/menu_icon.png   25x25 on transparent

Run from the project root (needs Pillow):  python3 tools/gen_icon.py
"""
import os

from PIL import Image

N = 25
PAL = {
    "K": (0x00, 0x00, 0x00), "W": (0xFF, 0xFF, 0xFF), "L": (0xAA, 0xAA, 0xAA),
    "D": (0x55, 0x55, 0x55), "R": (0xFF, 0x00, 0x00), "r": (0xAA, 0x00, 0x00),
    "o": (0xFF, 0x55, 0x55), "Y": (0xFF, 0xFF, 0x00), "y": (0xFF, 0xAA, 0x00),
    "i": (0xFF, 0xFF, 0x55), "A": (0xAA, 0xAA, 0x55), "a": (0x55, 0x55, 0x00),
    "S": (0xFF, 0xAA, 0x55), "s": (0xFF, 0x55, 0x00), "N": (0xAA, 0x55, 0x00),
    "n": (0x55, 0x00, 0x00), "b": (0x00, 0x00, 0x55), "B": (0x00, 0x55, 0xAA),
    "c": (0x00, 0x55, 0xFF), "C": (0xAA, 0xFF, 0xFF), "E": (0x00, 0xFF, 0xAA),
    "e": (0x00, 0xAA, 0x55), "p": (0x00, 0x55, 0x00), "M": (0x55, 0xAA, 0x00),
    "Z": (0xAA, 0xFF, 0x55), "T": (0xAA, 0xFF, 0xAA), "m": (0xAA, 0x00, 0xAA),
    "P": (0x00, 0xAA, 0x00), "G": (0x55, 0xAA, 0xAA), "g": (0x00, 0x55, 0x55),
}


def disc(cx, cy, r):
    return {(x, y) for y in range(N) for x in range(N) if (x - cx) ** 2 + (y - cy) ** 2 <= r * r}


def ell(cx, cy, rx, ry):
    return {(x, y) for y in range(N) for x in range(N)
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1}


def rect(x0, y0, x1, y1):
    return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}


def part(g, cells, shade, diag=False):
    nb = [(1, 0), (-1, 0), (0, 1), (0, -1)] + ([(1, 1), (-1, -1), (1, -1), (-1, 1)] if diag else [])
    for (x, y) in cells:
        for dx, dy in nb:
            p = (x + dx, y + dy)
            if p not in cells and 0 <= p[0] < N and 0 <= p[1] < N:
                g[p[1]][p[0]] = "K"
    for (x, y) in cells:
        if 0 <= x < N and 0 <= y < N:
            g[y][x] = shade(x, y) if callable(shade) else shade


def new():
    return [["."] * N for _ in range(N)]


def to_img(g):
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    for y in range(N):
        for x in range(N):
            if g[y][x] != ".":
                img.putpixel((x, y), PAL[g[y][x]] + (255,))
    return img



def art():
    g = new()
    # flame
    part(g, rect(0, 11, 2, 13), lambda x, y: "Y" if y == 12 else "y")
    # fins
    top = {(x, y) for y in range(3, 9) for x in range(4 + (8 - y), 11 - (8 - y) // 2)}
    part(g, top, "e")
    low = {(x, y) for y in range(16, 22) for x in range(4 + (y - 16), 11 - (y - 16) // 2)}
    part(g, low, "e")
    # nozzle
    part(g, rect(3, 10, 4, 14), "D")
    # hull
    hull = rect(5, 9, 18, 16) | rect(19, 10, 20, 15) | rect(21, 11, 22, 14) | rect(23, 12, 23, 13)
    part(g, hull, lambda x, y: ("L" if y < 13 else "D") if x > 18 else
         ("E" if y < 14 else ("r" if y == 14 else "e")))
    for y in range(10, 16):
        g[y][18] = "K"
    # canopy with pilot
    cx, cy = 13, 8.5
    can = {(x, y) for (x, y) in disc(cx, cy, 6.2) if y <= 10}
    part(g, can, "C")
    face = disc(13, 9, 3.6)
    for (x, y) in face:
        if (x, y) in can:
            g[y][x] = "M"
    for (x, y), c in {(11, 7): "W", (11, 8): "W", (12, 8): "K", (12, 7): "W",
                      (14, 7): "W", (14, 8): "W", (15, 8): "K", (15, 7): "W",
                      (13, 4): "K", (13, 3): "K", (14, 3): "y",
                      (9, 6): "W", (10, 5): "W"}.items():
        g[y][x] = c
    return g


def main():
    os.makedirs("resources/images", exist_ok=True)
    to_img(art()).save("resources/images/menu_icon.png")
    print("wrote resources/images/menu_icon.png (25x25)")


if __name__ == "__main__":
    main()
