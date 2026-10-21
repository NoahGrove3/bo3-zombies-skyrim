#!/usr/bin/env python3
"""(Noah: make the entire menu hyper realistic) the lake's own life, drawn as things rather than shapes:

  * lily pads in loose rafts - each a waxy disc with its cleft, squashed by the angle we see the water at, a faint
    web of veins, its rim lifted and catching the sky, a dark meniscus where it meets the water, the sky's sheen
    sliding over its far half; a few in flower, the lilies' pointed petals cupped round gold centres;
  * reeds in the near corners - tapering, curving blades in clumps, dark against the water, their edges catching the
    moon from the left; cattail heads on some; sedge at their feet.

    python3 tools/menulakelife.py assets/ui

Writes menu_lake_life.png (2560x1440, alpha)."""
import math, pathlib, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'assets/ui')
W, H, HORIZON = 2560, 1440, 800
S = 3                                                    # drawn three times over, then shrunk: soft true edges
rng = np.random.default_rng(57)


def floor(x, y):
    return ((x - 1160) / 820) ** 2 + ((y - 1190) / 250) ** 2 < 1.0


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def pad(d, x, y, r, fl, ang, notch, flower):
    """one lily pad at (x, y) (painting px), radius r, squash fl"""
    X, Y, R, Rv = x * S, y * S, r * S, r * S * fl
    steps = 90
    a0 = math.radians(ang + notch / 2); a1 = math.radians(ang + 360 - notch / 2)
    def ring(k, lift=0.0):
        pts = [(X, Y)]
        for i in range(steps + 1):
            a = a0 + (a1 - a0) * i / steps
            wob = 1 + 0.025 * math.sin(a * 7 + ang)
            pts.append((X + math.cos(a) * R * k * wob, Y + math.sin(a) * Rv * k * wob - lift))
        return pts
    # the water pulled up round its edge: a dark ring
    d.polygon(ring(1.06), fill=(3, 6, 9, 170))
    base = (int(rng.uniform(15, 22)), int(rng.uniform(30, 40)), int(rng.uniform(26, 32)), 255)
    d.polygon(ring(1.0), fill=base)
    # light and colour across it: the far half (top) catches the sky's sheen, the moon-side (left) a little warmth
    for k in range(10, 0, -1):
        t = k / 10
        c = mix(base, (34, 52, 52, 255), (1 - t) * 0.55)
        d.polygon([(X + (px - X) * t, Y + (py - Y) * t - Rv * 0.10 * (1 - t)) for px, py in ring(1.0)], fill=c)
    for i in range(7):  # veins
        a = a0 + (a1 - a0) * (i + 0.5) / 7
        d.line([(X, Y), (X + math.cos(a) * R * 0.92, Y + math.sin(a) * Rv * 0.92)], fill=(42, 66, 58, 120), width=max(1, int(S * r / 26)))
    # the rim, lifted a little, catching the sky on its far side
    pts = ring(1.0)[1:]
    for i in range(len(pts) - 1):
        a = a0 + (a1 - a0) * i / steps
        far = max(0.0, -math.sin(a))
        c = mix((30, 46, 46, 200), (110, 128, 150, 230), far ** 1.5)
        d.line([pts[i], pts[i + 1]], fill=c, width=max(1, int(S * max(1.0, r / 18))))
    if flower:
        fr = R * 0.38
        cy = Y - Rv * 0.15
        petals = []
        for layer, (n, ln, lift) in enumerate(((10, 1.0, 0.25), (8, 0.75, 0.55), (6, 0.5, 0.85))):
            for i in range(n):
                a = 2 * math.pi * i / n + layer * 0.3 + ang
                petals.append((math.sin(a) * fl, layer, a, ln, lift))
        for _, layer, a, ln, lift in sorted(petals, key=lambda p: (p[1], p[0])):
            tip = (X + math.cos(a) * fr * ln, cy + math.sin(a) * fr * ln * fl * 1.5 - fr * lift)
            sd = (math.cos(a + 1.5) * fr * 0.22, math.sin(a + 1.5) * fr * 0.22 * fl)
            root = (X, cy - fr * lift * 0.3)
            shade = 0.55 + 0.45 * max(0.0, -math.cos(a))           # the moon's side brighter
            c = mix((70, 64, 78, 255), (232, 226, 236, 255), shade * (0.7 + 0.3 * layer / 2))
            d.polygon([root, (root[0] + sd[0], root[1] + sd[1]), tip, (root[0] - sd[0], root[1] - sd[1])], fill=c, outline=mix(c, (40, 36, 48, 255), 0.4))
        d.ellipse([X - fr * 0.18, cy - fr * 0.85 - fr * 0.12, X + fr * 0.18, cy - fr * 0.85 + fr * 0.12], fill=(214, 176, 82, 255))


def blade(d, x0, y0, hgt, lean, width, col, rim):
    """one reed blade: a curved, tapering ribbon"""
    n = 14
    left, right = [], []
    for k in range(n + 1):
        f = k / n
        cx = x0 + lean * hgt * f * f
        cy = y0 - hgt * f
        w = width * (1 - f) ** 0.8 + 0.4
        nx = 1.0
        left.append(((cx - w * nx / 2) * S, cy * S)); right.append(((cx + w * nx / 2) * S, cy * S))
    d.polygon(left + right[::-1], fill=col)
    d.line(left, fill=rim, width=max(1, S // 2 + 1))     # its moon-side edge


def reeds(d, x0, x1, ybase, count):
    clumps = [(rng.uniform(x0, x1), rng.uniform(0.7, 1.25)) for _ in range(count // 12)]
    items = []
    for cx, sc in clumps:
        for _ in range(12):
            x = cx + rng.normal(0, 18)
            hgt = rng.uniform(110, 300) * sc
            lean = rng.normal(0, 0.18) + (0.08 if x < W / 2 else -0.08)
            items.append((hgt, x, lean))
    for hgt, x, lean in sorted(items, key=lambda t: -t[0]):        # tall ones behind
        g = rng.uniform(0, 1)
        col = (int(8 + 8 * g), int(14 + 9 * g), int(13 + 6 * g), 255)
        rim = (int(48 + 30 * g), int(60 + 30 * g), int(78 + 30 * g), 190)
        blade(d, x, ybase + rng.uniform(-6, 10), hgt, lean, rng.uniform(3.0, 6.5), col, rim)
        if rng.random() < 0.12:   # a cattail head on its own stalk
            sx = x + rng.uniform(-6, 6)
            top = ybase - hgt * rng.uniform(0.85, 1.05)
            d.line([(sx * S, ybase * S), ((sx + lean * hgt * 0.7) * S, top * S)], fill=(14, 18, 16, 255), width=S * 2)
            hx = sx + lean * hgt * 0.62
            hy = top + hgt * 0.12
            hw, hh = 6.5, hgt * 0.13
            d.rounded_rectangle([(hx - hw) * S, (hy - hh / 2) * S, (hx + hw) * S, (hy + hh / 2) * S], radius=int(hw * S), fill=(24, 17, 14, 255))
            d.rounded_rectangle([(hx - hw) * S, (hy - hh / 2) * S, (hx - hw * 0.2) * S, (hy + hh / 2) * S], radius=int(hw * S * 0.6), fill=(52, 40, 36, 170))
    # sedge at their feet: short dense blades
    for _ in range(count * 2):
        x = rng.uniform(x0, x1)
        blade(d, x, ybase + rng.uniform(0, 14), rng.uniform(25, 70), rng.normal(0, 0.45), rng.uniform(2, 4),
              (int(rng.uniform(7, 14)), int(rng.uniform(12, 20)), int(rng.uniform(11, 16)), 255), (40, 52, 64, 150))


def main():
    big = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    pads = []
    clusters = [(170, 1040), (420, 905), (2120, 960), (2390, 1110), (1740, 862), (640, 860), (2300, 1360), (260, 1340), (1990, 1300)]
    for cx, cy in clusters:
        for _ in range(int(rng.integers(6, 13))):
            x = cx + rng.normal(0, 70 + (cy - 800) * 0.15)
            y = cy + rng.normal(0, 14 + (cy - 800) * 0.05)
            if y < 812 or y > 1430 or floor(x, y):
                continue
            pads.append((y, x))
    for y, x in sorted(pads):
        t = (y - 800) / 640
        r = (7 + 46 * t ** 1.15) * rng.uniform(0.8, 1.15)
        fl = 0.20 + 0.14 * t
        pad(d, x, y, r, fl, rng.uniform(0, 360), rng.uniform(16, 28), rng.random() < 0.14 and r > 14)
    reeds(d, -40, 330, 1452, 150)
    reeds(d, 2250, 2600, 1452, 150)
    img = big.resize((W, H), Image.LANCZOS)
    img.save(OUT / 'menu_lake_life.png', optimize=True)
    print('menulakelife: wrote menu_lake_life.png')


if __name__ == '__main__':
    main()
