"""Undying Nights main menu, painted from scratch (no game captures): a matte painting in layers that the menu animates.

    python tools/menupaint.py <out dir> [preview.jpg]

Layers (2560x1440):
  menu_sky.jpg     night sky: deep blue to near-black, stars, the Blood Moon and its halo, cloud banks lit by it
  menu_clouds.png  a thin drifting cloud veil (alpha), slid slowly across the moon
  menu_aurora.png  a faint aurora band (alpha) that shimmers
  menu_far.png     the far range: ridged snow peaks lit from the moon side, fading into haze (alpha)
  menu_mid.png     the barrow ridge: a ruined Nordic arch and standing stones, the horde coming over the crest (alpha)
  menu_near.png    the foreground: the shrine on its plinth, leaning burial stones, a dead tree, the cliff the menu sits on (alpha)
Everything is computed: height fields with lighting for the mountains, fbm clouds lit toward the moon, silhouettes drawn as
supersampled polygons with moon rim light and depth haze.
"""
import math, pathlib, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 2560, 1440
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'assets/ui')
PREVIEW = sys.argv[2] if len(sys.argv) > 2 else None
R = np.random.default_rng(1201)
MOON = (0.255 * W, 0.315 * H)        # the Blood Moon, upper left (the title sits top centre, the menu on the right)
MOON_R = 0.064 * H
HAZE = np.array([0.20, 0.23, 0.31])  # the cold night haze between the layers
MOONLIGHT = np.array([1.0, 0.62, 0.48])


def fbm(h, w, octaves=6, base=4, seed=0, gain=0.5, ridged=False):
    r = np.random.default_rng(seed)
    acc = np.zeros((h, w), np.float32); amp = 1.0; tot = 0.0
    for o in range(octaves):
        n = base * 2 ** o
        g = r.random((max(2, int(n * h / w)) + 3, n + 3)).astype(np.float32)
        layer = np.asarray(Image.fromarray(g).resize((w + int(w / n) * 2, h + int(h / max(2, n * h / w)) * 2), Image.BICUBIC))[:h, :w]
        if ridged:
            layer = 1.0 - np.abs(layer * 2 - 1)
            layer = layer ** 2
        acc += layer * amp; tot += amp; amp *= gain
    return acc / tot


def grid():
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    return xx, yy


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def to_img(rgb, a=None):
    rgb = np.clip(rgb, 0, 1)
    if a is None:
        return Image.fromarray((rgb * 255).astype(np.uint8), 'RGB')
    return Image.fromarray((np.dstack([rgb, np.clip(a, 0, 1)[..., None]]) * 255).astype(np.uint8), 'RGBA')


# ------------------------------------------------------------------------------------------------------------- sky
def sky():
    xx, yy = grid()
    v = yy / H
    top = np.array([0.012, 0.016, 0.034]); mid = np.array([0.045, 0.055, 0.095]); low = np.array([0.11, 0.10, 0.13])
    col = top + (mid - top) * smooth(0.0, 0.55, v)[..., None] + (low - mid) * smooth(0.45, 0.85, v)[..., None]
    # moon glow: a wide red-orange bloom, a tight halo ring
    d = np.sqrt((xx - MOON[0]) ** 2 + (yy - MOON[1]) ** 2) / MOON_R
    bloom = np.exp(-((d - 0.0) / 9.0) ** 2) * 0.55 + np.exp(-((d - 0.0) / 3.2) ** 2) * 0.5
    ring = np.exp(-((d - 2.35) / 0.18) ** 2) * 0.08
    col = col + (bloom[..., None] * np.array([0.33, 0.10, 0.06])) + ring[..., None] * np.array([0.9, 0.5, 0.4])
    # stars, fewer near the moon and the horizon
    st = R.random((H, W))
    mask = (st > 0.9993) * smooth(0.75, 0.2, v) * smooth(3.0, 9.0, d)
    stars = np.asarray(Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6)), np.float32) / 255 * 2.2
    tw = R.random((H, W)) > 0.99985
    big = np.asarray(Image.fromarray((tw * smooth(0.7, 0.2, v) * smooth(4, 10, d) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.4)), np.float32) / 255 * 5
    col = col + (stars + big)[..., None] * np.array([0.85, 0.9, 1.0])
    # cloud banks: fbm, thicker low, lit on the moon side (simple self-shadow: compare with a copy shifted toward the moon)
    n = fbm(H, W, 7, 3, seed=5)
    n2 = fbm(H, W, 5, 6, seed=9)
    dens = smooth(0.48, 0.78, n * 0.75 + n2 * 0.35) * (0.35 + 0.65 * smooth(0.15, 0.7, v)) * smooth(1.6, 3.5, d)
    sx, sy = (xx - MOON[0]), (yy - MOON[1]); L = np.sqrt(sx ** 2 + sy ** 2) + 1
    shift = np.roll(np.roll(dens, -6, 1), -6, 0)
    lit = np.clip((dens - shift) * 6 + 0.35, 0, 1)
    near = np.exp(-(L / (MOON_R * 9)) ** 2)
    cloud = np.array([0.05, 0.05, 0.08]) + lit[..., None] * (np.array([0.10, 0.08, 0.11]) + near[..., None] * np.array([0.55, 0.22, 0.14]))
    col = col * (1 - dens[..., None] * 0.85) + cloud * dens[..., None] * 0.85
    # the moon itself: a crimson disc with mottled maria, limb darkening, a soft edge
    m = np.clip(1.2 - d, 0, 1); disc = smooth(1.02, 0.98, d)
    mar = fbm(H, W, 5, 24, seed=21)
    face = np.array([0.95, 0.42, 0.26]) * (0.72 + 0.28 * smooth(0.35, 0.65, mar))[..., None] * (0.55 + 0.45 * np.sqrt(np.clip(1 - d ** 2, 0, 1)))[..., None]
    face = face + np.array([0.25, 0.15, 0.1]) * smooth(0.6, 0.0, d)[..., None]
    col = col * (1 - disc[..., None]) + face * disc[..., None]
    # a thin cloud bar across the moon's lower half
    bar = smooth(0.62, 0.8, fbm(H, W, 5, 10, seed=33)) * np.exp(-((yy - (MOON[1] + MOON_R * 0.35)) / (MOON_R * 0.25)) ** 2) * smooth(2.2, 0.0, d)
    col = col * (1 - 0.7 * bar[..., None]) + np.array([0.22, 0.07, 0.05]) * 0.7 * bar[..., None]
    return col


def clouds_veil():
    xx, yy = grid(); v = yy / H
    n = fbm(H, W, 6, 5, seed=41)
    a = smooth(0.55, 0.75, n) * smooth(0.0, 0.25, v) * smooth(0.75, 0.4, v) * 0.55
    d = np.sqrt((xx - MOON[0]) ** 2 + (yy - MOON[1]) ** 2) / MOON_R
    rgb = np.array([0.10, 0.09, 0.12]) + np.exp(-(d / 8) ** 2)[..., None] * np.array([0.45, 0.18, 0.12])
    return rgb * np.ones((H, W, 1)), a


def aurora():
    xx, yy = grid()
    u = xx / W
    cy = 0.16 + 0.05 * np.sin(u * 5.0 + 1.0) + 0.025 * np.sin(u * 13.0)
    band = np.exp(-(((yy / H) - cy) / 0.045) ** 2)
    curtain = (0.55 + 0.45 * fbm(H, W, 4, 40, seed=51)) * smooth(0.1, 0.5, u) * smooth(1.0, 0.65, u)
    rays = np.clip((cy - yy / H + 0.09) / 0.14, 0, 1) * (0.5 + 0.5 * np.sin(u * 180 + fbm(H, W, 3, 8, seed=52) * 12))
    a = np.clip(band * curtain * 0.45 + band * rays * 0.25, 0, 1)
    rgb = np.dstack([np.full((H, W), 0.35), np.full((H, W), 1.0), np.full((H, W), 0.75)])
    return rgb, a


# ------------------------------------------------------------------------------------------------------- mountains
def range_layer(seed, base_y, amp, octs, rough, haze, snow=0.0, ridged=True, xscale=1.0):
    """a mountain range as a height profile with a lit rock/snow face (light from the moon, up-left)"""
    xx, yy = grid()
    # the profile: ridged noise along x
    prof = fbm(64, W, octs, int(3 * xscale), seed=seed, ridged=ridged)[32]
    prof = (prof - prof.min()) / (np.ptp(prof) + 1e-6)
    top = base_y - prof * amp
    a = smooth(top - 1.5, top + 1.5, yy)
    # surface detail: ridged noise as a height field, its x-gradient as the light
    hf = fbm(H, W, 7, int(12 * xscale), seed=seed + 1, ridged=True)
    gx = np.gradient(hf, axis=1) * 60 + np.gradient(hf, axis=0) * 25
    light = np.clip(0.45 - gx * rough, 0, 1)
    depth = np.clip((yy - top) / (amp * 1.2 + 1), 0, 1)
    rock = np.array([0.075, 0.08, 0.10]) * (0.55 + 0.9 * light)[..., None]
    if snow > 0:
        sn = smooth(0.45, 0.6, hf + (1 - depth) * snow * 0.6) * (1 - depth) ** 1.5
        rock = rock * (1 - sn[..., None]) + (np.array([0.42, 0.44, 0.52]) * (0.35 + 0.95 * light)[..., None] + np.array([0.12, 0.04, 0.02]) * light[..., None]) * sn[..., None]
    # rim light along the crest from the moon side
    rim = np.exp(-((yy - top) / 3.0) ** 2) * smooth(0.0, 1.0, light)
    rock = rock + rim[..., None] * np.array([0.35, 0.16, 0.12])
    # haze: stronger lower down (fog pooling in the valleys)
    hz = np.clip(haze + depth * 0.35, 0, 0.95)
    rgb = rock * (1 - hz[..., None]) + HAZE * hz[..., None]
    return rgb, a, top


# ------------------------------------------------------------------------------------------------ silhouettes (2x)
S = 2


class Canvas:
    def __init__(self):
        self.im = Image.new('L', (W * S, H * S), 0)
        self.d = ImageDraw.Draw(self.im)

    def poly(self, pts, v=255):
        self.d.polygon([(x * S, y * S) for x, y in pts], fill=v)

    def line(self, pts, w, v=255):
        p = [(x * S, y * S) for x, y in pts]
        self.d.line(p, fill=v, width=max(1, int(w * S)), joint='curve')
        r = w * S / 2
        for x, y in (p[0], p[-1]):
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=v)

    def ell(self, cx, cy, rx, ry, v=255):
        self.d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S], fill=v)

    def mask(self):
        return np.asarray(self.im.resize((W, H), Image.LANCZOS), np.float32) / 255


def figure(c, x, y, h, pose, rng):
    """an undead silhouette, feet at (x, y), height h. pose: 0 shamble, 1 reaching, 2 hunched runner, 3 crawler, 4 tall robed"""
    u = h / 100.0
    lean = rng.uniform(-6, 10) * u
    if pose == 3:  # crawling along the ground
        c.line([(x - 30 * u, y - 4 * u), (x + 10 * u, y - 14 * u)], 13 * u)
        c.ell(x + 18 * u, y - 18 * u, 7 * u, 8 * u)
        c.line([(x + 6 * u, y - 13 * u), (x + 26 * u, y - 2 * u)], 5 * u)
        c.line([(x - 8 * u, y - 10 * u), (x + 2 * u, y)], 5 * u)
        c.line([(x - 28 * u, y - 5 * u), (x - 48 * u, y - 2 * u)], 6 * u)
        return
    hip = (x + lean * 0.3, y - 48 * u)
    nk = (x + lean, y - 84 * u) if pose != 2 else (x + lean + 14 * u, y - 74 * u)
    if pose == 4:  # a tall robed figure (the lich, a necromancer): a long cloak to the ground
        c.poly([(x - 16 * u, y), (x + 18 * u, y), (nk[0] + 9 * u, nk[1] + 6 * u), (nk[0] - 9 * u, nk[1] + 6 * u)])
        c.poly([(nk[0] - 8 * u, nk[1] + 2 * u), (nk[0] + 8 * u, nk[1] + 2 * u), (nk[0] + 5 * u, nk[1] - 16 * u), (nk[0], nk[1] - 22 * u), (nk[0] - 6 * u, nk[1] - 15 * u)])
        c.line([nk, (nk[0] + 26 * u, nk[1] - 4 * u), (nk[0] + 30 * u, nk[1] - 30 * u)], 4.5 * u)  # an arm raised
        c.line([(nk[0] + 32 * u, nk[1] - 52 * u), (nk[0] + 24 * u, y)], 2.4 * u)                # a staff
        return
    # legs: one planted, one dragging
    st = rng.uniform(10, 18) * u
    knee1 = (hip[0] - st * 0.4, y - 24 * u); knee2 = (hip[0] + st * 0.5, y - 25 * u)
    c.line([hip, knee1, (x - st, y)], 8 * u)
    c.line([hip, knee2, (x + st * 0.9, y - 1 * u)], 8 * u)
    c.line([(x - st - 2 * u, y), (x - st + 9 * u, y)], 4 * u)
    # torso, a little bent
    c.line([hip, ((hip[0] + nk[0]) / 2 + 3 * u, (hip[1] + nk[1]) / 2), nk], 16 * u)
    # head, drooped forward
    hd = (nk[0] + 5 * u, nk[1] - 9 * u) if pose != 2 else (nk[0] + 10 * u, nk[1] - 4 * u)
    c.ell(hd[0], hd[1], 7.5 * u, 8.5 * u)
    # arms
    sh = (nk[0], nk[1] + 4 * u)
    if pose == 1:   # reaching forward
        c.line([sh, (sh[0] + 22 * u, sh[1] + 2 * u), (sh[0] + 44 * u, sh[1] - 6 * u)], 6 * u)
        c.line([sh, (sh[0] + 18 * u, sh[1] + 10 * u), (sh[0] + 38 * u, sh[1] + 6 * u)], 6 * u)
        for f in range(3):
            c.line([(sh[0] + 44 * u, sh[1] - 6 * u), (sh[0] + 50 * u, sh[1] - 9 * u + f * 3 * u)], 1.6 * u)
    elif pose == 2:  # a runner: arms swept back
        c.line([sh, (sh[0] - 14 * u, sh[1] + 14 * u), (sh[0] - 28 * u, sh[1] + 10 * u)], 5.5 * u)
        c.line([sh, (sh[0] + 12 * u, sh[1] + 16 * u), (sh[0] + 24 * u, sh[1] + 26 * u)], 5.5 * u)
    else:            # hanging, swinging
        c.line([sh, (sh[0] - 6 * u, sh[1] + 20 * u), (sh[0] - 2 * u, sh[1] + 40 * u)], 6 * u)
        c.line([sh, (sh[0] + 9 * u, sh[1] + 19 * u), (sh[0] + 16 * u, sh[1] + 37 * u)], 6 * u)
    # rags hanging from the waist, a torn edge
    for i in range(4):
        rx = hip[0] - 9 * u + i * 6 * u
        c.line([(rx, hip[1] - 2 * u), (rx + rng.uniform(-2, 2) * u, hip[1] + rng.uniform(10, 18) * u)], 3 * u)


def shade_silhouette(mask, base, rim_col, rim_w, haze, seed):
    """fill a silhouette mask: near-black body with texture, a moon-side rim (left/up), depth haze"""
    tex = fbm(H, W, 5, 60, seed=seed)
    body = base * (0.75 + 0.5 * tex)[..., None]
    # rim: where the mask edge faces the moon (up-left)
    sh = np.roll(np.roll(mask, rim_w, 1), rim_w, 0)
    rim = np.clip(mask - sh, 0, 1)
    rgb = body + rim[..., None] * rim_col
    rgb = rgb * (1 - haze) + HAZE * haze
    return rgb


def stone_slab(c, pts):
    c.poly(pts)


def mid_layer(far_top):
    """the barrow ridge: a ruined arch, standing stones, the horde coming over the crest and down the slope"""
    xx, yy = grid()
    # the ridge: a rise from the right to the middle, the slope coming toward the viewer on the left
    prof = fbm(64, W, 6, 4, seed=71)[32]
    u = np.arange(W) / W
    ridge = H * (0.60 - 0.13 * np.exp(-((u - 0.62) / 0.22) ** 2) - 0.03 * np.sin(u * 4)) - (prof - 0.5) * 50
    ground = smooth(ridge[None, :] - 1.5, ridge[None, :] + 1.5, yy)
    c = Canvas()
    # the arch on the crest (two leaning pillars and a lintel), the barrow's door between them
    ax = 0.635 * W; ay = float(ridge[int(ax)]) + 6
    c.poly([(ax - 130, ay), (ax - 92, ay), (ax - 98, ay - 300), (ax - 136, ay - 290)])
    c.poly([(ax + 88, ay), (ax + 128, ay), (ax + 140, ay - 296), (ax + 100, ay - 304)])
    c.poly([(ax - 160, ay - 286), (ax + 168, ay - 300), (ax + 160, ay - 342), (ax - 150, ay - 330)])
    c.poly([(ax - 40, ay - 342), (ax + 50, ay - 346), (ax + 30, ay - 390), (ax - 22, ay - 386)])  # a broken capstone
    for sx, hgt, wd, tilt in [(-330, 170, 46, -8), (-250, 120, 40, 6), (260, 150, 44, 10), (360, 210, 52, -5), (470, 110, 38, 14), (-460, 90, 34, -12)]:
        bx = ax + sx; by = float(ridge[int(min(W - 1, max(0, bx)))]) + 4
        c.poly([(bx - wd / 2, by), (bx + wd / 2, by), (bx + wd / 2 + tilt, by - hgt), (bx - wd / 2 + tilt * 0.6, by - hgt * 0.94)])
    stones = c.mask()
    # the horde: out of the barrow, over the crest, down the slope toward the viewer, growing as they come
    hc = Canvas(); rng = np.random.default_rng(13)
    figs = []
    for i in range(70):
        t = rng.random() ** 0.8                     # 0 at the barrow door, 1 down the slope
        fx = ax - 40 + rng.normal(0, 30) - t * (0.48 * W) + rng.normal(0, 60) * t
        fx = float(np.clip(fx, 40, W - 40))
        fy = float(ridge[int(fx)]) + 4 + t * rng.uniform(10, 70)
        hgt = 34 + t * 110 + rng.normal(0, 6)
        figs.append((fy, fx, hgt, rng.choice([0, 0, 1, 1, 2, 3, 4], p=[0.25, 0.15, 0.2, 0.15, 0.13, 0.05, 0.07])))
    for fy, fx, hgt, pose in sorted(figs):
        figure(hc, fx, fy, hgt, pose, rng)
    horde = hc.mask()
    a = np.clip(ground + stones + horde, 0, 1)
    # shading: ground with snow, stones and horde as silhouettes with a red rim, everything in the haze
    depth = np.clip((yy - ridge[None, :]) / 300, 0, 1)
    sn = fbm(H, W, 6, 20, seed=77)
    gcol = np.array([0.12, 0.13, 0.17]) * (0.6 + 0.6 * sn)[..., None] * (1 - depth[..., None] * 0.4)
    grim = np.exp(-((yy - ridge[None, :]) / 3.5) ** 2)[..., None] * np.array([0.30, 0.12, 0.09])
    gcol = gcol + grim
    scol = shade_silhouette(stones, np.array([0.035, 0.035, 0.045]), np.array([0.38, 0.15, 0.11]), 3, 0.42, 79)
    hcol = shade_silhouette(horde, np.array([0.02, 0.02, 0.026]), np.array([0.42, 0.14, 0.10]), 2, 0.30, 81)
    rgb = gcol * (1 - stones[..., None]) + scol * stones[..., None]
    rgb = rgb * (1 - horde[..., None]) + hcol * horde[..., None]
    hz = np.clip(0.42 - depth * 0.25, 0.1, 1)
    rgb = rgb * (1 - hz[..., None] * (1 - horde[..., None] * 0.4)) + HAZE * (hz[..., None] * (1 - horde[..., None] * 0.4))
    return rgb, a, ridge, (ax, ay)


def near_layer():
    """the foreground: the shrine (rune pillar on a stepped plinth), burial stones, a dead tree, the cliff on the right"""
    xx, yy = grid(); u = xx / W
    c = Canvas()
    # ground: a low rise across the bottom, higher to the right where the cliff stands
    gy = H * (0.86 - 0.05 * smooth(0.55, 1.0, np.arange(W) / W)) + (fbm(64, W, 5, 6, seed=91)[32] - 0.5) * 40
    gpts = [(0, H)] + [(x, float(gy[x])) for x in range(0, W, 8)] + [(W, H)]
    c.poly(gpts)
    # the cliff on the right: layered slabs (the menu's dark ground)
    rng = np.random.default_rng(5)
    base_x = 0.70 * W
    c.poly([(base_x + 120, H), (base_x + 40, H * 0.80), (base_x + 180, H * 0.62), (base_x + 420, H * 0.50), (W, H * 0.40), (W, H)])
    for i in range(7):
        y0 = H * (0.50 + 0.065 * i) + rng.normal(0, 8)
        x0 = base_x + 150 - i * 30 + rng.normal(0, 20)
        c.poly([(x0, y0 + 30), (x0 + 60, y0 - 6), (W + 10, y0 - 40 - i * 4), (W + 10, y0 + 46), (x0 + 30, y0 + 52)])
    # the shrine: stepped plinth and a tall carved rune pillar, a crown of horns at the top (left of centre)
    sx, sy = 0.40 * W, float(gy[int(0.40 * W)]) + 8
    c.poly([(sx - 150, sy), (sx + 150, sy), (sx + 130, sy - 34), (sx - 130, sy - 34)])
    c.poly([(sx - 110, sy - 34), (sx + 110, sy - 34), (sx + 92, sy - 64), (sx - 92, sy - 64)])
    c.poly([(sx - 44, sy - 64), (sx + 44, sy - 64), (sx + 36, sy - 330), (sx - 36, sy - 330)])
    c.poly([(sx - 54, sy - 330), (sx + 54, sy - 330), (sx + 40, sy - 356), (sx - 40, sy - 356)])
    for side in (-1, 1):  # horns curling up from the crown
        pts = []
        for t in np.linspace(0, 1, 14):
            ang = math.pi * (0.5 + side * (0.15 + 0.9 * t))
            r = 44 + 30 * t
            pts.append((sx + side * 10 + math.cos(ang) * r * 0.7 * side * -1, sy - 352 - math.sin(math.pi * 0.5 * t) * 90 * (1 - 0.3 * t)))
        c.line(pts, 14 - 8 * 0, 255)
    # burial stones round it, leaning, some with Nordic knot tops
    for gx_, hgt, wd, tilt in [(0.20, 120, 60, -14), (0.27, 90, 50, 9), (0.53, 110, 56, 12), (0.60, 80, 46, -10), (0.12, 150, 70, 6), (0.07, 70, 44, -18), (0.33, 60, 40, 15)]:
        bx = gx_ * W; by = float(gy[int(bx)]) + 10
        c.poly([(bx - wd / 2, by), (bx + wd / 2, by), (bx + wd / 2 + tilt, by - hgt), (bx + tilt * 0.5, by - hgt - wd * 0.45), (bx - wd / 2 + tilt, by - hgt)])
    # a dead tree on the left edge, its branches clawing over the sky
    def branch(x, y, ang, ln, w, depth):
        if depth == 0 or ln < 8:
            return
        x2 = x + math.cos(ang) * ln; y2 = y - math.sin(ang) * ln
        mx = (x + x2) / 2 + rng.normal(0, ln * 0.08); my = (y + y2) / 2 + rng.normal(0, ln * 0.08)
        c.line([(x, y), (mx, my), (x2, y2)], w)
        for _ in range(2 if depth > 2 else rng.integers(1, 3)):
            branch(x2, y2, ang + rng.normal(0, 0.55), ln * rng.uniform(0.6, 0.78), w * 0.68, depth - 1)
    tx = 0.045 * W; ty = float(gy[int(tx)]) + 10
    c.poly([(tx - 40, ty), (tx + 44, ty), (tx + 18, ty - 360), (tx - 6, ty - 360)])
    branch(tx + 6, ty - 350, math.radians(80), 200, 26, 7)
    branch(tx + 6, ty - 300, math.radians(35), 230, 18, 6)
    branch(tx, ty - 260, math.radians(140), 140, 14, 5)
    m = c.mask()
    # the shrine on its own (for the glow and a slightly lighter carving)
    sc = Canvas()
    sc.poly([(sx - 44, sy - 64), (sx + 44, sy - 64), (sx + 36, sy - 330), (sx - 36, sy - 330)])
    shrine = sc.mask()
    rgb = shade_silhouette(m, np.array([0.022, 0.022, 0.028]), np.array([0.30, 0.11, 0.08]), 3, 0.06, 93)
    # carved runes down the pillar: faint violet cuts (the glow itself is animated by the menu)
    runes = np.zeros((H, W), np.float32)
    rr = Canvas()
    for i in range(9):
        cy = sy - 90 - i * 26
        kind = i % 3
        if kind == 0:
            rr.line([(sx, cy - 9), (sx, cy + 9)], 3); rr.line([(sx, cy - 4), (sx + 8, cy - 10)], 3)
        elif kind == 1:
            rr.line([(sx - 7, cy + 9), (sx, cy - 9), (sx + 7, cy + 9)], 3)
        else:
            rr.line([(sx, cy - 9), (sx, cy + 9)], 3); rr.line([(sx - 7, cy), (sx + 7, cy)], 3)
    runes = rr.mask()
    rgb = rgb + runes[..., None] * np.array([0.45, 0.22, 0.75])
    return rgb, m, (sx, sy - 343), gy


def compose(layers):
    out = layers[0][0].copy()
    for rgb, a in layers[1:]:
        out = out * (1 - a[..., None]) + rgb * a[..., None]
    return out


def grain(rgb, s=1.4):
    return np.clip(rgb + R.normal(0, s / 255, rgb.shape), 0, 1)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sk = sky()
    to_img(grain(sk)).save(OUT / 'menu_sky.jpg', quality=92)
    cv, ca = clouds_veil(); to_img(cv, ca).save(OUT / 'menu_clouds.png')
    ar, aa = aurora(); to_img(ar, aa).resize((1280, 720), Image.LANCZOS).save(OUT / 'menu_aurora.png')
    far_rgb, far_a, far_top = range_layer(61, H * 0.60, H * 0.25, 7, 1.0, 0.38, snow=1.0)
    farther_rgb, farther_a, _ = range_layer(62, H * 0.52, H * 0.20, 6, 0.8, 0.62, snow=1.0, xscale=1.6)
    # the farther range sits behind the far one: bake them into one layer
    fa = np.clip(far_a + farther_a * (1 - far_a), 0, 1)
    frgb = (farther_rgb * farther_a[..., None] * (1 - far_a[..., None]) + far_rgb * far_a[..., None]) / np.maximum(fa[..., None], 1e-4)
    to_img(grain(frgb), fa).save(OUT / 'menu_far.png')
    mid_rgb, mid_a, ridge, arch = mid_layer(far_top)
    to_img(grain(mid_rgb), mid_a).save(OUT / 'menu_mid.png')
    near_rgb, near_a, crown, gy = near_layer()
    to_img(grain(near_rgb), near_a).save(OUT / 'menu_near.png')
    print('crown (shrine glow) at', crown, '| arch at', arch)
    if PREVIEW:
        fog = HAZE * np.ones((H, W, 3))
        yy = grid()[1] / H
        fa1 = smooth(0.5, 0.62, yy) * smooth(0.75, 0.62, yy) * 0.35
        img = compose([(sk, None), (ar * 0 + np.array([0.35, 1.0, 0.75]), aa * 0.35), (frgb, fa), (fog, fa1), (mid_rgb, mid_a), (near_rgb, near_a)])
        Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).resize((1600, 900), Image.LANCZOS).save(PREVIEW, quality=90)


if __name__ == '__main__':
    main()
