#!/usr/bin/env python3
"""The main menu's shrine scene, behind the shrine: a range of snowy mountains far across a still lake, northern
lights over them. The game moves it (src/Hud.cpp DrawMenu): the aurora's curtains sway and breathe, the lake shows
the mountains and the aurora upside down, its reflection wavering strip by strip as ripples run out toward the range.

    python3 tools/menulake.py assets/ui

Writes (2560x1440 painting space, the horizon - the far shore - at y 800):
  menu_mountains.png   the ranges (alpha: clear above the peaks)
  menu_lake_refl.png   the ranges upside down in the lake under the horizon (2560x640, alpha)
  menu_aurora2.png     the northern lights: two curtains (alpha), drawn twice out of step by the game
  menu_lake_life.png   life on the lake: lily pads (a few in flower) floating on the water, reeds and bushes in the
                       near corners (2560x1440, alpha) - the game rocks it gently on the water
The far shore carries a line of snowy pines in groves.
"""
import math, pathlib, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'menulake')
OUT.mkdir(parents=True, exist_ok=True)
W, H = 2560, 1440
HORIZON = 800


def fbm1(n, oct=7, base=3, seed=0, pers=0.5):
    rng = np.random.default_rng(seed)
    out, amp, tot = np.zeros(n, np.float32), 1.0, 0.0
    x = np.linspace(0, 1, n, dtype=np.float32)
    for o in range(oct):
        k = base * 2 ** o
        g = rng.random(k + 2).astype(np.float32)
        out += np.interp(x * k, np.arange(k + 2), g) * amp
        tot += amp
        amp *= pers
    return out / tot


def fbm2(w, h, oct=6, base=4, seed=0, pers=0.55):
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(oct):
        n = base * 2 ** o
        g = rng.random((n + 1, int(n * w / h) + 2)).astype(np.float32)
        out += np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC), np.float32) / 255 * amp
        tot += amp
        amp *= pers
    return out / tot


def blur(m, r):
    return np.asarray(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)), np.float32) / 255


def rgba(rgb, a):
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), np.clip(a, 0, 1) * 255]).astype(np.uint8), 'RGBA')


def ridge(seed, top, amp, sharp):
    """A mountain profile: y of the crest per column (sharp peaks: ridged noise)."""
    n = fbm1(W, 8, 3, seed)
    r = 1 - np.abs(2 * fbm1(W, 7, 2, seed + 50) - 1)  # ridged: peaks where the noise crosses its middle
    prof = 0.55 * n + 0.45 * r ** sharp
    prof = (prof - prof.min()) / (prof.max() - prof.min())
    return top + amp * (1 - prof)


def mountains():
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    rgb = np.zeros((H, W, 3), np.float32)
    a = np.zeros((H, W), np.float32)
    rock = fbm2(W, H, 7, 6, seed=31)
    streak = fbm2(W, H, 5, 3, seed=32)
    # far to near: (seed, crest top, crest range, sharpness, rock colour, snow colour, haze)
    ranges = [(11, 360, 320, 3.0, (40, 49, 76), (200, 214, 238), 0.42, 0.30),
              (12, 520, 220, 2.4, (24, 29, 46), (150, 166, 200), 0.22, 0.20),
              (13, 660, 120, 1.6, (11, 14, 24), (80, 92, 120), 0.08, 0.10)]
    for seed, top, amp, sharp, rc, sc, haze, snowshare in ranges:
        crest = ridge(seed, top, amp, sharp)
        inside = yy >= crest[None, :]
        depth = np.clip((yy - crest[None, :]) / (HORIZON - crest[None, :] + 1), 0, 1)   # 0 at the crest, 1 at the shore
        # facets: a rocky heightfield whose slope toward the moon (up-left) lights a face
        T = blur(fbm2(W, H, 6, 5, seed=seed + 100), 3.0)
        gx = np.gradient(T, axis=1) * 220 + np.gradient(crest)[None, :] * -0.12
        lit = np.clip(0.5 + 0.5 * np.tanh(-gx * 2.0), 0, 1)
        lit = 0.25 + 0.75 * lit
        snowline = snowshare * (0.6 + 0.8 * fbm1(W, 6, 5, seed + 7))[None, :]
        snow = np.clip((snowline - depth) * 14 + (T - 0.5) * 2.0, 0, 1) * np.clip(lit * 1.4 - 0.2, 0, 1)
        base = np.array(rc, np.float32)[None, None] * (0.75 + 0.5 * rock[..., None]) * (0.55 + 0.9 * lit[..., None])
        snc = np.array(sc, np.float32)[None, None] * (0.45 + 0.7 * lit[..., None])
        col = base * (1 - snow[..., None]) + snc * snow[..., None]
        mist = np.clip((yy - (HORIZON - 90)) / 90.0, 0, 1) ** 1.5
        hz = np.array([52, 62, 94], np.float32)[None, None]
        col = col * (1 - haze) + hz * haze
        col = col * (1 - 0.6 * mist[..., None]) + hz * 0.6 * mist[..., None]
        m = inside.astype(np.float32)
        m = np.where(yy > HORIZON + 4, 0, m)
        edge = np.clip((yy - crest[None, :]) / 1.5, 0, 1)
        m = m * edge
        rgb = rgb * (1 - m[..., None]) + col * m[..., None]
        a = np.maximum(a, m)
    # the far shore: a thin dark line of trees and stones at the water
    shore = HORIZON - 6 - 10 * fbm1(W, 6, 40, 77)
    tm = ((yy >= shore[None, :]) & (yy <= HORIZON + 2)).astype(np.float32)
    rgb = rgb * (1 - tm[..., None]) + np.array([14, 17, 28])[None, None] * tm[..., None]
    a = np.maximum(a, tm)
    img = forest(rgba(rgb, a))
    img.save(OUT / 'menu_mountains.png', optimize=True)
    return img


def pine(d, x, y, h, rng, dark, snow):
    """a snowy pine: tiers of dark needles, each tier's upper edge heavy with moonlit snow"""
    tiers = max(3, int(h / 7))
    w = h * 0.36
    for k in range(tiers):
        t0 = k / tiers
        yb = y - h * t0 * 0.92
        wb = w * (1 - t0 * 0.85) * rng.uniform(0.85, 1.1)
        apex = yb - h * 0.34
        d.polygon([(x - wb, yb), (x + wb, yb), (x + rng.uniform(-0.4, 0.4), apex)], fill=dark)
        if rng.random() < 0.7:  # a dusting of snow on the tier's moonward (left) shoulder
            sw = wb * 0.55
            d.polygon([(x - sw, yb - h * 0.10), (x - sw * 0.1, yb - h * 0.13), (x, apex + h * 0.06)], fill=snow)
    d.line([(x, y), (x, y + h * 0.06)], fill=(20, 16, 18, 255), width=max(1, int(h / 18)))


def forest(img):
    """snowy pines along the lake's far shore only, in groves with gaps (drawn at 2x, then shrunk: soft edges)"""
    big = Image.new('RGBA', (W * 2, H * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    rng = np.random.default_rng(91)
    al = np.asarray(img)[..., 3]
    trees = []
    # the shore's tree line
    x = 0.0
    while x < W:
        grove = rng.random() < 0.75          # groves and gaps along the shore
        for _ in range(int(rng.integers(4, 30)) if grove else 0):
            trees.append((HORIZON - 3 + rng.uniform(-3, 2), x + rng.uniform(0, 60), rng.uniform(10, 34) * rng.uniform(0.7, 1.2)))
        x += rng.uniform(40, 110)
    for y, x, hgt in sorted(trees):  # far (higher) first
        haze = np.clip((HORIZON - y) / 240, 0, 1)
        dark = tuple(int(c * (1 - haze) + hc * haze) for c, hc in zip((9, 14, 19), (34, 42, 66))) + (255,)
        snow = tuple(int(c * (1 - haze * 0.6) + hc * haze * 0.6) for c, hc in zip((86, 98, 124), (50, 60, 88))) + (255,)
        pine(d, x * 2, y * 2, hgt * 2, rng, dark, snow)
    big = big.resize((W, H), Image.LANCZOS)
    out = img.copy()
    out.alpha_composite(big)
    return out


def lakelife():
    """lily pads floating on the dark water (bigger toward us, squashed by the angle we look at the lake from), a few
    white flowers among them; reeds and low bushes in the near corners. Kept off the shrine's floor."""
    big = Image.new('RGBA', (W * 2, H * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    rng = np.random.default_rng(57)
    floor = lambda x, y: ((x - 1160) / 820) ** 2 + ((y - 1190) / 250) ** 2 < 1.0
    pads = []
    clusters = [(170, 1040), (420, 905), (2120, 960), (2390, 1110), (1740, 862), (640, 860), (2300, 1360), (260, 1340), (1990, 1300)]
    for cx, cy in clusters:
        for _ in range(int(rng.integers(5, 11))):
            x = cx + rng.normal(0, 70 + (cy - 800) * 0.15)
            y = cy + rng.normal(0, 14 + (cy - 800) * 0.05)
            if y < 812 or y > 1430 or floor(x, y):
                continue
            pads.append((y, x))
    for y, x in sorted(pads):  # far first
        t = (y - 800) / 640
        r = 7 + 46 * t ** 1.15
        fl = 0.22 + 0.12 * t
        ang = rng.uniform(0, 360)
        notch = rng.uniform(18, 30)
        bx, by = x * 2, y * 2
        R, Rv = r * 2 * rng.uniform(0.8, 1.15), r * 2 * fl
        box = [bx - R, by - Rv, bx + R, by + Rv]
        green = (int(rng.uniform(22, 34)), int(rng.uniform(52, 70)), int(rng.uniform(42, 56)), 245)
        d.pieslice([box[0], box[1] + 3, box[2], box[3] + 3], ang + notch, ang + 360, fill=(4, 8, 14, 150))  # its shadow on the water
        d.pieslice(box, ang + notch, ang + 360, fill=green)
        d.arc(box, ang + notch + 120, ang + 300, fill=(110, 150, 140, 200), width=max(1, int(R / 22)))  # moonlight on its rim
        for k in range(5):  # veins
            a = math.radians(ang + notch + 30 + k * 62)
            d.line([(bx, by), (bx + math.cos(a) * R * 0.85, by + math.sin(a) * Rv * 0.85)], fill=(40, 78, 64, 160), width=1)
        if rng.random() < 0.18:  # in flower: a white water lily, its petals catching the moon
            fr = R * 0.32
            for k in range(8):
                a = math.radians(k * 45 + ang)
                px, py = bx + math.cos(a) * fr * 0.6, by + math.sin(a) * fr * 0.3 - fr * 0.25
                d.ellipse([px - fr * 0.45, py - fr * 0.3, px + fr * 0.45, py + fr * 0.3], fill=(225, 228, 245, 250))
            d.ellipse([bx - fr * 0.25, by - fr * 0.42, bx + fr * 0.25, by - fr * 0.18], fill=(240, 220, 140, 255))
    # reeds and bushes: the near corners, dark against the water, a little moonlight on their edges
    def reeds(x0, x1, ybase):
        for _ in range(140):
            x = rng.uniform(x0, x1)
            hgt = rng.uniform(90, 260) * (1.2 if abs(x - (x0 + x1) / 2) < (x1 - x0) * 0.25 else 0.85)
            lean = rng.uniform(-0.35, 0.35)
            pts = [(x * 2, ybase * 2)]
            for k in range(1, 7):
                f = k / 6
                pts.append(((x + lean * hgt * f * f) * 2, (ybase - hgt * f) * 2))
            col = (int(rng.uniform(14, 26)), int(rng.uniform(24, 36)), int(rng.uniform(22, 30)), 255)
            d.line(pts, fill=col, width=int(rng.uniform(4, 9)))
            if rng.random() < 0.5:
                d.line(pts[2:], fill=(70, 90, 110, 160), width=2)
            if rng.random() < 0.12:  # a cattail head
                hx, hy = pts[-2]
                d.ellipse([hx - 7, hy - 26, hx + 7, hy + 10], fill=(40, 28, 22, 255))
        for _ in range(26):  # low bushes at the reeds' feet
            x = rng.uniform(x0, x1)
            rr = rng.uniform(30, 70)
            y = ybase - rr * 0.3
            for _ in range(10):
                ox, oy = rng.normal(0, rr * 0.45), rng.normal(0, rr * 0.25)
                q = rr * rng.uniform(0.35, 0.6)
                c = int(rng.uniform(12, 24))
                d.ellipse([(x + ox - q) * 2, (y + oy - q * 0.8) * 2, (x + ox + q) * 2, (y + oy + q * 0.8) * 2], fill=(c, c + 14, c + 8, 255))
                d.ellipse([(x + ox - q * 0.6) * 2, (y + oy - q * 0.85) * 2, (x + ox + q * 0.2) * 2, (y + oy - q * 0.45) * 2], fill=(60, 78, 96, 120))
    reeds(-40, 330, 1450)
    reeds(2260, 2600, 1450)
    big.resize((W, H), Image.LANCZOS).save(OUT / 'menu_lake_life.png', optimize=True)



def reflection(mtn):
    """The ranges mirrored in the lake, under the horizon: darker and bluer, the nearer the darker (alpha: clear where
    the lake mirrors open sky - the game draws the sky's own reflection, stars and aurora, under it)."""
    m = np.asarray(mtn, np.float32)
    top = m[HORIZON - 640:HORIZON][::-1].copy()                 # mirror at the shore
    t = np.linspace(0, 1, 640, dtype=np.float32)[:, None, None]
    water = np.array([8, 12, 24], np.float32)[None, None]
    rgb = top[..., :3] * (0.72 - 0.30 * t) + water * (0.28 + 0.30 * t)
    rgb *= np.array([0.86, 0.94, 1.12])[None, None]
    im = rgba(rgb, top[..., 3] / 255).filter(ImageFilter.GaussianBlur(1.6))
    im.save(OUT / 'menu_lake_refl.png', optimize=True)


def aurora():
    """Curtains of light: a bright green hem low down, rays rising from it into teal and violet, fading out."""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    total = np.zeros((H, W, 3), np.float32)
    alpha = np.zeros((H, W), np.float32)
    wsum = np.zeros((H, W), np.float32)
    rays = fbm2(W, H, 4, 2, seed=61)
    fine = np.asarray(Image.fromarray((np.random.default_rng(62).random((8, W)) * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC), np.float32) / 255
    for k, (y0, amp, wl, ph, height, tilt) in enumerate([(450, 70, 900, 0.3, 380, -0.05), (310, 55, 1300, 2.1, 300, 0.04)]):
        hem = y0 + amp * np.sin(xx[0] / wl * 2 * math.pi + ph) + 40 * np.sin(xx[0] / 260 + ph * 3) + tilt * (xx[0] - W / 2)
        d = (hem[None, :] - yy) / height                              # 0 at the hem, 1 at the top of the rays
        dc = np.clip(d, -5, 5)
        up = np.where(dc >= 0, np.exp(-dc * 1.5) * (0.7 + 0.3 * np.exp(-dc * 18)), np.exp(dc * 11))         # rays above the hem, a sharp fade under it
        streaks = 0.45 + 0.55 * (0.6 * fine + 0.4 * rays)
        patch = fbm2(W, H, 3, 2, seed=70 + k)
        brightness = up * streaks * np.clip((patch - 0.25) * 2.4, 0.2, 1.0)
        green = np.array([70, 255, 160], np.float32)
        teal = np.array([60, 200, 255], np.float32)
        violet = np.array([170, 90, 255], np.float32)
        dd = np.clip(d, 0, 1)[..., None]
        col = green * (1 - dd) ** 2 + teal * 2 * dd * (1 - dd) + violet * dd ** 2
        total += col * brightness[..., None]
        wsum = wsum + brightness
        alpha = 1 - (1 - alpha) * (1 - brightness)
    alpha = blur(alpha, 2)
    rgb = np.clip(total / np.maximum(wsum[..., None], 1e-3), 0, 255)
    rgba(rgb, np.clip(alpha * 0.95, 0, 1)).save(OUT / 'menu_aurora2.png', optimize=True)


if __name__ == '__main__':
    m = mountains()
    reflection(m)
    aurora()
    lakelife()
    sky = Image.open(OUT / 'menu_sky.jpg').convert('RGBA').resize((W, H))
    sky.alpha_composite(Image.open(OUT / 'menu_aurora2.png'))
    sky.alpha_composite(m)
    lake = Image.new('RGBA', (W, 640), (12, 17, 34, 255))
    lake.alpha_composite(Image.open(OUT / 'menu_lake_refl.png'))
    sky.paste(lake, (0, HORIZON))
    sky.alpha_composite(Image.open(OUT / 'menu_lake_life.png'))
    sky.convert('RGB').resize((1280, 720)).save(OUT / 'lake_preview.jpg', quality=88)
    print('menulake: wrote menu_mountains.png, menu_lake_refl.png, menu_aurora2.png')
