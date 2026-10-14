#!/usr/bin/env python3
"""The main menu's shrine scene, behind the shrine: a range of snowy mountains far across a still lake, northern
lights over them. The game moves it (src/Hud.cpp DrawMenu): the aurora's curtains sway and breathe, the lake shows
the mountains and the aurora upside down, its reflection wavering strip by strip as ripples run out toward the range.

    python3 tools/menulake.py assets/ui

Writes (2560x1440 painting space, the horizon - the far shore - at y 800):
  menu_mountains.png   the ranges (alpha: clear above the peaks)
  menu_lake_refl.png   the ranges upside down in the lake under the horizon (2560x640, alpha)
  menu_aurora2.png     the northern lights: two curtains (alpha), drawn twice out of step by the game
"""
import math, pathlib, sys
import numpy as np
from PIL import Image, ImageFilter

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
    img = rgba(rgb, a)
    img.save(OUT / 'menu_mountains.png', optimize=True)
    return img


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
    sky = Image.open(OUT / 'menu_sky.jpg').convert('RGBA').resize((W, H))
    sky.alpha_composite(Image.open(OUT / 'menu_aurora2.png'))
    sky.alpha_composite(m)
    lake = Image.new('RGBA', (W, 640), (12, 17, 34, 255))
    lake.alpha_composite(Image.open(OUT / 'menu_lake_refl.png'))
    sky.paste(lake, (0, HORIZON))
    sky.convert('RGB').resize((1280, 720)).save(OUT / 'lake_preview.jpg', quality=88)
    print('menulake: wrote menu_mountains.png, menu_lake_refl.png, menu_aurora2.png')
