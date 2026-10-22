"""The intro film's pictures: a woodcut legend in the manner of a storybook print (bold black ink cut from a block,
flat spot colours, parchment), drawn from scratch in code, scene by scene, in LAYERS so the film can move them.

    python3 tools/legend.py [scene ...]     -> assets/ui/legend/<scene>_<layer>.png (+ preview sheets in /tmp)

Every layer is drawn at twice the size, then cut like a block: edges roughened with noise, gouges carved into big
ink areas, the ink printed unevenly; then shrunk. Layers:
    paper   the page itself (jpg)                         moves: none
    back    sky, far land (opaque over the page)          moves: slow drift
    mid     the main subject                              moves: slight drift / bob
    fx0..2  three frames of flicker (flames, glows)      moves: cycled
    front   foreground figures / framing                  moves: parallax
The film (Hud.cpp DrawFilm) knows each scene's layer motions.
"""
import math, pathlib, random, sys
import numpy as np
import cv2
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'ui' / 'legend'
W, H = 1600, 900          # one picture (16:9)
S = 2                     # drawn at twice the size
INK = (34, 24, 18)
PAPER = (219, 199, 160)
GOLD = (222, 168, 52)
RED = (170, 40, 30)
GREEN = (120, 150, 80)
BLUE = (70, 120, 200)
ICE = (150, 190, 220)
VIOLET = (120, 80, 170)


# ---------------------------------------------------------------- noise
def fbm(h, w, seed, octaves=5, base=4):
    rng = np.random.default_rng(seed)
    acc = np.zeros((h, w), np.float32)
    amp = tot = 0.0
    amp = 1.0
    for o in range(octaves):
        n = base * 2 ** o
        g = rng.random((max(2, n * h // w + 2), n + 2)).astype(np.float32)
        acc += amp * cv2.resize(g, (w, h), interpolation=cv2.INTER_CUBIC)
        tot += amp
        amp *= 0.55
    return acc / tot


# ---------------------------------------------------------------- a canvas of ink and colour
class Layer:
    def __init__(self, seed=1):
        self.ink = Image.new('L', (W * S, H * S), 0)
        self.col = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
        self.di = ImageDraw.Draw(self.ink)
        self.dc = ImageDraw.Draw(self.col)
        self.rng = random.Random(seed)
        self.seed = seed
        self.solid = Image.new('L', (W * S, H * S), 0)   # areas that get carved gouges (big ink fields)
        self.ds = ImageDraw.Draw(self.solid)

    # points in picture units (0..W, 0..H)
    def P(self, pts):
        return [(x * S, y * S) for x, y in pts]

    def poly(self, pts, fill=255, carve=False):
        self.di.polygon(self.P(pts), fill=fill)
        if carve and fill:
            self.ds.polygon(self.P(pts), fill=255)

    def blob(self, pts, fill=255, carve=False, smooth=True):
        self.poly(spline(pts) if smooth else pts, fill, carve)

    def line(self, pts, w, fill=255):
        p = self.P(pts)
        self.di.line(p, fill=fill, width=int(w * S), joint='curve')
        r = w * S / 2
        for x, y in (p[0], p[-1]):
            self.di.ellipse((x - r, y - r, x + r, y + r), fill=fill)

    def curve(self, pts, w, fill=255):
        self.line(spline(pts, closed=False), w, fill)

    def circle(self, cx, cy, r, fill=255, carve=False):
        self.di.ellipse(((cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S), fill=fill)
        if carve and fill:
            self.ds.ellipse(((cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S), fill=255)

    def ring(self, cx, cy, r, w, fill=255):
        self.di.ellipse(((cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S), outline=fill, width=int(w * S))

    def colour(self, pts, rgb, smooth=False):
        self.dc.polygon(self.P(spline(pts) if smooth else pts), fill=rgb + (255,))

    def colour_circle(self, cx, cy, r, rgb):
        self.dc.ellipse(((cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S), fill=rgb + (255,))

    def rect(self, x0, y0, x1, y1, fill=255, carve=False):
        self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], fill, carve)

    # ---- cut it like a block, return RGBA at final size
    def cut(self, rough=2.2, gouge=True, paper=None):
        ink = np.array(self.ink, np.float32) / 255
        h, w = ink.shape
        # roughen: displace by noise, then threshold
        nx = (fbm(h, w, self.seed * 7 + 1, 5, 24) - 0.5) * rough * S * 2.2
        ny = (fbm(h, w, self.seed * 7 + 2, 5, 24) - 0.5) * rough * S * 2.2
        gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
        ink = cv2.remap(ink, gx + nx, gy + ny, cv2.INTER_LINEAR)
        ink = cv2.GaussianBlur(ink, (0, 0), 0.8 * S)
        ink = np.clip((ink - 0.45) / 0.12, 0, 1)
        if gouge:  # a few chunky knife cuts in the big black fields (the block's own marks)
            solid = np.array(self.solid, np.float32) / 255
            solid = cv2.erode(solid, np.ones((16 * S, 16 * S), np.uint8))
            g = Image.new('L', (w, h), 0)
            dg = ImageDraw.Draw(g)
            rng = np.random.default_rng(self.seed)
            ys, xs = np.nonzero(solid > 0.5)
            if len(xs):
                n = min(len(xs) // (1400 * S * S), 900)
                pick = rng.integers(0, len(xs), n)
                field = fbm(h, w, self.seed + 9, 3, 6)
                for i in pick:
                    x, y = xs[i], ys[i]
                    a = (field[y, x] - 0.5) * 2.4 - 0.3
                    L = rng.uniform(7, 16) * S
                    wd = rng.uniform(2.2, 4.0) * S
                    x1, y1 = x + math.cos(a) * L, y + math.sin(a) * L
                    dg.line((x, y, x1, y1), fill=255, width=int(wd))
                    dg.ellipse((x - wd / 2, y - wd / 2, x + wd / 2, y + wd / 2), fill=255)
            g = np.array(g.filter(ImageFilter.GaussianBlur(0.6 * S)), np.float32) / 255
            ink = np.clip(ink - np.clip((g - 0.3) * 3, 0, 1) * solid, 0, 1)
        # the print: dense black, the paper's tooth showing through in specks
        grain = fbm(h, w, self.seed * 7 + 3, 6, 60)
        tooth = (np.random.default_rng(self.seed + 3).random((h // S, w // S)) < 0.035).astype(np.float32)
        tooth = cv2.resize(cv2.GaussianBlur(tooth, (0, 0), 0.7), (w, h))
        ink *= np.clip(0.97 + (grain - 0.5) * 0.25 - tooth * 0.7, 0.35, 1.0)
        col = np.array(self.col, np.float32) / 255
        ca = col[..., 3]
        # the colour wash is printed a touch off register and blotchy
        ca = cv2.GaussianBlur(ca, (0, 0), 1.2 * S) * np.clip(0.75 + (fbm(h, w, self.seed * 7 + 4, 5, 18) - 0.5) * 0.7, 0.5, 1)
        rgb = col[..., :3]
        out = np.zeros((h, w, 4), np.float32)
        inkc = np.array(INK, np.float32) / 255
        out[..., :3] = rgb * (1 - ink[..., None]) + inkc * ink[..., None]
        out[..., 3] = np.clip(np.maximum(ink, ca * 0.88), 0, 1)
        img = Image.fromarray((out * 255).astype(np.uint8), 'RGBA').resize((W, H), Image.LANCZOS)
        if paper is not None:
            base = paper.copy()
            base.alpha_composite(img)
            return base
        return img


# ---------------------------------------------------------------- shapes
def spline(pts, closed=True, steps=10):
    """Catmull-Rom through the points"""
    if len(pts) < 3:
        return pts
    P = list(pts)
    if closed:
        P = [P[-1]] + P + [P[0], P[1]]
    else:
        P = [P[0]] + P + [P[-1]]
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for s in range(steps):
            t = s / steps
            t2, t3 = t * t, t * t * t
            x = 0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3)
            y = 0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)
            out.append((x, y))
    if not closed:
        out.append(P[-2])
    return out


def T(pts, dx=0, dy=0, s=1.0, rot=0.0, flip=False):
    c, n = math.cos(rot), math.sin(rot)
    out = []
    for x, y in pts:
        if flip:
            x = -x
        x, y = x * s, y * s
        out.append((x * c - y * n + dx, x * n + y * c + dy))
    return out


def spiral(cx, cy, r, turns=1.6, start=0.0, steps=60, cw=True):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        a = start + (1 if cw else -1) * t * turns * 2 * math.pi
        rr = r * (1 - t * 0.85)
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    return pts


def swirl_cloud(L, cx, cy, size, n=4, seed=0, fill_back=True, w=None):
    """the curling cloud of the legends: a row of lobes, each with a spiral cut in"""
    rng = random.Random(seed)
    w = w or size * 0.11
    lobes = []
    x = cx - size * (n - 1) * 0.42
    for i in range(n):
        r = size * rng.uniform(0.38, 0.55)
        y = cy - r * 0.3 * math.sin(i / max(1, n - 1) * math.pi) - rng.uniform(0, size * 0.1)
        lobes.append((x, y, r))
        x += size * rng.uniform(0.62, 0.9)
    if fill_back:  # paper behind (hides what is behind the cloud)
        for (x, y, r) in lobes:
            L.circle(x, y, r + w * 0.6, fill=0)
    for (x, y, r) in lobes:
        L.ring(x, y, r, w)
        L.curve(spiral(x, y, r * 0.78, 1.25, rng.uniform(0, 6.28)), w * 0.85)
    # the flat bottom with a curl at each end
    x0 = lobes[0][0] - lobes[0][2]
    x1 = lobes[-1][0] + lobes[-1][2]
    yb = max(y + r * 0.55 for (x, y, r) in lobes)
    L.line([(x0, yb), (x1, yb)], w)


def flame(L, cx, by, h, w, lean=0.0, seed=0, colour=True, frame=0):
    """a legend flame: a fat black teardrop, a white tongue cut inside, a black core, curling tips (frame sways it)"""
    rng = random.Random(seed * 31 + frame * 7)
    sw = math.sin(frame * 2.09 + seed * 1.7)
    tipx = cx + lean * h + sw * w * 0.18
    outer = [(cx - w * 0.5, by - h * 0.22), (cx - w * 0.38 + sw * w * 0.08, by - h * 0.58), (cx - w * 0.06 + sw * w * 0.12, by - h * 0.82), (tipx, by - h),
             (cx + w * 0.3 + sw * w * 0.1, by - h * 0.66), (cx + w * 0.5, by - h * 0.32), (cx + w * 0.36, by - h * 0.05), (cx, by + h * 0.02), (cx - w * 0.36, by - h * 0.05)]
    if colour:
        L.colour(spline(outer), RED if seed % 3 else GOLD)
    L.blob(outer, 255, carve=True)
    inner = [(cx - w * 0.26, by - h * 0.2), (cx - w * 0.16 + sw * w * 0.1, by - h * 0.5), (cx + (tipx - cx) * 0.6, by - h * 0.78), (cx + w * 0.2 + sw * w * 0.08, by - h * 0.46),
             (cx + w * 0.26, by - h * 0.2), (cx + w * 0.12, by - h * 0.08), (cx - w * 0.12, by - h * 0.08)]
    L.blob(inner, 0)
    if colour:
        L.colour(spline(inner), GOLD if seed % 3 else RED)
    core = [(cx - w * 0.12, by - h * 0.17), (cx - w * 0.02 + sw * w * 0.06, by - h * 0.46), (cx + w * 0.12, by - h * 0.17), (cx, by - h * 0.1)]
    L.blob(core, 255)
    # a curl breaking off the tip
    L.curve(spiral(tipx + w * 0.22 * (1 if sw > 0 else -1), by - h * 1.08, w * 0.14, 1.1, frame * 1.3), max(2.5, w * 0.06))


def sunburst(L, cx, cy, r, n=10, tri=0.22, colour=GOLD, gap=1.35, w=None):
    """a gold disc ringed by little black triangles (the legends' holy light)"""
    if colour:
        L.colour_circle(cx, cy, r, colour)
    L.ring(cx, cy, r, w or r * 0.12)
    for i in range(n):
        a = i / n * 2 * math.pi
        d = r * gap
        tip = (cx + math.cos(a) * (d + r * tri * 1.6), cy + math.sin(a) * (d + r * tri * 1.6))
        b1 = (cx + math.cos(a - 0.16) * d, cy + math.sin(a - 0.16) * d)
        b2 = (cx + math.cos(a + 0.16) * d, cy + math.sin(a + 0.16) * d)
        L.poly([b1, tip, b2])


def ground_lines(L, y0, y1, seed, n=14, w=3):
    """the carved horizon: a few wavy lines and stipple"""
    rng = random.Random(seed)
    for i in range(n):
        y = rng.uniform(y0, y1)
        x0 = rng.uniform(-50, W * 0.7)
        L.curve([(x0, y), (x0 + 80, y + rng.uniform(-4, 4)), (x0 + 200, y + rng.uniform(-6, 6)), (x0 + rng.uniform(260, 420), y)], w * rng.uniform(0.6, 1.2))


def speckle(L, x0, y0, x1, y1, n, r=(1.5, 3.5), seed=0, fill=255):
    rng = random.Random(seed)
    for _ in range(n):
        L.circle(rng.uniform(x0, x1), rng.uniform(y0, y1), rng.uniform(*r), fill=fill)


def dashes(L, x0, y0, x1, y1, n, seed=0, ang=-0.3, length=(10, 26), w=3):
    rng = random.Random(seed)
    for _ in range(n):
        x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
        a = ang + rng.uniform(-0.3, 0.3)
        Ln = rng.uniform(*length)
        L.line([(x, y), (x + math.cos(a) * Ln, y + math.sin(a) * Ln)], w * rng.uniform(0.6, 1.1))


# ---------------------------------------------------------------- figures
def person(L, x, y, h, pose='stand', face=1, cloak=None, helm=False, horns=False, hood=False, sword=False, spell=None, carve=True, seed=0):
    """a chunky legend figure: feet at (x, y), height h, facing +1 right / -1 left"""
    f = face
    u = h / 10.0
    rng = random.Random(seed)
    # legs
    if pose == 'run':
        L.line([(x, y - 4.6 * u), (x + f * 1.8 * u, y - 2.2 * u), (x + f * 2.6 * u, y)], 1.3 * u)
        L.line([(x, y - 4.6 * u), (x - f * 1.2 * u, y - 2.4 * u), (x - f * 2.6 * u, y - 1.2 * u)], 1.3 * u)
    else:
        L.line([(x - 0.7 * u, y - 4.6 * u), (x - 0.9 * u, y)], 1.4 * u)
        L.line([(x + 0.7 * u, y - 4.6 * u), (x + 0.9 * u, y)], 1.4 * u)
    # body (a tunic / cloak shape)
    body = [(x - 1.6 * u, y - 4.2 * u), (x - 1.3 * u, y - 7.6 * u), (x + 1.3 * u, y - 7.6 * u), (x + 1.7 * u, y - 4.2 * u)]
    if cloak:
        cl = [(x - f * 0.8 * u, y - 7.4 * u), (x - f * 3.4 * u, y - 3.6 * u + rng.uniform(-1, 1) * u), (x - f * 1.2 * u, y - 4.0 * u)]
        L.colour(cl, cloak)
        L.poly(cl, 255, carve)
        L.poly([(p[0] + f * 0.25 * u, p[1]) for p in cl][:2] + [cl[2]], 0)
        L.colour(body, cloak)
    L.blob(body, 255, carve, smooth=False)
    # arms
    if pose == 'run':
        L.line([(x + f * 0.6 * u, y - 7.2 * u), (x + f * 2.6 * u, y - 6.8 * u), (x + f * 4.0 * u, y - 7.4 * u)], 1.0 * u)
        L.line([(x - f * 0.6 * u, y - 7.2 * u), (x + f * 1.8 * u, y - 6.0 * u), (x + f * 3.4 * u, y - 6.4 * u)], 1.0 * u)
    elif pose == 'raise':
        L.line([(x + f * 0.8 * u, y - 7.2 * u), (x + f * 2.0 * u, y - 9.0 * u), (x + f * 2.4 * u, y - 11.0 * u)], 1.0 * u)
        L.line([(x - f * 0.8 * u, y - 7.2 * u), (x - f * 1.8 * u, y - 5.6 * u), (x - f * 1.6 * u, y - 4.4 * u)], 1.0 * u)
    else:
        L.line([(x + f * 1.2 * u, y - 7.2 * u), (x + f * 2.2 * u, y - 5.4 * u), (x + f * 2.0 * u, y - 4.2 * u)], 1.0 * u)
        L.line([(x - f * 1.2 * u, y - 7.2 * u), (x - f * 2.2 * u, y - 5.4 * u), (x - f * 2.0 * u, y - 4.2 * u)], 1.0 * u)
    # head
    hx, hy = x + f * 0.2 * u, y - 8.8 * u
    L.circle(hx, hy, 1.25 * u, 0)
    L.ring(hx, hy, 1.25 * u, 0.42 * u)
    L.circle(hx + f * 0.45 * u, hy - 0.1 * u, 0.22 * u)     # the eye
    if helm or horns:
        L.blob([(hx - 1.4 * u, hy - 0.1 * u), (hx - 1.1 * u, hy - 1.3 * u), (hx, hy - 1.7 * u), (hx + 1.1 * u, hy - 1.3 * u), (hx + 1.4 * u, hy - 0.1 * u)], 255, smooth=True)
        if horns:
            for s in (-1, 1):
                L.poly([(hx + s * 1.0 * u, hy - 1.0 * u), (hx + s * 2.6 * u, hy - 2.8 * u), (hx + s * 2.0 * u, hy - 1.0 * u)])
    if hood:
        L.blob([(hx - 1.6 * u, hy + 0.6 * u), (hx - 1.4 * u, hy - 1.4 * u), (hx + f * 0.2 * u, hy - 2.2 * u), (hx + f * 1.7 * u, hy - 1.0 * u),
                (hx + f * 1.0 * u, hy - 0.9 * u), (hx - f * 0.6 * u, hy + 1.0 * u)], 255)
    if sword:
        tip = (x + f * 3.4 * u, y - 13.0 * u) if pose == 'raise' else (x + f * 5.6 * u, y - 4.0 * u)
        hilt = (x + f * 2.4 * u, y - 11.0 * u) if pose == 'raise' else (x + f * 2.0 * u, y - 4.4 * u)
        L.line([hilt, tip], 0.9 * u)
        L.line([(hilt[0] - 0.9 * u, hilt[1] + 0.3 * u), (hilt[0] + 0.9 * u, hilt[1] - 0.3 * u)], 0.6 * u)
    if spell:
        sx, sy = (x + f * 2.5 * u, y - 11.6 * u) if pose == 'raise' else (x + f * 2.4 * u, y - 4.0 * u)
        L.colour_circle(sx, sy, 1.3 * u, spell)
        L.ring(sx, sy, 1.3 * u, 0.3 * u)
        for i in range(8):
            a = i / 8 * 6.283
            L.line([(sx + math.cos(a) * 1.8 * u, sy + math.sin(a) * 1.8 * u), (sx + math.cos(a) * 2.6 * u, sy + math.sin(a) * 2.6 * u)], 0.3 * u)
    return (hx, hy)


def zombie(L, x, y, h, face=1, phase=0.0, seed=0, eyes=True, carve=True):
    """a running dead: hunched, arms reaching, ragged edges, red eyes"""
    rng = random.Random(seed)
    f = face
    u = h / 10.0
    lean = 1.4 * u
    s1 = math.sin(phase) * 1.4 * u
    # legs (a stride)
    L.line([(x, y - 4.2 * u), (x + f * (1.2 * u + s1), y - 2.0 * u), (x + f * (2.0 * u + s1), y)], 1.2 * u)
    L.line([(x, y - 4.2 * u), (x - f * (0.6 * u + s1), y - 2.2 * u), (x - f * (1.8 * u + s1 * 0.5), y - 0.4 * u)], 1.2 * u)
    # torso: hunched forward, ragged hem
    body = [(x - f * 1.4 * u, y - 4.0 * u), (x - f * 0.8 * u + f * lean * 0.4, y - 7.0 * u), (x + f * (0.6 * u + lean), y - 7.6 * u),
            (x + f * (1.8 * u + lean * 0.5), y - 5.6 * u), (x + f * 1.4 * u, y - 4.2 * u)]
    hem = []
    for i in range(6):
        t = i / 5
        hem.append((x + f * (1.4 * u - 2.8 * u * t), y - 4.2 * u + (0.8 * u if i % 2 else 0)))
    L.poly(body + hem, 255, carve)
    # arms reaching
    sh = (x + f * (0.6 * u + lean), y - 7.2 * u)
    a1 = 0.3 * math.sin(phase + 1.2)
    L.line([sh, (sh[0] + f * 2.2 * u, sh[1] + (0.6 + a1) * u), (sh[0] + f * 4.2 * u, sh[1] + (0.2 + a1) * u)], 0.9 * u)
    L.line([sh, (sh[0] + f * 1.8 * u, sh[1] + (1.6 - a1) * u), (sh[0] + f * 3.8 * u, sh[1] + (1.4 - a1) * u)], 0.9 * u)
    for k in (0.2 + a1, 1.4 - a1):  # claws
        hx, hy = sh[0] + f * (4.2 if k > 1 else 4.0) * u, sh[1] + k * u
        for c in (-0.5, 0, 0.5):
            L.line([(hx, hy), (hx + f * 0.9 * u, hy + c * u)], 0.3 * u)
    # head, thrust forward, jaw hanging
    hx, hy = x + f * (1.4 * u + lean), y - 8.6 * u
    head = [(hx - 1.1 * u, hy + 0.2 * u), (hx - 0.9 * u, hy - 1.1 * u), (hx + f * 0.5 * u, hy - 1.3 * u), (hx + f * 1.3 * u, hy - 0.4 * u),
            (hx + f * 1.0 * u, hy + 1.3 * u), (hx + f * 0.1 * u, hy + 1.6 * u), (hx - f * 0.4 * u, hy + 0.9 * u)]
    L.blob(head, 255, carve)
    L.blob([(hx + f * 0.1 * u, hy + 0.6 * u), (hx + f * 0.9 * u, hy + 0.5 * u), (hx + f * 0.5 * u, hy + 1.3 * u)], 0)   # the mouth
    if eyes:
        L.circle(hx + f * 0.55 * u, hy - 0.35 * u, 0.36 * u, 0)
        L.colour_circle(hx + f * 0.55 * u, hy - 0.35 * u, 0.42 * u, RED)
    # hair strands
    for i in range(3):
        L.line([(hx - 0.6 * u + i * 0.4 * u, hy - 1.1 * u), (hx - f * (1.6 + rng.uniform(0, 1)) * u, hy - (0.4 - i * 0.3) * u)], 0.25 * u)


def dragon(L, cx, cy, size, face=1, wing=0.0, mouth=0.4, skeletal=False, carve=True, seed=0):
    """a dragon in flight: long neck, spiked back, two jagged wings, curling tail (wing: -1 down .. 1 up)"""
    f = face
    u = size / 10.0
    rng = random.Random(seed)
    def pt(x, y):
        return (cx + f * x * u, cy + y * u)
    # wings (behind): a bat wing, finger bones fanned from the shoulder, the membrane scalloped between them
    for side, off in ((-1, -0.7), (1, 0.5)):
        sh = pt(0.8 + off, -1.0)
        up = wing * 1.6
        ex, ey = 0.8 + off + 1.6, -1.0 - 4.4 - up
        elbow = pt(ex, ey)
        tips = []
        sc = 1.0 if side > 0 else 0.84
        for k in range(5):
            a = math.radians(-95 - k * 26 - side * 4)
            r = (6.4 - abs(k - 1.5) * 0.55) * sc
            tips.append(pt(ex + math.cos(a) * r, ey + math.sin(a) * r * (0.85 - wing * 0.1) + k * 0.5))
        mem = [sh, elbow]
        for k, t in enumerate(tips):
            mem.append(t)
            if k < len(tips) - 1:
                n = tips[k + 1]
                mx, my = (t[0] + n[0]) / 2, (t[1] + n[1]) / 2
                mem.append((mx + (sh[0] - mx) * 0.22, my + (sh[1] - my) * 0.22))
        mem.append(pt(-2.6 + off, 0.3))
        L.poly(mem, 255, carve)
        L.line([sh, elbow], 0.55 * u)
        for t in tips:  # finger bones, cut white along the black
            L.line([elbow, (elbow[0] + (t[0] - elbow[0]) * 0.92, elbow[1] + (t[1] - elbow[1]) * 0.92)], 0.2 * u, fill=0)
            L.poly([(t[0] - 0.25 * u, t[1] + 0.1 * u), (t[0] + f * 0.05 * u, t[1] - 0.7 * u), (t[0] + 0.25 * u, t[1] + 0.1 * u)])   # claw
        if skeletal:
            for k in range(len(tips) - 1):
                a_, b_ = tips[k], tips[k + 1]
                hole = [(elbow[0] * 0.45 + a_[0] * 0.55, elbow[1] * 0.45 + a_[1] * 0.55), (elbow[0] * 0.15 + a_[0] * 0.85, elbow[1] * 0.15 + a_[1] * 0.85),
                        (elbow[0] * 0.15 + b_[0] * 0.85, elbow[1] * 0.15 + b_[1] * 0.85), (elbow[0] * 0.45 + b_[0] * 0.55, elbow[1] * 0.45 + b_[1] * 0.55)]
                L.blob(hole, 0)
    # body
    body = [pt(-3.6, 0.6), pt(-1.0, -1.4), pt(2.2, -1.4), pt(3.6, -0.4), pt(3.2, 0.9), pt(0.0, 1.6), pt(-2.8, 1.4)]
    L.blob(body, 255, carve)
    # neck and head
    neck = [pt(2.6, -1.2), pt(4.4, -3.0), pt(5.8, -4.0), pt(6.4, -3.2), pt(5.2, -1.8), pt(3.6, 0.4)]
    L.blob(neck, 255, carve)
    hx, hy = pt(6.4, -3.8)
    m = mouth * 0.9
    head = [pt(5.6, -4.4), pt(6.4, -5.1), pt(8.2, -4.7), pt(8.9, -4.2), pt(7.2, -3.9), pt(8.6, -3.5 + m), pt(7.6, -2.9 + m), pt(6.1, -3.1)]
    L.poly(head, 255, carve)
    L.poly([pt(7.0, -3.95), pt(8.8, -4.1), pt(8.5, -3.5 + m)], 0)       # the mouth's dark
    L.poly([pt(7.0, -3.95), pt(8.8, -4.1), pt(8.5, -3.5 + m)], 255) if mouth < 0.05 else None
    for i in range(4):  # teeth
        tx = 7.2 + i * 0.4
        L.poly([pt(tx, -3.95), pt(tx + 0.15, -3.6), pt(tx + 0.3, -3.97)], 255)
    # horns
    L.poly([pt(6.0, -4.8), pt(4.6, -6.2), pt(6.4, -5.0)])
    L.poly([pt(6.6, -5.0), pt(5.6, -6.6), pt(7.0, -5.1)])
    # eye
    L.circle(*pt(7.1, -4.55), 0.3 * u, 0)
    if skeletal:
        L.colour_circle(*pt(7.1, -4.55), 0.32 * u, ICE)
    # back spikes
    for i in range(9):
        t = i / 8
        bx, by = -3.4 + t * 8.6, (0.4 - 1.8 * math.sin(t * 2.6)) - 0.9
        if t > 0.62:
            bx, by = 3.0 + (t - 0.62) * 8, -1.6 - (t - 0.62) * 6
        L.poly([pt(bx - 0.3, by + 0.3), pt(bx - 0.2, by - 0.9), pt(bx + 0.3, by + 0.2)])
    # tail
    tail = [pt(-3.4, 0.6), pt(-6.0, 1.6), pt(-8.2, 0.8), pt(-9.4, -0.6), pt(-8.6, -1.2)]
    L.curve(tail, 0.9 * u)
    L.poly([pt(-8.6, -1.6), pt(-9.8, -1.6), pt(-9.0, -0.4)])
    # legs and claws
    for lx in (-1.6, 1.8):
        L.line([pt(lx, 1.0), pt(lx + 0.6, 2.4), pt(lx + 1.3, 2.9)], 0.6 * u)
        for c in (-0.3, 0.1, 0.5):
            L.line([pt(lx + 1.3, 2.9), pt(lx + 1.8, 3.1 + c)], 0.2 * u)
    if skeletal:  # ribs cut out of the body
        for i in range(5):
            x = -2.4 + i * 1.1
            L.curve([pt(x, -0.9), pt(x + 0.3, 0.2), pt(x, 1.1)], 0.28 * u, fill=0)


def house(L, x, y, w, h, roof=0.6, carve=True, win=True):
    """a Nord longhouse: a black wall with a white door and beams cut in, a steep roof with shingle rows cut in"""
    L.rect(x, y - h, x + w, y, 255, carve)
    top = (x + w * 0.5, y - h - w * roof)
    L.poly([(x - w * 0.14, y - h + 4), top, (x + w * 1.14, y - h + 4)], 255, carve)
    for k in range(1, 4):  # shingle rows (white)
        t = k / 4
        yy = top[1] + (y - h - top[1]) * t
        hw = (w * 0.64) * t
        L.line([(x + w * 0.5 - hw, yy), (x + w * 0.5 + hw, yy)], 2.2, fill=0)
    L.line([(x + w * 0.5, top[1] - 4), (x + w * 0.5 - 14, top[1] - 26)], 4)      # the crossed gable horns
    L.line([(x + w * 0.5, top[1] - 4), (x + w * 0.5 + 14, top[1] - 26)], 4)
    if win:
        L.rect(x + w * 0.4, y - h * 0.55, x + w * 0.6, y, 0)                     # the door
        L.line([(x + w * 0.08, y - h + 6), (x + w * 0.08, y - 4)], 3, fill=0)   # posts
        L.line([(x + w * 0.92, y - h + 6), (x + w * 0.92, y - 4)], 3, fill=0)


def tower(L, x, y, w, h, carve=True):
    L.rect(x, y - h, x + w, y, 255, carve)
    for i in range(3):  # crenels
        L.rect(x + i * w * 0.38, y - h - w * 0.3, x + i * w * 0.38 + w * 0.24, y - h + 1, 255)
    L.rect(x + w * 0.4, y - h * 0.8, x + w * 0.6, y - h * 0.62, 0)


def barrow(L, x, y, w, h, door=True, carve=True):
    pts = [(x, y), (x + w * 0.18, y - h * 0.85), (x + w * 0.5, y - h), (x + w * 0.82, y - h * 0.85), (x + w, y)]
    L.blob(pts, 255, carve)
    if door:
        L.blob([(x + w * 0.42, y), (x + w * 0.42, y - h * 0.45), (x + w * 0.5, y - h * 0.55), (x + w * 0.58, y - h * 0.45), (x + w * 0.58, y)], 0)


def standing_stone(L, x, y, w, h, rune=True, colour=None):
    pts = [(x, y), (x + w * 0.1, y - h * 0.9), (x + w * 0.5, y - h), (x + w * 0.9, y - h * 0.88), (x + w, y)]
    if colour:
        L.colour(pts, colour)
    L.blob(pts, 255, True)
    if rune:
        cx = x + w * 0.5
        L.line([(cx, y - h * 0.75), (cx, y - h * 0.3)], w * 0.08, 0)
        L.line([(cx, y - h * 0.62), (cx + w * 0.2, y - h * 0.72)], w * 0.08, 0)
        L.line([(cx, y - h * 0.48), (cx - w * 0.2, y - h * 0.58)], w * 0.08, 0)


def grave(L, x, y, w, h, tilt=0.0, cross=False):
    if cross:
        pts = T([(-0.1, 0), (-0.1, -0.62), (-0.35, -0.62), (-0.35, -0.78), (-0.1, -0.78), (-0.1, -1), (0.1, -1), (0.1, -0.78), (0.35, -0.78), (0.35, -0.62), (0.1, -0.62), (0.1, 0)], x, y, h, tilt)
        L.poly(pts)
    else:
        pts = T([(-0.5, 0), (-0.5, -0.7), (-0.35, -0.95), (0, -1.02), (0.35, -0.95), (0.5, -0.7), (0.5, 0)], x, y, h, tilt)
        pts = [(px, py) for px, py in pts]
        L.poly([(x + (px - x) * w / h, py) for px, py in pts], 255, True)


def glyphs(L, x, y, width, size, seed=0, w=None):
    """a band of rune-like carved letters (decoration: the legend's own script)"""
    rng = random.Random(seed)
    w = w or size * 0.16
    cx = x
    while cx < x + width - size * 0.7:
        gw = size * rng.uniform(0.45, 0.8)
        k = rng.randint(0, 9)
        x0, x1, y0, y1 = cx, cx + gw, y, y + size
        xm, ym = (x0 + x1) / 2, (y0 + y1) / 2
        strokes = {
            0: [[(x0, y0), (x0, y1)], [(x0, y0), (x1, y0)], [(x0, ym), (x1, ym)], [(x1, ym), (x1, y1)]],
            1: [[(x0, y0), (x1, y0)], [(xm, y0), (xm, y1)], [(x0, y1), (x1, y1)]],
            2: [[(x0, y0), (x0, y1)], [(x0, ym), (x1, y0)], [(x0, ym), (x1, y1)]],
            3: [[(x0, y0), (x1, y0), (x1, y1)], [(x0, ym), (x1, ym)]],
            4: [[(x0, y1), (xm, y0), (x1, y1)], [(x0 + gw * 0.25, ym + size * 0.1), (x1 - gw * 0.25, ym + size * 0.1)]],
            5: [[(x0, y0), (x0, y1), (x1, y1)], [(xm, y0), (xm, ym)]],
            6: [[(x1, y0), (x0, y0), (x0, y1), (x1, y1)], [(x1, ym), (xm, ym)]],
            7: [[(x0, y0), (x1, y1)], [(x1, y0), (x0, y1)]],
            8: [[(xm, y0), (xm, y1)], [(x0, y0 + size * 0.25), (x1, y0 + size * 0.25)], [(x0, y1 - size * 0.25), (x1, y1 - size * 0.25)]],
            9: [[(x0, y0), (x1, y0), (x0, y1), (x1, y1)]],
        }[k]
        for s in strokes:
            L.line(s, w)
        cx += gw + size * rng.uniform(0.18, 0.32)
        if rng.random() < 0.14:
            cx += size * 0.5


def land(L, top, style='waves', seed=0, w=6, y_bottom=None):
    """a stretch of land: a thick carved outline along 'top' (left to right), the inside left as paper with a pattern"""
    yb = y_bottom or H + 40
    pts = spline(top, closed=False)
    L.poly(pts + [(top[-1][0], yb), (top[0][0], yb)], 0)          # paper (hides what is behind)
    L.line(pts, w)
    rng = random.Random(seed)
    xs = [p[0] for p in pts]
    def top_y(x):
        for i in range(len(pts) - 1):
            if pts[i][0] <= x <= pts[i + 1][0]:
                t = (x - pts[i][0]) / max(1e-3, pts[i + 1][0] - pts[i][0])
                return pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t
        return pts[-1][1]
    if style == 'waves':
        for _ in range(int((max(xs) - min(xs)) / 28)):
            x = rng.uniform(min(xs) + 20, max(xs) - 40)
            y = top_y(x) + rng.uniform(26, 160)
            if y > yb - 20:
                continue
            L.curve([(x, y), (x + 14, y - 7), (x + 30, y), (x + 44, y - 6)], 3.2)
    elif style == 'stipple':
        for _ in range(int((max(xs) - min(xs)) * 0.45)):
            x = rng.uniform(min(xs), max(xs))
            y = top_y(x) + rng.uniform(14, 120) ** 1.0
            L.circle(x, y, rng.uniform(1.4, 3.2))
    elif style == 'grass':
        for _ in range(int((max(xs) - min(xs)) / 36)):
            x = rng.uniform(min(xs) + 10, max(xs) - 10)
            y = top_y(x) + rng.uniform(18, 120)
            for k in (-1, 0, 1):
                L.line([(x, y), (x + k * 7, y - 14 - (k == 0) * 6)], 3)


def hero(L, x, y, h, face=1, pose='stand', tunic=(70, 95, 150), cape=None, helm=False, horns=False, hood=False, sword=False, spell=None, lift=0.0, seed=0):
    """the legend's hero: big head in profile, a coloured tunic with a cut belt, boots, a cap or helm (feet at x, y)"""
    f = face
    u = h / 10.0
    if cape:
        cp = [(x - f * 0.8 * u, y - 7.3 * u), (x - f * 3.6 * u, y - 2.6 * u), (x - f * 2.6 * u, y - 2.2 * u), (x - f * 0.6 * u, y - 4.2 * u)]
        L.colour(cp, cape)
        L.poly(cp, 255)
        L.poly([(x - f * 1.0 * u, y - 6.6 * u), (x - f * 2.9 * u, y - 2.9 * u), (x - f * 2.5 * u, y - 2.8 * u), (x - f * 0.9 * u, y - 4.6 * u)], 0)
        L.colour([(x - f * 1.0 * u, y - 6.6 * u), (x - f * 2.9 * u, y - 2.9 * u), (x - f * 2.5 * u, y - 2.8 * u), (x - f * 0.9 * u, y - 4.6 * u)], cape)
    # legs and boots
    if pose == 'run':
        L.line([(x, y - 4.0 * u), (x + f * 1.6 * u, y - 2.0 * u), (x + f * 2.2 * u, y - 0.4 * u)], 1.1 * u)
        L.line([(x, y - 4.0 * u), (x - f * 1.0 * u, y - 2.0 * u), (x - f * 2.4 * u, y - 1.4 * u)], 1.1 * u)
        L.blob([(x + f * 1.7 * u, y - 0.9 * u), (x + f * 3.0 * u, y - 0.6 * u), (x + f * 2.9 * u, y), (x + f * 1.6 * u, y)], 255, smooth=False)
    else:
        for s in (-1, 1):
            L.line([(x + s * 0.8 * u, y - 4.0 * u), (x + s * 0.9 * u, y - 0.8 * u)], 1.1 * u)
            L.blob([(x + s * 0.9 * u - 0.6 * u, y - 1.2 * u), (x + s * 0.9 * u + 0.6 * u, y - 1.2 * u), (x + s * 0.9 * u + f * 1.4 * u, y - 0.3 * u),
                    (x + s * 0.9 * u + f * 1.4 * u, y), (x + s * 0.9 * u - 0.6 * u, y)], 255, smooth=False)
    # tunic
    tun = [(x - 1.5 * u, y - 7.5 * u), (x + 1.5 * u, y - 7.5 * u), (x + 2.3 * u, y - 3.7 * u), (x - 2.3 * u, y - 3.7 * u)]
    L.colour(tun, tunic)
    L.poly(tun, 255)
    inner = [(x - 1.2 * u, y - 7.2 * u), (x + 1.2 * u, y - 7.2 * u), (x + 1.9 * u, y - 4.0 * u), (x - 1.9 * u, y - 4.0 * u)]
    L.poly(inner, 0)
    L.colour(inner, tunic)
    L.line([(x - 1.85 * u, y - 5.4 * u), (x + 1.85 * u, y - 5.4 * u)], 0.55 * u)          # belt
    L.rect(x + f * 0.1 * u - 0.35 * u, y - 5.75 * u, x + f * 0.1 * u + 0.35 * u, y - 5.05 * u, 0)
    L.line([(x - 0.7 * u, y - 4.9 * u), (x - 1.0 * u, y - 4.0 * u)], 0.22 * u)             # folds
    L.line([(x + 0.7 * u, y - 4.9 * u), (x + 1.0 * u, y - 4.0 * u)], 0.22 * u)
    # arms
    sh = (x + f * 0.9 * u, y - 7.0 * u)
    if pose == 'raise':
        hand = (x + f * 2.4 * u, y - (11.0 + lift) * u)
        L.line([sh, (x + f * 2.0 * u, y - 9.0 * u), hand], 0.9 * u)
    elif pose == 'run':
        hand = (x + f * 3.4 * u, y - 6.8 * u)
        L.line([sh, (x + f * 2.4 * u, y - 6.2 * u), hand], 0.9 * u)
    else:
        hand = (x + f * 2.6 * u, y - 5.0 * u)
        L.line([sh, (x + f * 2.2 * u, y - 6.0 * u), hand], 0.9 * u)
    L.circle(*hand, 0.55 * u)
    back = (x - f * 2.4 * u, y - 4.8 * u)
    L.line([(x - f * 0.9 * u, y - 7.0 * u), (x - f * 2.0 * u, y - 6.0 * u), back], 0.9 * u)
    L.circle(*back, 0.5 * u)
    # head: a big profile, white face in a black outline
    hx, hy = x + f * 0.3 * u, y - 9.0 * u
    face_pts = [(hx - f * 1.5 * u, hy + 0.6 * u), (hx - f * 1.5 * u, hy - 1.0 * u), (hx - f * 0.4 * u, hy - 1.7 * u), (hx + f * 1.1 * u, hy - 1.3 * u),
                (hx + f * 1.5 * u, hy - 0.2 * u), (hx + f * 2.0 * u, hy + 0.2 * u), (hx + f * 1.4 * u, hy + 0.6 * u), (hx + f * 1.2 * u, hy + 1.3 * u),
                (hx + f * 0.2 * u, hy + 1.7 * u), (hx - f * 0.8 * u, hy + 1.4 * u)]
    L.blob(face_pts, 255, smooth=False)
    shrink = [(hx + (px - hx) * 0.78, hy + (py - hy) * 0.78 + 0.05 * u) for px, py in face_pts]
    L.blob(shrink, 0, smooth=False)
    L.circle(hx + f * 0.7 * u, hy - 0.25 * u, 0.33 * u)                                  # eye
    L.line([(hx + f * 0.35 * u, hy - 0.8 * u), (hx + f * 1.05 * u, hy - 0.75 * u)], 0.2 * u)   # brow
    L.line([(hx + f * 0.6 * u, hy + 0.85 * u), (hx + f * 1.05 * u, hy + 0.8 * u)], 0.18 * u)   # mouth
    L.curve([(hx - f * 0.5 * u, hy - 0.2 * u), (hx - f * 0.25 * u, hy + 0.2 * u), (hx - f * 0.55 * u, hy + 0.5 * u)], 0.2 * u)  # ear
    if helm or horns:
        cap = [(hx - f * 1.8 * u, hy + 0.3 * u), (hx - f * 1.7 * u, hy - 1.2 * u), (hx - f * 0.3 * u, hy - 2.3 * u), (hx + f * 1.2 * u, hy - 1.7 * u),
               (hx + f * 1.5 * u, hy - 0.7 * u), (hx - f * 0.1 * u, hy - 0.8 * u), (hx - f * 0.7 * u, hy + 0.4 * u)]
        L.blob(cap, 255, True)
        if horns:
            L.blob([(hx - f * 0.9 * u, hy - 1.8 * u), (hx - f * 2.2 * u, hy - 3.6 * u), (hx - f * 2.7 * u, hy - 4.6 * u), (hx - f * 1.5 * u, hy - 3.5 * u), (hx - f * 0.2 * u, hy - 2.2 * u)], 255)
            L.blob([(hx + f * 0.6 * u, hy - 2.0 * u), (hx + f * 1.6 * u, hy - 3.8 * u), (hx + f * 2.0 * u, hy - 4.8 * u), (hx + f * 1.2 * u, hy - 3.4 * u), (hx + f * 0.9 * u, hy - 1.7 * u)], 255)
    elif hood:
        L.blob([(hx - f * 1.9 * u, hy + 1.6 * u), (hx - f * 1.9 * u, hy - 1.3 * u), (hx - f * 0.2 * u, hy - 2.6 * u), (hx + f * 1.4 * u, hy - 1.6 * u),
                (hx + f * 1.3 * u, hy - 0.9 * u), (hx - f * 0.2 * u, hy - 1.0 * u), (hx - f * 0.8 * u, hy + 1.8 * u)], 255, True)
    else:  # a Nord's shaggy hair and a braid down the back
        hair = [(hx - f * 1.7 * u, hy + 0.9 * u), (hx - f * 1.8 * u, hy - 1.0 * u), (hx - f * 0.6 * u, hy - 2.0 * u), (hx + f * 0.9 * u, hy - 1.8 * u),
                (hx + f * 1.5 * u, hy - 0.9 * u), (hx + f * 0.7 * u, hy - 1.2 * u), (hx + f * 0.2 * u, hy - 0.8 * u), (hx - f * 0.6 * u, hy - 0.6 * u), (hx - f * 0.9 * u, hy + 0.6 * u)]
        L.blob(hair, 255, smooth=False)
        L.curve([(hx - f * 1.5 * u, hy + 0.6 * u), (hx - f * 1.9 * u, hy + 1.8 * u), (hx - f * 1.6 * u, hy + 2.8 * u)], 0.45 * u)
        for k in range(3):
            L.line([(hx - f * (1.7 + 0.05 * k) * u, hy + (1.2 + k * 0.5) * u), (hx - f * (1.45 + 0.05 * k) * u, hy + (1.35 + k * 0.5) * u)], 0.12 * u, fill=0)
        L.blob([(hx + f * 0.2 * u, hy + 0.9 * u), (hx + f * 1.3 * u, hy + 0.9 * u), (hx + f * 1.0 * u, hy + 2.0 * u), (hx + f * 0.4 * u, hy + 1.8 * u)], 255)   # beard
    if sword:
        if pose == 'raise':
            hilt, tip = hand, (hand[0] + f * 1.0 * u, hand[1] - 5.0 * u)
        else:
            hilt, tip = hand, (hand[0] + f * 4.6 * u, hand[1] - 2.4 * u)
        dx, dy = tip[0] - hilt[0], tip[1] - hilt[1]
        n = math.hypot(dx, dy)
        nx, ny = -dy / n, dx / n
        blade = [(hilt[0] + nx * 0.45 * u, hilt[1] + ny * 0.45 * u), (tip[0] + nx * 0.3 * u - dx / n * 0.6 * u, tip[1] + ny * 0.3 * u - dy / n * 0.6 * u), tip,
                 (tip[0] - nx * 0.3 * u - dx / n * 0.6 * u, tip[1] - ny * 0.3 * u - dy / n * 0.6 * u), (hilt[0] - nx * 0.45 * u, hilt[1] - ny * 0.45 * u)]
        L.poly(blade, 255)
        inner_b = [(hilt[0] + nx * 0.2 * u + dx / n * 0.6 * u, hilt[1] + ny * 0.2 * u + dy / n * 0.6 * u), (tip[0] - dx / n * 1.0 * u, tip[1] - dy / n * 1.0 * u),
                   (hilt[0] - nx * 0.2 * u + dx / n * 0.6 * u, hilt[1] - ny * 0.2 * u + dy / n * 0.6 * u)]
        L.poly(inner_b, 0)
        L.line([(hilt[0] + nx * 1.0 * u, hilt[1] + ny * 1.0 * u), (hilt[0] - nx * 1.0 * u, hilt[1] - ny * 1.0 * u)], 0.45 * u)
    if spell:
        sx, sy = hand[0] + f * 0.3 * u, hand[1] - 1.0 * u
        L.colour_circle(sx, sy, 1.2 * u, spell)
        L.ring(sx, sy, 1.2 * u, 0.28 * u)
    return hand


def skeleton_lying(L, x, y, l, face=1):
    """a body laid out in its barrow: skull, ribs, arms crossed"""
    f = face
    u = l / 10.0
    L.circle(x + f * 4.6 * u, y - 0.6 * u, 1.0 * u)                        # skull
    L.circle(x + f * 4.9 * u, y - 0.8 * u, 0.3 * u, 0)                     # its socket (the eye opens later)
    L.line([(x + f * 3.6 * u, y - 0.3 * u), (x - f * 4.6 * u, y)], 0.45 * u)  # spine and legs
    for i in range(5):
        rx = x + f * (3.0 - i * 0.55) * u
        L.curve([(rx, y - 1.0 * u), (rx - f * 0.2 * u, y - 0.2 * u), (rx, y + 0.5 * u)], 0.28 * u)
    L.line([(x + f * 0.4 * u, y), (x - f * 4.8 * u, y - 0.6 * u)], 0.35 * u)
    L.line([(x + f * 0.4 * u, y), (x - f * 4.8 * u, y + 0.5 * u)], 0.35 * u)
    L.line([(x + f * 3.2 * u, y - 0.6 * u), (x + f * 1.4 * u, y - 0.1 * u), (x + f * 2.6 * u, y + 0.5 * u)], 0.3 * u)


def claw_rune(L, x, y, s, k=0, w=None):
    """a Dragon tongue rune: claw strokes and a dot, cut deep"""
    w = w or s * 0.14
    forms = [
        [[(0.1, 0.0), (0.1, 1.0)], [(0.5, 0.0), (0.4, 0.8)], [(0.9, 0.0), (0.9, 1.0)]],
        [[(0.0, 0.2), (1.0, 0.2)], [(0.5, 0.2), (0.2, 1.0)], [(0.5, 0.2), (0.8, 1.0)]],
        [[(0.2, 0.0), (0.8, 1.0)], [(0.8, 0.0), (0.2, 1.0)], [(0.1, 0.5), (0.4, 0.5)]],
        [[(0.0, 0.0), (0.5, 0.5), (1.0, 0.0)], [(0.5, 0.5), (0.5, 1.0)]],
        [[(0.2, 0.0), (0.2, 1.0), (0.9, 1.0)], [(0.6, 0.1), (0.8, 0.6)]],
    ][k % 5]
    for st in forms:
        L.line([(x + px * s, y + py * s) for px, py in st], w)
    L.circle(x + s * 0.5, y + s * 1.25, w * 0.6)


def cartouche(L, x, y, w, h, seed=0, colour=None):
    """a carved name-box (its letters are the legend's own script)"""
    if colour:
        L.colour([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], colour)
    L.rect(x, y, x + w, y + h, 255)
    L.rect(x + 6, y + 6, x + w - 6, y + h - 6, 0)
    if colour:
        L.colour([(x + 6, y + 6), (x + w - 6, y + 6), (x + w - 6, y + h - 6), (x + 6, y + h - 6)], colour)
    glyphs(L, x + 16, y + h * 0.24, w - 32, h * 0.52, seed=seed, w=h * 0.08)


def shard(L, cx, cy, l, rot, seed=0, glow=BLUE):
    """a broken slab of rock: jagged, cracked, a vein of the crystal's light in it"""
    rng = random.Random(seed)
    base = [(-0.5, -0.12), (-0.38, -0.3), (-0.05, -0.36), (0.18, -0.26), (0.5, -0.2), (0.44, 0.02), (0.52, 0.2), (0.2, 0.3), (-0.12, 0.24), (-0.46, 0.16)]
    pts = T([(px + rng.uniform(-0.04, 0.04), py * rng.uniform(0.8, 1.25)) for px, py in base], cx, cy, l, rot)
    L.poly(pts, 255, True)
    crack = T([(-0.3, -0.05), (-0.1, 0.05), (0.08, -0.04), (0.3, 0.06)], cx, cy, l, rot)
    L.line(crack, max(3, l * 0.022), fill=0)
    if glow:
        L.colour(T([(-0.12, -0.03), (0.1, -0.08), (0.14, 0.04), (-0.08, 0.06)], cx, cy, l, rot), glow)
        L.line(T([(-0.12, -0.02), (0.12, 0.02)], cx, cy, l, rot), max(2.5, l * 0.02), fill=0)


# ---------------------------------------------------------------- the page and its border
def paper_page(seed=11):
    h, w = H, W
    n = fbm(h, w, seed, 6, 6)
    img = np.ones((h, w, 3), np.float32) * np.array(PAPER, np.float32)
    img *= (0.965 + 0.05 * (n[..., None] - 0.5) * 2)
    rng = np.random.default_rng(seed)
    fib = Image.new('L', (w, h), 0)
    df = ImageDraw.Draw(fib)
    for _ in range(3500):
        x, y = rng.integers(0, w), rng.integers(0, h)
        a = rng.random() * math.pi
        Ln = rng.integers(5, 22)
        df.line((x, y, x + Ln * math.cos(a), y + Ln * math.sin(a)), fill=int(rng.integers(60, 160)), width=1)
    fib = np.array(fib.filter(ImageFilter.GaussianBlur(0.5)), np.float32) / 255
    img *= (1 - 0.07 * fib[..., None])
    blot = fbm(h, w, seed + 1, 4, 3)
    img *= (1 - 0.04 * np.clip((blot - 0.62) * 4, 0, 1))[..., None]
    img *= (0.97 + 0.06 * rng.random((h, w, 1)))
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')


def frame(L, inset=14, w=7):
    """the cut frame round the picture (a heavy rule and a thin one)"""
    L.rect(0, 0, W, inset + w, 255)
    L.rect(0, H - inset - w, W, H, 255)
    L.rect(0, 0, inset + w, H, 255)
    L.rect(W - inset - w, 0, W, H, 255)
    L.poly([(0, 0), (W, 0), (W, inset), (0, inset)], 0)
    L.poly([(0, H - inset), (W, H - inset), (W, H), (0, H)], 0)
    L.poly([(0, 0), (inset, 0), (inset, H), (0, H)], 0)
    L.poly([(W - inset, 0), (W, 0), (W, H), (W - inset, H)], 0)
    k = inset + w + 9
    for (a, b) in (((k, k), (W - k, k)), ((W - k, k), (W - k, H - k)), ((W - k, H - k), (k, H - k)), ((k, H - k), (k, k))):
        L.line([a, b], 2.4)


# ---------------------------------------------------------------- scenes
SCENES = {}


def scene(fn):
    SCENES[fn.__name__] = fn
    return fn


def save(name, layer, img, jpg=False):
    OUT.mkdir(parents=True, exist_ok=True)
    p = OUT / f'{name}_{layer}.{"jpg" if jpg else "png"}'
    if jpg:
        img.convert('RGB').save(p, quality=86)
    else:
        img.save(p, optimize=True)
    return p


@scene
def helgen(seed=100):
    """Helgen burns: the town's roofs and its keep in flames, people fleeing, the World-Eater over it"""
    out = {}
    back = Layer(seed)
    swirl_cloud(back, 250, 150, 110, 4, seed=1, fill_back=False)
    swirl_cloud(back, 1330, 120, 95, 3, seed=2, fill_back=False)
    dashes(back, 60, 60, W - 60, 420, 55, seed=3, ang=-0.15, w=4)
    # far mountains as carved outlines
    back.curve([(-20, 600), (160, 470), (300, 540), (470, 420), (660, 545), (820, 455), (1010, 560), (1180, 430), (1380, 540), (1620, 460)], 6)
    for (x, y) in ((470, 420), (1180, 430), (160, 470)):
        back.poly([(x - 40, y + 34), (x, y + 2), (x + 40, y + 34), (x + 20, y + 26), (x, y + 40), (x - 20, y + 26)], 255)   # snow caps cut
    out['back'] = back.cut()
    mid = Layer(seed + 1)
    tower(mid, 1050, 770, 130, 360)
    for i, (x, w_, h_) in enumerate([(120, 190, 120), (340, 160, 100), (520, 210, 140), (760, 170, 110), (1230, 190, 125), (1450, 150, 100)]):
        house(mid, x, 775, w_, h_, 0.55 + 0.1 * (i % 2))
    mid.rect(-20, 770, W + 20, 800, 255, True)
    ground_lines(mid, 808, 880, seed=4, n=14, w=4)
    # people fleeing (little figures)
    for i, x in enumerate((300, 640, 900, 1380)):
        person(mid, x, 870, 70, 'run', face=-1 if i % 2 else 1, seed=i)
    out['mid'] = mid.cut()
    spots = [(215, 655, 150), (420, 675, 120), (625, 635, 170), (845, 665, 130), (1115, 410, 150), (1325, 650, 140), (1525, 675, 115)]
    for fr in range(3):
        fx = Layer(seed + 10 + fr)
        for i, (x, y, h_) in enumerate(spots):
            flame(fx, x, y, h_ * (1 + 0.07 * math.sin(fr * 2.1 + i)), h_ * 0.62, lean=0.1 * math.sin(i + fr), seed=i, frame=fr)
            flame(fx, x + h_ * 0.42, y + 10, h_ * 0.62, h_ * 0.4, lean=-0.1, seed=i + 7, frame=fr + 1)
        out[f'fx{fr}'] = fx.cut(gouge=False)
    front = Layer(seed + 20)
    dragon(front, 640, 380, 330, face=1, wing=0.3, mouth=0.6, seed=1)
    out['front'] = front.cut()
    return out


@scene
def sky(seed=200):
    """the World-Eater rises from the ash and sings a word no tongue can name: its rings spread over the land"""
    out = {}
    back = Layer(seed)
    for i, r in enumerate([170, 260, 360, 470, 590, 720]):
        back.ring(760, 300, r, 7 if i % 2 else 3.5)
    swirl_cloud(back, 230, 150, 80, 3, seed=21, fill_back=True)
    land(back, [(-40, 700), (240, 640), (520, 680), (860, 625), (1180, 670), (1420, 640), (1660, 665)], 'waves', seed=5)
    # the ruins of Helgen smoking on the horizon
    for i, x in enumerate((360, 420, 1080, 1150, 1210)):
        back.rect(x, 640 - (i % 2) * 30, x + 40, 690, 255, True)
        back.curve([(x + 20, 640 - (i % 2) * 30), (x + 10, 600), (x + 36, 560), (x + 14, 510)], 4)
    out['back'] = back.cut()
    mid = Layer(seed + 1)
    dragon(mid, 620, 360, 300, face=1, wing=0.7, mouth=1.0, seed=3)
    out['mid'] = mid.cut()
    for fr in range(3):  # the word itself: red claw-runes flying out of the jaws
        fx = Layer(seed + 10 + fr)
        mx, my = 620 + 8.8 * 30, 360 - 3.8 * 30
        for k in range(4):
            t = (k + fr / 3) / 4
            x = mx + 50 + t * 520
            y = my - 10 - math.sin(t * 3.1) * 70
            sz = 46 + t * 46
            box = [(x - sz * 0.15, y - sz * 0.2), (x + sz * 1.15, y - sz * 0.2), (x + sz * 1.15, y + sz * 1.5), (x - sz * 0.15, y + sz * 1.5)]
            fx.colour(box, RED)
            claw_rune(fx, x, y, sz, k=k + 1)
        out[f'fx{fr}'] = fx.cut(gouge=False)
    front = Layer(seed + 20)
    land(front, [(1150, 905), (1230, 850), (1420, 838), (1560, 860), (1660, 905)], 'grass', seed=6)
    hero(front, 1360, 860, 230, face=-1, tunic=(120, 80, 50), seed=2)
    out['front'] = front.cut()
    return out


@scene
def graves(seed=300):
    """the Curse rolls from peak to plain, and every grave in Skyrim hears it"""
    out = {}
    back = Layer(seed)
    for i, r in enumerate([260, 380, 520, 680, 860]):
        back.ring(800, -160, r + 240, 5 if i % 2 else 3)
    back.curve([(-20, 520), (200, 380), (330, 450), (520, 330), (720, 470), (900, 360), (1110, 480), (1300, 350), (1480, 440), (1620, 380)], 6)
    for (x, y) in ((520, 330), (1300, 350), (200, 380), (900, 360)):
        back.poly([(x - 46, y + 40), (x, y + 2), (x + 46, y + 40), (x + 22, y + 30), (x, y + 46), (x - 22, y + 30)], 255)
    land(back, [(-40, 560), (300, 530), (700, 560), (1100, 525), (1660, 555)], 'stipple', seed=8)
    barrow(back, 80, 560, 380, 150)
    barrow(back, 1150, 545, 420, 160)
    out['back'] = back.cut()
    mid = Layer(seed + 1)
    land(mid, [(-40, 770), (400, 750), (800, 770), (1200, 748), (1660, 765)], 'grass', seed=9)
    stones = [(140, 150), (330, 120), (560, 210), (850, 180), (1060, 130), (1270, 200), (1460, 140)]
    for (x, h_) in stones:
        standing_stone(mid, x, 790, h_ * 0.45, h_)
    for i in range(14):
        grave(mid, 100 + i * 108 + (i % 3) * 16, 860 + (i % 2) * 16, 46, 72 + (i % 4) * 10, tilt=0.14 * math.sin(i * 1.7), cross=i % 4 == 1)
    out['mid'] = mid.cut()
    for fr in range(3):  # the stones' runes glow in turn; the graves tremble (marks above them)
        fx = Layer(seed + 10 + fr)
        for i, (x, h_) in enumerate(stones):
            if (i + fr) % 3 == 0:
                fx.colour_circle(x + h_ * 0.225, 790 - h_ * 0.55, h_ * 0.2, VIOLET)
        for i in range(14):
            if (i + fr) % 2 == 0:
                gx = 100 + i * 108 + (i % 3) * 16
                gy = 860 + (i % 2) * 16 - 72 - (i % 4) * 10
                fx.line([(gx - 22, gy - 14), (gx - 30, gy - 30)], 3.5)
                fx.line([(gx + 22, gy - 14), (gx + 30, gy - 30)], 3.5)
        out[f'fx{fr}'] = fx.cut(gouge=False)
    return out


@scene
def dusk(seed=400):
    """by day it sleeps beneath the barrow stone; when the sun goes down the dead come home"""
    out = {}
    back = Layer(seed)
    sunburst(back, 800, 300, 120, 12, colour=GOLD)
    swirl_cloud(back, 330, 170, 80, 3, seed=31, fill_back=True)
    swirl_cloud(back, 1270, 150, 80, 3, seed=32, fill_back=True)
    land(back, [(-40, 380), (260, 330), (560, 370), (800, 320), (1080, 360), (1360, 325), (1660, 370)], 'grass', seed=10)
    out['back'] = back.cut()
    mid = Layer(seed + 1)
    # the barrow cut open: thick earth with the stone chamber in it
    mid.rect(-20, 430, W + 20, 900, 255, True)
    mid.blob([(110, 560), (420, 520), (800, 505), (1180, 520), (1490, 560), (1490, 815), (110, 815)], 0)
    mid.line([(110, 560), (420, 520), (800, 505), (1180, 520), (1490, 560)], 8)
    for i in range(9):  # the chamber's stones (a carved course)
        mid.line([(120 + i * 172, 815), (120 + i * 172, 790)], 5)
    mid.line([(110, 790), (1490, 790)], 5)
    for i in range(4):
        skeleton_lying(mid, 290 + i * 340, 735, 290, face=1)
    for i in range(10):  # roots and stones in the earth (cut white)
        x = 80 + i * 160
        mid.curve([(x, 440), (x + 20, 470), (x - 10, 495)], 4, fill=0)
    speckle(mid, 30, 830, 1570, 890, 90, r=(2, 5), seed=6, fill=0)
    for i in range(9):
        grave(mid, 140 + i * 165, 432, 40, 64, tilt=0.1 * math.sin(i))
    out['mid'] = mid.cut()
    for fr in range(3):  # their eyes open, one after another
        fx = Layer(seed + 10 + fr)
        for i in range(4):
            if i <= fr + 1:
                fx.colour_circle(290 + i * 340 + 4.9 * 29, 735 - 0.8 * 29, 10, RED)
        out[f'fx{fr}'] = fx.cut(gouge=False)
    return out


@scene
def horde(seed=500):
    """they come with the Moon: the dead run you down, the Wailers call the dead"""
    out = {}
    back = Layer(seed)
    back.colour_circle(1190, 230, 160, RED)
    back.ring(1190, 230, 160, 9)
    for (dx, dy, r) in ((-56, -30, 32), (44, 44, 24), (64, -64, 18), (-30, 76, 15)):
        back.ring(1190 + dx, 230 + dy, r, 4.5)
    swirl_cloud(back, 260, 170, 90, 4, seed=7, fill_back=True)
    land(back, [(-40, 620), (400, 575), (760, 610), (1200, 570), (1660, 610)], 'waves', seed=11)
    out['back'] = back.cut()
    for fr in range(3):  # the running dead (three strides)
        m = Layer(seed + 10 + fr)
        for i, (x, y, h_) in enumerate([(180, 840, 170), (420, 870, 200), (650, 830, 160), (300, 760, 120), (560, 750, 110), (820, 790, 140),
                                        (980, 860, 190), (120, 720, 95), (760, 720, 95)]):
            zombie(m, x + fr * 6, y, h_, face=1, phase=fr * 2.1 + i * 1.3, seed=i)
        out[f'fx{fr}'] = m.cut()
    front = Layer(seed + 20)
    wx, wy = 1290, 870
    robe = [(wx - 80, wy), (wx - 52, wy - 200), (wx - 22, wy - 270), (wx + 32, wy - 272), (wx + 62, wy - 200), (wx + 100, wy)]
    front.colour(robe, VIOLET)
    front.poly(robe, 255, True)
    front.poly([(wx - 50, wy - 10), (wx - 30, wy - 190), (wx + 40, wy - 190), (wx + 70, wy - 10)], 0)
    front.colour([(wx - 50, wy - 10), (wx - 30, wy - 190), (wx + 40, wy - 190), (wx + 70, wy - 10)], VIOLET)
    for k in range(4):
        front.line([(wx - 30 + k * 30, wy - 180), (wx - 40 + k * 36, wy - 20)], 5)
    front.blob([(wx - 34, wy - 265), (wx - 46, wy - 345), (wx + 10, wy - 380), (wx + 56, wy - 335), (wx + 44, wy - 265)], 255)
    front.blob([(wx - 14, wy - 314), (wx + 24, wy - 314), (wx + 10, wy - 268)], 0)
    front.colour_circle(wx - 12, wy - 338, 9, RED)
    front.colour_circle(wx + 24, wy - 338, 9, RED)
    front.line([(wx - 44, wy - 240), (wx - 130, wy - 320), (wx - 150, wy - 360)], 15)
    front.line([(wx + 54, wy - 240), (wx + 140, wy - 320), (wx + 156, wy - 362)], 15)
    for i, r in enumerate([60, 105, 150]):
        front.curve([(wx - 150 - r * 0.4, wy - 330 - r), (wx - 150 - r, wy - 340), (wx - 150 - r * 0.4, wy - 350 + r)], 7)
    out['front'] = front.cut()
    return out


def bone_knight(L, x, y, h):
    u = h / 10.0
    hero(L, x, y, h, face=1, tunic=(200, 196, 180), helm=True, horns=True, seed=11)
    for i in range(4):  # the ribcage cut into the breastplate
        L.curve([(x - 1.4 * u, y - (7.0 - i * 0.5) * u), (x, y - (6.6 - i * 0.5) * u), (x + 1.4 * u, y - (7.0 - i * 0.5) * u)], 0.28 * u)
    L.line([(x, y - 7.3 * u), (x, y - 5.6 * u)], 0.3 * u)
    # the great sword planted point-down
    sx = x + 3.4 * u
    L.poly([(sx - 0.5 * u, y - 6.0 * u), (sx + 0.5 * u, y - 6.0 * u), (sx + 0.3 * u, y - 0.6 * u), (sx, y), (sx - 0.3 * u, y - 0.6 * u)])
    L.line([(sx, y - 5.6 * u), (sx, y - 1.4 * u)], 0.15 * u, fill=0)
    L.line([(sx - 1.4 * u, y - 6.0 * u), (sx + 1.4 * u, y - 6.0 * u)], 0.5 * u)
    L.line([(sx, y - 6.0 * u), (sx, y - 7.4 * u)], 0.45 * u)
    L.circle(sx, y - 7.6 * u, 0.4 * u)


def barrow_king(L, x, y, h):
    u = h / 10.0
    hero(L, x, y, h, face=1, tunic=RED, cape=(90, 60, 40), helm=True, seed=12)
    hx, hy = x + 0.3 * u, y - 9.0 * u
    crown = [(hx - 1.7 * u, hy - 1.6 * u), (hx - 1.7 * u, hy - 3.0 * u), (hx - 1.0 * u, hy - 2.2 * u), (hx - 0.3 * u, hy - 3.4 * u),
             (hx + 0.4 * u, hy - 2.2 * u), (hx + 1.1 * u, hy - 3.0 * u), (hx + 1.2 * u, hy - 1.6 * u)]
    L.colour(crown, GOLD)
    L.poly(crown, 255)
    L.poly([(px + (hx - px) * 0.2, py + (hy - 2.2 * u - py) * 0.25) for px, py in crown], 0)
    L.colour([(px + (hx - px) * 0.2, py + (hy - 2.2 * u - py) * 0.25) for px, py in crown], GOLD)
    # the long beard
    L.blob([(hx + 0.2 * u, hy + 0.8 * u), (hx + 1.4 * u, hy + 0.8 * u), (hx + 1.2 * u, hy + 3.4 * u), (hx + 0.4 * u, hy + 4.2 * u), (hx - 0.2 * u, hy + 2.2 * u)], 255, True)
    for k in range(3):
        L.line([(hx + (0.4 + k * 0.3) * u, hy + 1.4 * u), (hx + (0.5 + k * 0.25) * u, hy + 3.4 * u)], 0.16 * u, fill=0)
    # the war axe
    ax = x + 3.0 * u
    L.line([(ax, y - 1.0 * u), (ax + 0.4 * u, y - 9.0 * u)], 0.5 * u)
    L.blob([(ax + 0.3 * u, y - 8.8 * u), (ax + 2.2 * u, y - 9.8 * u), (ax + 2.6 * u, y - 7.8 * u), (ax + 2.0 * u, y - 6.4 * u), (ax + 0.3 * u, y - 7.4 * u)], 255, True)
    L.curve([(ax + 2.0 * u, y - 9.4 * u), (ax + 2.3 * u, y - 8.0 * u), (ax + 1.8 * u, y - 6.8 * u)], 0.2 * u, fill=0)


def lich(L, x, y, h):
    u = h / 10.0
    robe = [(x - 2.6 * u, y), (x - 1.6 * u, y - 7.6 * u), (x + 1.4 * u, y - 7.6 * u), (x + 2.8 * u, y)]
    L.colour(robe, VIOLET)
    L.poly(robe, 255, True)
    L.poly([(x - 1.9 * u, y - 0.4 * u), (x - 1.0 * u, y - 7.0 * u), (x + 0.9 * u, y - 7.0 * u), (x + 2.1 * u, y - 0.4 * u)], 0)
    L.colour([(x - 1.9 * u, y - 0.4 * u), (x - 1.0 * u, y - 7.0 * u), (x + 0.9 * u, y - 7.0 * u), (x + 2.1 * u, y - 0.4 * u)], VIOLET)
    for k in range(3):
        L.line([(x - 0.6 * u + k * 0.6 * u, y - 6.6 * u), (x - 1.0 * u + k * 1.0 * u, y - 0.6 * u)], 0.22 * u)
    glyphs(L, x - 1.6 * u, y - 2.2 * u, 3.4 * u, 0.9 * u, seed=5, w=0.12 * u)
    # the hood and the skull in it
    hx, hy = x, y - 9.0 * u
    L.blob([(hx - 2.0 * u, hy + 1.6 * u), (hx - 1.8 * u, hy - 1.4 * u), (hx, hy - 2.7 * u), (hx + 1.8 * u, hy - 1.4 * u), (hx + 2.0 * u, hy + 1.6 * u)], 255, True)
    L.blob([(hx - 1.1 * u, hy + 0.9 * u), (hx - 1.1 * u, hy - 0.6 * u), (hx, hy - 1.4 * u), (hx + 1.1 * u, hy - 0.6 * u), (hx + 1.0 * u, hy + 0.9 * u), (hx, hy + 1.5 * u)], 0)
    L.circle(hx - 0.45 * u, hy - 0.2 * u, 0.36 * u)
    L.circle(hx + 0.45 * u, hy - 0.2 * u, 0.36 * u)
    L.poly([(hx - 0.15 * u, hy + 0.5 * u), (hx, hy + 0.2 * u), (hx + 0.15 * u, hy + 0.5 * u)])
    for k in range(4):
        L.line([(hx - 0.5 * u + k * 0.33 * u, hy + 0.9 * u), (hx - 0.5 * u + k * 0.33 * u, hy + 1.2 * u)], 0.12 * u)
    # arms: one raised with a spell, one on the staff
    L.line([(x + 1.0 * u, y - 7.0 * u), (x + 2.2 * u, y - 9.0 * u), (x + 2.6 * u, y - 10.6 * u)], 0.8 * u)
    L.line([(x - 1.0 * u, y - 7.0 * u), (x - 2.4 * u, y - 5.6 * u)], 0.8 * u)
    L.line([(x - 2.6 * u, y + 0.2 * u), (x - 2.4 * u, y - 11.2 * u)], 0.42 * u)
    L.circle(x - 2.4 * u, y - 11.6 * u, 0.7 * u)
    L.circle(x - 2.6 * u, y - 11.7 * u, 0.16 * u, 0)
    L.circle(x - 2.2 * u, y - 11.7 * u, 0.16 * u, 0)


@scene
def champions(seed=600):
    """champions stride among them: the Bone Knight, the Barrow King, the Lich who sings the hymn"""
    out = {}
    back = Layer(seed)
    for i in range(3):  # each with its name-box over it
        cartouche(back, 190 + i * 470, 70, 280, 76, seed=i * 5 + 2, colour=(200, 180, 140))
    land(back, [(-40, 790), (500, 778), (1100, 792), (1660, 780)], 'stipple', seed=12)
    dashes(back, 40, 200, W - 40, 520, 30, seed=13, ang=0.2, w=3)
    out['back'] = back.cut()
    mid = Layer(seed + 1)
    bone_knight(mid, 300, 810, 330)
    barrow_king(mid, 780, 810, 360)
    lich(mid, 1290, 810, 340)
    out['mid'] = mid.cut()
    for fr in range(3):  # the lich's spell burns, the knight's eye glows
        fx = Layer(seed + 10 + fr)
        lx, ly = 1290 + 2.6 * 34, 810 - 10.6 * 34 - 30
        fx.colour_circle(lx, ly, 26 + fr * 6, VIOLET)
        fx.ring(lx, ly, 34 + fr * 8, 4)
        for i in range(8):
            a = i / 8 * 6.283 + fr * 0.4
            fx.line([(lx + math.cos(a) * (44 + fr * 6), ly + math.sin(a) * (44 + fr * 6)), (lx + math.cos(a) * (60 + fr * 8), ly + math.sin(a) * (60 + fr * 8))], 4)
        fx.colour_circle(300 + 0.3 * 33 + 0.7 * 33, 810 - 9.0 * 33 - 0.25 * 33, 7 + fr, RED)
        out[f'fx{fr}'] = fx.cut(gouge=False)
    return out


@scene
def wyrm(seed=700):
    """when the fog rolls in across the frozen ground, Grimmvar comes hunting without sound"""
    out = {}
    back = Layer(seed)
    back.colour_circle(290, 190, 90, ICE)
    back.ring(290, 190, 90, 6)
    for i in range(8):
        x = 30 + i * 215
        hh = 260 + (i % 3) * 60
        back.poly([(x, 760), (x + 55, 760 - hh), (x + 110, 760)], 255, True)   # dead pines
        for k in range(5):
            yy = 760 - hh + 60 + k * 40
            back.line([(x + 55, yy), (x + 55 - 30 - k * 8, yy + 22)], 5, fill=0)
            back.line([(x + 55, yy), (x + 55 + 30 + k * 8, yy + 22)], 5, fill=0)
    land(back, [(-40, 760), (500, 750), (1100, 765), (1660, 752)], 'stipple', seed=14)
    out['back'] = back.cut()
    mid = Layer(seed + 1)
    dragon(mid, 900, 430, 280, face=-1, wing=0.4, mouth=0.7, skeletal=True, seed=5)
    out['mid'] = mid.cut()
    for fr in range(3):  # the fog: rows of curling clouds drifting in
        fx = Layer(seed + 10 + fr)
        for row in range(3):
            y = 650 + row * 75
            x0 = -120 + (fr * 45 + row * 130) % 270
            k = 0
            while x0 < W + 100:
                swirl_cloud(fx, x0, y, 72 + row * 10, 3, seed=row * 10 + k, fill_back=True)
                x0 += 330
                k += 1
        out[f'fx{fr}'] = fx.cut(gouge=False)
    return out


@scene
def shrine(seed=800):
    """yet in every city stands a Shrine: the crystal held in broken stone over its rune circle"""
    out = {}
    back = Layer(seed)
    for i, (x, w_, h_) in enumerate([(430, 110, 80), (1180, 120, 90), (1340, 110, 80), (1470, 100, 70)]):
        house(back, x, 700, w_, h_)
    tower(back, 1060, 700, 70, 200)
    land(back, [(-40, 700), (500, 694), (1100, 704), (1660, 696)], 'stipple', seed=15)
    out['back'] = back.cut()
    mid = Layer(seed + 1)
    mid.blob([(360, 800), (560, 742), (800, 728), (1040, 742), (1240, 800), (1040, 858), (800, 872), (560, 858)], 255, True)
    mid.blob([(400, 800), (580, 755), (800, 742), (1020, 755), (1200, 800), (1020, 845), (800, 858), (580, 845)], 0)
    mid.colour([(400, 800), (580, 755), (800, 742), (1020, 755), (1200, 800), (1020, 845), (800, 858), (580, 845)], (190, 200, 220))
    glyphs(mid, 530, 786, 540, 28, seed=4, w=4.5)
    hero(mid, 250, 870, 330, pose='raise', face=1, tunic=(120, 80, 50), spell=BLUE, seed=4)
    out['mid'] = mid.cut()
    for fr in range(3):  # the shards turn slowly round the crystal; the crystal pulses
        fx = Layer(seed + 10 + fr)
        cx, cy = 800, 410
        a0 = fr * 0.1
        ring = [(-150, -190, 170, -0.9), (150, -160, 190, 0.7), (-200, 10, 150, 0.15), (190, 40, 170, -0.25), (-120, 200, 190, 0.8), (130, 210, 150, -0.6), (0, 260, 120, 1.5)]
        for j, (dx, dy, l, rot) in enumerate(ring):
            ang = math.atan2(dy, dx) + a0
            d = math.hypot(dx, dy)
            shard(fx, cx + math.cos(ang) * d, cy + math.sin(ang) * d * 0.92 + math.sin(fr * 2.1 + j) * 6, l, rot + a0 * 0.6, seed=j)
        r = 58 + fr * 5
        fx.colour_circle(cx, cy, r + 30, ICE)
        fx.colour_circle(cx, cy, r, BLUE)
        fx.ring(cx, cy, r, 6)
        fx.poly([(cx - 24, cy - 14), (cx - 6, cy - r - 44), (cx + 4, cy - 20), (cx + 14, cy - r - 30), (cx + 26, cy - 12)], 255)   # its crown of crystal
        for i in range(12):
            a = i / 12 * 6.283 + fr * 0.18
            fx.poly([(cx + math.cos(a - 0.07) * (r + 44), cy + math.sin(a - 0.07) * (r + 44)), (cx + math.cos(a) * (r + 74), cy + math.sin(a) * (r + 74)),
                     (cx + math.cos(a + 0.07) * (r + 44), cy + math.sin(a + 0.07) * (r + 44))])
        fx.colour_circle(800, 800, 30 + fr * 8, BLUE)   # its light pooling on the circle
        out[f'fx{fr}'] = fx.cut(gouge=False)
    return out


@scene
def dragonborn(seed=900):
    """break the curse from hold to hold: only a Dragonborn can set this realm to right"""
    out = {}
    back = Layer(seed)
    sunburst(back, 800, 330, 170, 16, colour=GOLD, gap=1.3)
    swirl_cloud(back, 230, 210, 90, 3, seed=11, fill_back=True)
    swirl_cloud(back, 1370, 190, 90, 3, seed=12, fill_back=True)
    land(back, [(-40, 730), (400, 690), (800, 715), (1200, 680), (1660, 715)], 'grass', seed=16)
    # the holds' towers far off, left and right
    for x in (120, 260, 1330, 1460):
        tower(back, x, 715, 60, 150)
    out['back'] = back.cut()
    mid = Layer(seed + 1)
    hero(mid, 800, 860, 300, pose='raise', face=1, tunic=(150, 50, 40), cape=(100, 40, 30), helm=True, horns=True, sword=True, seed=6)
    out['mid'] = mid.cut()
    for fr in range(3):  # the raised blade blazes
        fx = Layer(seed + 10 + fr)
        x, y = 800 + 2.4 * 30 + 1.0 * 30, 860 - 11.0 * 30 - 5.0 * 30
        for i in range(12):
            a = i / 12 * 6.283 + fr * 0.26
            fx.line([(x + math.cos(a) * (40 + fr * 6), y + math.sin(a) * (40 + fr * 6)), (x + math.cos(a) * (72 + fr * 10), y + math.sin(a) * (72 + fr * 10))], 5)
        fx.colour_circle(x, y, 26 + fr * 4, GOLD)
        out[f'fx{fr}'] = fx.cut(gouge=False)
    return out


# ---------------------------------------------------------------- the words, carved
def carved_text(text, size, colour=INK, width=None, seed=0):
    """a line set in Cinzel, then cut: rough edges, letters a hair off their line, ink uneven"""
    font = ImageFont.truetype(str(ROOT / 'assets' / 'fonts' / 'Cinzel-Bold.ttf'), size * S)
    rng = random.Random(seed)
    # measure
    tmp = ImageDraw.Draw(Image.new('L', (10, 10)))
    widths = [tmp.textlength(c, font=font) for c in text]
    tw = int(sum(widths) + size * S)
    th = int(size * S * 1.6)
    mask = Image.new('L', (tw, th), 0)
    d = ImageDraw.Draw(mask)
    x = size * S * 0.5
    for c, cw in zip(text, widths):
        if c != ' ':
            g = Image.new('L', (int(cw + size * S), th), 0)
            ImageDraw.Draw(g).text((size * S * 0.5, size * S * 0.2), c, font=font, fill=255)
            g = g.rotate(rng.uniform(-3.5, 3.5), resample=Image.BICUBIC, center=(g.width / 2, th * 0.55))
            mask.paste(255, (int(x - size * S * 0.5), int(rng.uniform(-1.6, 1.6) * S)), g)
        x += cw
    m = np.array(mask, np.float32) / 255
    h, w = m.shape
    nx = (fbm(h, w, seed + 5, 4, max(8, w // 60)) - 0.5) * 3.0 * S
    ny = (fbm(h, w, seed + 6, 4, max(8, w // 60)) - 0.5) * 3.0 * S
    gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    m = cv2.remap(m, gx + nx, gy + ny, cv2.INTER_LINEAR)
    m = np.clip((cv2.GaussianBlur(m, (0, 0), 0.7 * S) - 0.45) / 0.14, 0, 1)
    m *= np.clip(0.82 + (fbm(h, w, seed + 7, 5, max(10, w // 40)) - 0.5) * 0.8, 0.6, 1)
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = colour
    out[..., 3] = (m * 255).astype(np.uint8)
    img = Image.fromarray(out, 'RGBA').resize((w // S, h // S), Image.LANCZOS)
    bbox = img.getbbox()
    return img.crop(bbox) if bbox else img


LINES = {
    'l01a': ('In the Fourth Era, two hundred and one,', 40), 'l01b': ('Helgen burned beneath a blackened sun.', 40),
    'l02a': ('You lived. But rising from the ash and flame,', 40), 'l02b': ('the World-Eater sang a word no tongue can name.', 40),
    'l03a': ("Alduin's Curse rolled out from peak to plain,", 40), 'l03b': ('and every grave in Skyrim heard its strain.', 40),
    'l04a': ('By day it sleeps beneath the barrow stone.', 40), 'l04b': ('But when the sun goes down, the dead come home.', 40),
    'l05a': ('They come with the Moon, each night more fierce, more fed:', 38), 'l05b': ('the Undead run you down, the Wailers call the dead.', 38),
    'l06a': ('Champions stride among them, terrible and grim:', 38), 'l06b': ('the Bone Knight, the Barrow King, the Lich who sings the hymn.', 38),
    'l07a': ('And when the fog rolls in across the frozen ground,', 38), 'l07b': ('Grimmvar, the Undying Wyrm, comes hunting without sound.', 38),
    'l08a': ('Yet in every city stands a Shrine of ancient might:', 38), 'l08b': ('take up its spells, and hold its circle through the night.', 38),
    'l09a': ('Break the curse from hold to hold, through fire and through blight,', 38), 'l09b': ('for only a Dragonborn can set this realm to right.', 44),
    'skip': ('Hold Space to skip', 22),
}
RED_LINES = {'l09b'}


def main():
    want = sys.argv[1:] or list(SCENES) + ['text', 'page']
    page = paper_page()
    if 'page' in want:
        # the full page the film sits on (16:9, 1920x1080) with its spiral border ornaments in the corners
        P = Image.new('RGBA', (W, H))
        pg = paper_page(seed=21)
        L = Layer(31)
        for (cx, cy, s) in ((60, 60, 1), (W - 60, 60, -1), (60, H - 60, -1), (W - 60, H - 60, 1)):
            L.curve(spiral(cx, cy, 44, 1.6, 0.0 if s > 0 else 3.14), 6)
            L.ring(cx, cy, 48, 4)
        # a running border of curls down each side
        for y in range(150, H - 150, 70):
            for cx in (40, W - 40):
                L.curve(spiral(cx, y, 22, 1.2, (y // 70) * 1.3), 4.5)
        pg.alpha_composite(L.cut(gouge=False))
        save('page', 'paper', pg, jpg=True)
    if 'text' in want:
        for key, (text, size) in LINES.items():
            img = carved_text(text, size, RED if key in RED_LINES else INK, seed=sum(map(ord, key)))
            save('text', key, img)
        g = Layer(41)
        glyphs(g, 20, 10, W - 40, 34, seed=9, w=5)
        img = g.cut(gouge=False)
        save('text', 'runes', img.crop(img.getbbox()))
    for name, fn in SCENES.items():
        if name not in want:
            continue
        layers = fn()
        # the picture's frame goes on the back layer; the page under it
        back = layers['back']
        fr = Layer(77)
        frame(fr)
        base = paper_page(seed=hash(name) % 1000)
        base.alpha_composite(back)
        base.alpha_composite(fr.cut(gouge=False))
        save(name, 'back', base, jpg=True)
        for k, img in layers.items():
            if k != 'back':
                save(name, k, img)
        # preview: everything at frame 0
        prev = base.copy()
        for k in ('mid', 'fx0', 'front'):
            if k in layers:
                prev.alpha_composite(layers[k])
        prev.convert('RGB').resize((800, 450)).save(f'/tmp/claude-0/legend_{name}.jpg', quality=85)
        print('drew', name, list(layers))


if __name__ == '__main__':
    main()
