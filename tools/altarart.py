"""The Echo Altar's floor, seen from above: a disc of dark granite engraved with rings, a dial of ticks, bands of runes
and a sunburst at its heart, all inlaid with gold that glows, and gold dust caught in the grain. One design for the
shrine in the game (tools/shrine.py: its floor's diffuse, normal and glow maps) and on the main menu
(tools/menushrine.py projects the same maps onto the painting's floor).

    floor_maps(n) -> (diffuse RGB, normal RGB, glow RGB, inlay L) at n x n; the disc fills the square (edge = radius 1)
    python3 tools/altarart.py out.png    a preview
"""
import math, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

GOLD = np.array([214, 150, 52], np.float32)
GOLD_HOT = np.array([255, 206, 110], np.float32)


def fbm(n, base, octaves, seed, pers=0.55):
    rng = np.random.default_rng(seed)
    out = np.zeros((n, n), np.float32)
    amp = tot = 0.0
    amp = 1.0
    for o in range(octaves):
        k = base * 2 ** o
        g = rng.random((k + 1, k + 1)).astype(np.float32)
        out += np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC), np.float32) / 255 * amp
        tot += amp
        amp *= pers
    return out / tot


def blur(a, r):
    return np.asarray(Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)), np.float32) / 255


# rune glyphs: strokes on a little grid (x across, y along the radius, -1..1)
BASE_GLYPHS = [[(0, -1, 0, 1), (0, -0.3, 0.6, -0.9)], [(-0.5, 1, 0, -1), (0.5, 1, 0, -1), (-0.3, 0.3, 0.3, 0.3)], [(0, -1, 0, 1), (-0.6, 0, 0.6, 0)],
               [(-0.6, -1, 0.6, 1), (0.6, -1, -0.6, 1)], [(0, -1, 0, 1), (0, 0.1, 0.6, 0.9)], [(-0.6, -0.8, 0.6, -0.8), (0, -0.8, 0, 1)],
               [(-0.5, -1, -0.5, 1), (-0.5, 0, 0.5, -0.6), (-0.5, 0, 0.5, 0.6)], [(0, -1, 0.6, 0), (0.6, 0, 0, 1), (0, 1, -0.6, 0), (-0.6, 0, 0, -1)]]


def glyphs(count, seed):
    rnd = random.Random(seed)
    pts = [(x, y) for x in (-0.6, 0, 0.6) for y in (-1, -0.33, 0.33, 1)]
    out = list(BASE_GLYPHS)
    while len(out) < count:
        g = [(0, -1, 0, 1)] if rnd.random() < 0.6 else []
        for _ in range(rnd.randint(2, 3)):
            a, b = rnd.sample(pts, 2)
            g.append((a[0], a[1], b[0], b[1]))
        out.append(g)
    rnd.shuffle(out)
    return out


def floor_maps(n=2048, seed=3):
    SS = 2
    N = n * SS
    c = N / 2
    R = N / 2 * 0.985                      # (a hair inside the square: the rim stays round)
    inlay = Image.new('L', (N, N), 0)      # the gold lines (engraved, filled)
    burst = Image.new('L', (N, N), 0)      # the sunburst at the heart (brighter)
    d = ImageDraw.Draw(inlay)
    db = ImageDraw.Draw(burst)
    W = lambda f: max(1, int(round(f * N)))  # a width as a fraction of the size

    def ring(r, w):
        d.ellipse([c - r * R, c - r * R, c + r * R, c + r * R], outline=255, width=W(w))

    def polar(r, a):
        return c + math.cos(a) * r * R, c + math.sin(a) * r * R

    # rings (some doubled, like a dial's bezel)
    for r, w in ((0.975, 0.0032), (0.952, 0.0016), (0.862, 0.0022), (0.845, 0.0014), (0.795, 0.0026), (0.615, 0.0026), (0.598, 0.0014),
                 (0.555, 0.0020), (0.375, 0.0024), (0.335, 0.0016), (0.29, 0.0022), (0.115, 0.0030)):
        ring(r, w)
    # the outer band: 40 small runes between 0.862 and 0.952, a dot between each
    G = glyphs(64, seed)
    def glyph(g, r, a, size_r, size_t, w):
        rad = (math.cos(a), math.sin(a))
        tan = (-math.sin(a), math.cos(a))
        for x0, y0, x1, y1 in g:
            p0 = (c + (r - y0 * size_r) * R * rad[0] + x0 * size_t * R * tan[0], c + (r - y0 * size_r) * R * rad[1] + x0 * size_t * R * tan[1])
            p1 = (c + (r - y1 * size_r) * R * rad[0] + x1 * size_t * R * tan[0], c + (r - y1 * size_r) * R * rad[1] + x1 * size_t * R * tan[1])
            d.line([p0, p1], fill=255, width=W(w))
            for p in (p0, p1):
                rr = W(w) / 2
                d.ellipse([p[0] - rr, p[1] - rr, p[0] + rr, p[1] + rr], fill=255)
    for i in range(40):
        a = i / 40 * 2 * math.pi
        glyph(G[i % len(G)], 0.907, a, 0.030, 0.022, 0.0022)
        x, y = polar(0.907, a + math.pi / 40)
        rr = 0.0022 * N
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=255)
    # the middle band: 24 large runes between 0.615 and 0.795, framed by short ticks
    for i in range(24):
        a = i / 24 * 2 * math.pi + math.pi / 24
        glyph(G[(i * 7 + 3) % len(G)], 0.705, a, 0.062, 0.040, 0.0034)
        for r0, r1 in ((0.795, 0.775), (0.615, 0.635)):
            aa = a - math.pi / 24
            d.line([polar(r0, aa), polar(r1, aa)], fill=255, width=W(0.0016))
    # the dial: 120 ticks between 0.375 and 0.555 (every fifth longer), 12 diamonds riding it
    for i in range(120):
        a = i / 120 * 2 * math.pi
        long = i % 5 == 0
        d.line([polar(0.555, a), polar(0.555 - (0.06 if long else 0.03), a)], fill=255, width=W(0.0018 if long else 0.0011))
    for i in range(12):
        a = i / 12 * 2 * math.pi
        cx, cy = polar(0.465, a)
        s = 0.018 * R
        tan = (-math.sin(a), math.cos(a)); rad = (math.cos(a), math.sin(a))
        pts = [(cx + rad[0] * s * 1.6, cy + rad[1] * s * 1.6), (cx + tan[0] * s, cy + tan[1] * s), (cx - rad[0] * s * 1.6, cy - rad[1] * s * 1.6), (cx - tan[0] * s, cy - tan[1] * s)]
        d.polygon(pts, outline=255, width=W(0.0016))
    # the cross: four long arms from the heart to the rim, a diamond where each meets the outer band
    for i in range(4):
        a = i * math.pi / 2 + math.pi / 4
        d.line([polar(0.29, a), polar(0.975, a)], fill=255, width=W(0.0016))
        cx, cy = polar(0.98, a)
        s = 0.022 * R
        tan = (-math.sin(a), math.cos(a)); rad = (math.cos(a), math.sin(a))
        d.polygon([(cx + rad[0] * s * 1.8, cy + rad[1] * s * 1.8), (cx + tan[0] * s, cy + tan[1] * s), (cx - rad[0] * s * 1.8, cy - rad[1] * s * 1.8), (cx - tan[0] * s, cy - tan[1] * s)], fill=255)
    # the band between 0.29 and 0.335: a chain of small runes
    for i in range(32):
        glyph(G[(i * 5 + 1) % len(G)], 0.3125, i / 32 * 2 * math.pi, 0.012, 0.009, 0.0013)
    # the sunburst: rays from the heart, long and short in turn, and a bright core
    for i in range(72):
        a = i / 72 * 2 * math.pi
        r1 = 0.27 if i % 2 == 0 else (0.20 if i % 4 == 1 else 0.16)
        db.line([polar(0.125, a), polar(r1, a)], fill=255, width=W(0.0024 if i % 2 == 0 else 0.0014))
    db.ellipse([c - 0.035 * R, c - 0.035 * R, c + 0.035 * R, c + 0.035 * R], fill=255)
    db.ellipse([c - 0.075 * R, c - 0.075 * R, c + 0.075 * R, c + 0.075 * R], outline=255, width=W(0.003))

    inlay = inlay.resize((n, n), Image.LANCZOS)
    burst = burst.resize((n, n), Image.LANCZOS)
    L = np.asarray(inlay, np.float32) / 255
    B = np.asarray(burst, np.float32) / 255
    lines = np.clip(L + B, 0, 1)

    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    rr = np.hypot(xx - n / 2 + 0.5, yy - n / 2 + 0.5) / (n / 2 * 0.985)
    inside = np.clip((1.0 - rr) * n * 0.5, 0, 1)

    # the stone: charcoal granite - broad mottling, fine grain, pale mica, hairline cracks, worn darker at the rim
    big = fbm(n, 4, 6, seed + 1)
    grain = fbm(n, 64, 4, seed + 2, 0.6)
    rng = np.random.default_rng(seed + 3)
    fine = rng.random((n, n)).astype(np.float32)
    fine = 0.6 * fine + 0.4 * blur(fine, 1.0)
    crack_n = fbm(n, 6, 6, seed + 4, 0.6)
    crack = np.clip((1 - np.abs(crack_n - 0.5) * 2 - 0.965) / 0.035, 0, 1) ** 1.5 * np.clip(fbm(n, 3, 3, seed + 5) * 2 - 0.7, 0, 1)
    mica = (rng.random((n, n)) > 0.992).astype(np.float32)
    mica = blur(mica, 0.6)
    tone = 0.10 + 0.07 * big + 0.05 * (grain - 0.5) + 0.04 * (fine - 0.5)
    tone *= 1.0 - 0.18 * np.clip((rr - 0.85) / 0.15, 0, 1)
    tone *= 1.0 - 0.6 * crack
    stone = np.dstack([tone * 0.95, tone * 0.97, tone * 1.05]) * 255
    stone += np.array([150, 150, 160], np.float32) * mica[..., None] * 0.35
    # the gold's warmth on the stone near the lines and the heart
    warm = blur(lines, 6) * 0.6 + np.exp(-(rr / 0.28) ** 2) * 0.35
    stone += GOLD * (warm * 0.10)[..., None]
    # gold dust: sparks scattered through the grain, gathered near the rings and toward the heart
    dust_p = (rng.random((n, n)) > 0.995).astype(np.float32) * (0.25 + 0.75 * np.clip(blur(lines, 14) * 3, 0, 1))
    dust = np.clip(blur(dust_p, 0.7) * 3.5, 0, 1)
    # the inlay: gold, brighter in the middle of each line, hottest in the sunburst
    core = np.clip(blur(lines, 0.6) * 1.2, 0, 1)
    gold = GOLD[None, None] * (0.75 + 0.25 * core[..., None]) + (GOLD_HOT - GOLD)[None, None] * (B[..., None] * 0.8)
    diff = stone * (1 - lines[..., None]) + gold * lines[..., None]
    diff = diff * (1 - dust[..., None] * 0.8) + GOLD_HOT[None, None] * dust[..., None] * 0.8
    diff *= inside[..., None]
    # height: the lines a little sunk, beveled; the stone's grain
    h = 0.35 * big + 0.25 * grain + 0.15 * fine - 0.8 * blur(lines, 1.2) - 0.4 * crack
    gy, gx = np.gradient(blur(h, 0.8) * 5.0)
    nx, ny = -gx, -gy
    nz = np.ones_like(nx)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    normal = np.dstack([(nx / ln) * 0.5 + 0.5, (ny / ln) * 0.5 + 0.5, (nz / ln) * 0.5 + 0.5]) * 255
    # glow: the gold lines (and the burst, brighter), a soft halo round them, the dust
    gl = np.clip(L * 0.85 + B * 1.0 + blur(lines, 3) * 0.35 + blur(B, 18) * 0.5 + dust * 0.9, 0, 1) * inside
    glow = GOLD_HOT[None, None] * gl[..., None]
    img = lambda a: Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGB')
    return img(diff), img(normal), img(glow), Image.fromarray((np.clip(lines * inside, 0, 1) * 255).astype(np.uint8), 'L')


if __name__ == '__main__':
    d, nm, g, _ = floor_maps(int(sys.argv[2]) if len(sys.argv) > 2 else 1024)
    out = np.asarray(d, np.float32) + np.asarray(g, np.float32) * 0.45
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(sys.argv[1] if len(sys.argv) > 1 else 'altar_preview.png')
