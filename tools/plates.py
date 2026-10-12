"""Undying Nights: the intro film's stills and the main menu's picture, graded from scenes staged and captured in
Skyrim itself (assets/plates/*.jpg, 2560x1440 game captures with the DevTools camera; nothing is painted).

    python tools/plates.py <out dir>      writes intro_*.jpg (1920x1080), menu_plate.jpg (2560x1440) and menu_aurora.png

Each plate gets the Undying Nights grade: lifted but cold shadows, a little less saturation, split toning toward
charcoal-blue in the dark and bone in the light, a vignette and fine grain. Some get a scene treatment of their own:
Helgen's sky burns (the aurora is turned to fire and smoke), the shrine's crown is turned violet, the wyrm stands in fog.
"""
import pathlib, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / 'assets' / 'plates'
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'assets' / 'ui')
rng = np.random.default_rng(7)


def load(name):
    return np.asarray(Image.open(SRC / f'{name}.jpg').convert('RGB'), np.float32) / 255.0


def fbm(h, w, octaves=5, base=3, seed=0):
    r = np.random.default_rng(seed)
    acc = np.zeros((h, w), np.float32); amp = 1.0; tot = 0.0
    for o in range(octaves):
        n = base * 2 ** o
        g = r.random((max(2, int(n * h / w)) + 1, n + 1)).astype(np.float32)
        acc += np.asarray(Image.fromarray(g).resize((w, h), Image.BICUBIC)) * amp
        tot += amp; amp *= 0.55
    return acc / tot


def rgb2hsv(x):
    r, g, b = x[..., 0], x[..., 1], x[..., 2]
    mx, mn = x.max(-1), x.min(-1); d = mx - mn + 1e-6
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
    s = np.where(mx > 1e-6, d / (mx + 1e-6), 0)
    return h, s, mx


def hsv2rgb(h, s, v):
    c = v * s; hp = (h % 360) / 60; xx = c * (1 - np.abs(hp % 2 - 1)); z = np.zeros_like(h)
    conds = [(hp < 1), (hp < 2), (hp < 3), (hp < 4), (hp < 5), (hp >= 5)]
    vals = [(c, xx, z), (xx, c, z), (z, c, xx), (z, xx, c), (xx, z, c), (c, z, xx)]
    r = np.select(conds, [v_[0] for v_ in vals]); g = np.select(conds, [v_[1] for v_ in vals]); b = np.select(conds, [v_[2] for v_ in vals])
    m = v - c
    return np.stack([r + m, g + m, b + m], -1)


def grade(x, exposure=1.0, lift=0.85, sat=0.85, vignette=0.55, cold=1.0):
    x = np.clip(x * exposure, 0, 1) ** lift                       # lift the night so the figures read
    lum = x @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    x = lum[..., None] + (x - lum[..., None]) * sat               # a little less colour
    sh = (1 - lum)[..., None] ** 2; hl = lum[..., None] ** 2
    x = x + sh * np.array([-0.010, 0.002, 0.030]) * cold + hl * np.array([0.020, 0.012, -0.010])  # charcoal-blue shadows, bone lights
    x = 0.5 + (x - 0.5) * 1.08                                    # a touch of contrast
    h, w = x.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    x = x * (1 - vignette * np.clip(r - 0.45, 0, 1) ** 1.6)[..., None]
    x = x + rng.normal(0, 1.6 / 255, x.shape).astype(np.float32)   # grain
    return np.clip(x, 0, 1)


def burn_sky(x):
    """Helgen: the aurora becomes fire-light, the sky fills with smoke lit from below."""
    h, s, v = rgb2hsv(x)
    aur = np.clip((s - 0.25) * 3, 0, 1) * np.clip((v - 0.15) * 3, 0, 1) * ((h > 100) & (h < 220))
    aur = np.asarray(Image.fromarray((aur * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3)), np.float32) / 255
    fire = hsv2rgb(np.full_like(h, 18.0) + 14 * fbm(*h.shape, seed=3), np.clip(s * 1.1, 0, 1), np.clip(v * 1.15, 0, 1))
    x = x * (1 - aur[..., H]) + fire * aur[..., H]
    H_, W_ = h.shape
    yy = np.mgrid[0:H_, 0:W_][0].astype(np.float32) / H_
    smoke = fbm(H_, W_, 6, 4, seed=5)
    sky = np.clip((0.62 - yy) / 0.62, 0, 1)                       # upper part of the frame
    x = x * (1 - 0.55 * sky[..., H] * smoke[..., H]) + np.array([0.20, 0.07, 0.03]) * (sky * smoke * 0.6)[..., H]
    glow = np.clip(1 - np.abs(yy - 0.55) / 0.25, 0, 1) ** 2      # the town burns along the horizon
    x = x + np.array([0.30, 0.10, 0.02]) * (glow * (0.6 + 0.4 * fbm(H_, W_, 4, 6, seed=9)))[..., H] * 0.55
    return np.clip(x, 0, 1)


def violet_crown(x):
    """The shrine's crown light: an over-bright white bloom in the capture, turned to the Dirge's violet."""
    h, s, v = rgb2hsv(x)
    hot = np.clip((v - 0.55) * 3, 0, 1) * np.clip((0.45 - s) * 3, 0, 1)
    hot = np.asarray(Image.fromarray((hot * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(10)), np.float32) / 255
    hot = np.clip(hot * 1.6, 0, 1)
    vio = np.array([0.55, 0.30, 0.95]) * (0.55 + 0.45 * v[..., H])
    return np.clip(x * (1 - hot[..., H]) + vio * hot[..., H], 0, 1)


def fog(x, amount=0.45, seed=11):
    h, w = x.shape[:2]
    yy = np.mgrid[0:h, 0:w][0].astype(np.float32) / h
    f = fbm(h, w, 6, 3, seed=seed) * np.clip((yy - 0.35) / 0.65, 0, 1) ** 0.8
    return np.clip(x * (1 - amount * f[..., H]) + np.array([0.30, 0.34, 0.40]) * (amount * f)[..., H], 0, 1)


H = None  # numpy newaxis


def crop(x, cx=0.5, cy=0.5, zoom=1.0):
    h, w = x.shape[:2]
    ch, cw = int(h / zoom), int(w / zoom)
    y0 = int(np.clip(cy * h - ch / 2, 0, h - ch)); x0 = int(np.clip(cx * w - cw / 2, 0, w - cw))
    return x[y0:y0 + ch, x0:x0 + cw]


def save(x, name, size=(1920, 1080), quality=90):
    im = Image.fromarray((np.clip(x, 0, 1) * 255).astype(np.uint8)).resize(size, Image.LANCZOS)
    OUT.mkdir(parents=True, exist_ok=True)
    im.save(OUT / name, quality=quality)
    print(name, size)


def main():
    save(grade(burn_sky(load('helgen_a')), exposure=1.25, cold=0.4), 'intro_helgen.jpg')
    save(grade(load('sky_a'), exposure=1.1, sat=0.7), 'intro_sky.jpg')
    save(grade(crop(load('graves_a'), 0.45, 0.45, 1.25), exposure=1.6, lift=0.8), 'intro_graves.jpg')
    save(grade(load('horde_e'), exposure=1.7, lift=0.78), 'intro_horde.jpg')
    save(grade(crop(load('horde_c'), 0.32, 0.6, 1.35), exposure=1.8, lift=0.78), 'intro_champions.jpg')
    save(grade(fog(load('wyrm_a'), 0.35), exposure=2.2, lift=0.75, sat=0.75), 'intro_wyrm.jpg')
    save(grade(violet_crown(load('shrine_a')), exposure=1.35), 'intro_shrine.jpg')
    # the main menu: the horde coming down from Bleak Falls Barrow under the aurora; the dark cairn on the right
    # carries the menu, the sky the title. The aurora is also cut out on its own for the slow shimmer.
    m = grade(load('menu_a'), exposure=1.65, lift=0.8, vignette=0.45)
    h, w = m.shape[:2]
    xx = np.mgrid[0:h, 0:w][1].astype(np.float32) / w
    m = m * (1 - 0.35 * np.clip((xx - 0.62) / 0.38, 0, 1) ** 1.5)[..., H]  # deepen the menu side
    save(m, 'menu_plate.jpg', (2560, 1440), 88)
    hh, s, v = rgb2hsv(load('menu_a'))
    aur = np.clip((s - 0.2) * 3, 0, 1) * np.clip((v - 0.12) * 4, 0, 1) * ((hh > 100) & (hh < 230))
    rgba = np.dstack([np.full_like(v, 0.55), np.full_like(v, 1.0), np.full_like(v, 0.85), aur * 0.9])
    Image.fromarray((rgba * 255).astype(np.uint8), 'RGBA').resize((1280, 720), Image.LANCZOS).save(OUT / 'menu_aurora.png')
    print('menu_aurora.png')
    # the cover (mod pages, the Party Pack): the menu's night with the title over the aurora
    cov = Image.open(OUT / 'menu_plate.jpg').convert('RGBA').resize((1920, 1080), Image.LANCZOS)
    shade = np.zeros((1080, 1920, 4), np.uint8); shade[..., 3] = (np.clip(1 - np.arange(1080) / 380.0, 0, 1) ** 1.5 * 170).astype(np.uint8)[:, None]
    cov.alpha_composite(Image.fromarray(shade, 'RGBA'))  # the title reads over the sky
    title = Image.open(ROOT / 'assets' / 'ui' / 'menu_title.png').convert('RGBA')
    tw = int(1920 * 0.62); th = int(title.height * tw / title.width)
    cov.alpha_composite(title.resize((tw, th), Image.LANCZOS), ((1920 - tw) // 2, int(1080 * 0.06)))
    cov.convert('RGB').save(ROOT / 'assets' / 'cover.jpg', quality=92)
    print('cover.jpg')


if __name__ == '__main__':
    main()
