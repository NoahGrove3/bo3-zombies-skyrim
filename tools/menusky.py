#!/usr/bin/env python3
"""(Noah: make the entire menu hyper realistic) the night over the lake, built like a long-exposure photograph:

  * the sky's own light: deep at the top, a cold glow of airglow and haze down by the far peaks;
  * the Milky Way rising out of the range on the right - its bright core low down, star clouds and the dark dust lanes
    of the Great Rift through it, the band thinning as it climbs;
  * forty thousand stars, from the few bright ones to the faint dust of them, each at its own colour (red giants to
    blue-white), dimmed low down by the air and lost in the moon's glare;
  * the Blood Moon in total eclipse: seas and highlands and a few bright-rayed craters on its face, deep red where the
    Earth's shadow is darkest, copper and gold toward the lit limb, the faint turquoise fringe real eclipse photos show,
    a red bloom in the air around it;
  * the northern lights: curtains of fine rays, a sharp bright hem low down, green fading upward into a faint crimson,
    brightest where a curtain folds back on itself;
  * thin night clouds (one layer still, one that tiles left-right for the menu to drift).

    python3 tools/menusky.py assets/ui

Writes menu_sky.jpg, menu_aurora2.png, menu_clouds.png, menu_clouds2.png (positions as the menu expects them:
the moon at 25.5% / 31.5% of the frame, the far shore at y 800)."""
import math, pathlib, sys
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'menusky')
OUT.mkdir(parents=True, exist_ok=True)
W, H, HORIZON = 2560, 1440, 800
MOON = (0.255 * W, 0.315 * H)
MOON_R = 0.064 * H
F = 1280 / math.tan(math.radians(31))
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)


def pnoise(h, w, beta, seed, lo=0.0):
    """tileable 1/f^beta noise (both ways), 0..1"""
    rng = np.random.default_rng(seed)
    wn = rng.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.fftfreq(w)[None, :]
    f = np.sqrt(fx * fx + fy * fy); f[0, 0] = 1
    amp = 1 / f ** beta
    if lo:
        amp *= 1 - np.exp(-(f / lo) ** 2)
    amp[0, 0] = 0
    n = np.real(np.fft.ifft2(np.fft.fft2(wn) * amp))
    n = (n - n.mean()) / (n.std() + 1e-9)
    return (0.5 + 0.18 * n).astype(np.float32)


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def to8(lin, gamma=2.2):
    return (np.clip(lin, 0, 1) ** (1 / gamma) * 255 + np.random.default_rng(3).random(lin.shape) - 0.5).clip(0, 255).astype(np.uint8)


def lin(srgb):
    return (np.asarray(srgb, np.float32) / 255) ** 2.2


def blackbody(T):
    """rough sRGB tint of a star of temperature T (K), normalised to its brightest channel"""
    t = T / 100.0
    r = np.where(t <= 66, 255, 329.7 * (t - 60) ** -0.1332)
    g = np.where(t <= 66, 99.47 * np.log(t) - 161.1, 288.1 * (t - 60) ** -0.0755)
    b = np.where(t >= 66, 255, np.where(t <= 19, 0, 138.5 * np.log(t - 10) - 305.0))
    c = np.clip(np.stack([r, g, b], -1), 1, 255) / 255
    return (c / c.max(-1, keepdims=True)) ** 2.2


# ------------------------------------------------------------------------------------------------ the sky's own light
def base_sky():
    v = yy / HORIZON
    zen, mid, low = lin((3, 4, 11)), lin((9, 11, 25)), lin((30, 36, 57))
    c = zen + (mid - zen) * smooth(0.0, 0.6, v)[..., None] + (low - mid) * smooth(0.55, 1.0, v)[..., None] ** 1.5
    # airglow: a faint green-gold band some degrees over the horizon
    c += (np.exp(-((v - 0.86) / 0.07) ** 2) * 0.0035)[..., None] * np.array([0.6, 1.0, 0.45])
    # the moon's glow in the air: a tight red bloom, a wide faint one
    d = np.hypot(xx - MOON[0], yy - MOON[1]) / MOON_R
    glow = 0.030 * np.exp(-(d - 1) / 0.55) * (d > 1) + 0.010 * np.exp(-d / 3.0) + 0.0025 * np.exp(-d / 9.0)
    c += glow[..., None] * np.array([1.0, 0.30, 0.16])
    return c.astype(np.float32), d


# ------------------------------------------------------------------------------------------------ the Milky Way
MW_LUMI = [None]


def milky_way():
    """returns (light, band weight): the band rises from behind the right-hand peaks and climbs up and left"""
    p0 = np.array([2080.0, 860.0]); p1 = np.array([980.0, -120.0])
    ax = (p1 - p0) / np.linalg.norm(p1 - p0); nx = np.array([-ax[1], ax[0]])
    rx, ry = xx - p0[0], yy - p0[1]
    u = (rx * ax[0] + ry * ax[1]) / np.linalg.norm(p1 - p0)          # 0 at the core .. 1 at the top
    v = (rx * nx[0] + ry * nx[1])
    warp = (pnoise(H, W, 1.6, 81) - 0.5) * 160
    width = 150 + 140 * np.clip(1 - u, 0, 1) ** 2                       # broad and bright low down
    band = np.exp(-((v + warp) / width) ** 2) * smooth(-0.15, 0.08, u) * smooth(1.25, 0.55, u)
    core = np.exp(-((v + warp * 0.6) / 170) ** 2 - ((u - 0.06) / 0.16) ** 2)
    clouds = pnoise(H, W, 1.5, 82) * 0.7 + gaussian_filter(pnoise(H, W, 1.0, 83), 1.2) * 0.3      # star clouds, coarse and fine
    clouds = np.clip((clouds - 0.30) * 1.8, 0, 2)
    # the dust: dark filaments along the band's middle (the Great Rift) and smaller lanes off it
    fil = gaussian_filter(pnoise(H, W, 1.7, 84), 2.5)
    lanes = 1 - np.abs(fil - 0.5) * 2                                    # ridges of the noise: filaments
    rift = smooth(0.80, 0.97, lanes) * np.exp(-((v + warp * 0.8 - 18) / 70) ** 2) * smooth(0.0, 0.25, u)
    dust = smooth(0.80, 0.97, 1 - np.abs(gaussian_filter(pnoise(H, W, 1.65, 85), 2.0) - 0.5) * 2) * band
    absorb = np.clip(1 - 0.92 * rift - 0.70 * dust, 0.04, 1)
    lumi = (band * (0.35 + 0.65 * clouds) + core * 1.1 * (0.5 + 0.5 * clouds)) * absorb
    MW_LUMI[0] = lumi
    tint = np.array([0.80, 0.86, 1.0])[None, None] * (1 - core[..., None]) + np.array([1.0, 0.84, 0.62])[None, None] * core[..., None]
    light = lumi[..., None] * tint * 0.045
    # a few faint red emission clouds in the band
    em = smooth(0.70, 0.85, pnoise(H, W, 1.1, 86)) * band
    light += (em * 0.004)[..., None] * np.array([1.0, 0.25, 0.35])
    return light.astype(np.float32), band


# ------------------------------------------------------------------------------------------------ the stars
def stars(band, dmoon):
    rng = np.random.default_rng(1977)
    n = 46000
    # positions: everywhere, and many more in the band
    xs = rng.uniform(0, W, n * 3); ys = rng.uniform(-10, HORIZON + 10, n * 3)
    bw = map_coordinates(band, [np.clip(ys, 0, H - 1), xs.clip(0, W - 1)], order=1)
    keep = rng.random(n * 3) < (0.28 + 0.72 * bw)
    xs, ys = xs[keep][:n], ys[keep][:n]
    n = len(xs)
    # magnitudes: each step fainter, about three times as many
    m = np.log(1 + rng.random(n) * (np.exp(1.05 * 7.5) - 1)) / 1.05     # (most of them faint)
    flux = 10 ** (-0.4 * (m - 1.0)) * 0.9
    T = np.clip(rng.lognormal(np.log(7800), 0.35, n), 2800, 25000)
    col = blackbody(T)
    # the air dims and reddens them low down; the moon's glare hides the faint ones near it
    alt = np.clip((HORIZON - ys) / F, 0.002, 1)
    airmass = 1 / (np.sin(alt) + 0.15 * (alt + 0.03) ** -0.6 * 0.02 + 0.02)
    ext = np.exp(-0.035 * airmass)
    col = col * np.stack([ext ** 0.7, ext ** 0.9, ext ** 1.2], -1)
    dm = np.hypot(xs - MOON[0], ys - MOON[1]) / MOON_R
    flux *= smooth(1.4, 5.0, dm) ** np.clip(4 - (7 - m) * 0.5, 0.5, 4)
    img = np.zeros((H, W, 3), np.float32)
    halo = np.zeros((H, W, 3), np.float32)
    ix = np.floor(xs).astype(int); iy = np.floor(ys).astype(int); fx = xs - ix; fy = ys - iy
    for dx, dy, wgt in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        X = ix + dx; Y = iy + dy
        ok = (X >= 0) & (X < W) & (Y >= 0) & (Y < H)
        for c in range(3):
            np.add.at(img[..., c], (Y[ok], X[ok]), (flux * wgt * col[:, c])[ok])
            big = m < 3.2
            np.add.at(halo[..., c], (Y[ok & big], X[ok & big]), (flux * wgt * col[:, c])[ok & big])
    out = np.stack([gaussian_filter(img[..., c], 0.62) for c in range(3)], -1) * 2.2
    out += np.stack([gaussian_filter(halo[..., c], 2.2) for c in range(3)], -1) * 0.9
    out += np.stack([gaussian_filter(halo[..., c], 7.0) for c in range(3)], -1) * 0.5
    # the unresolved glow of the countless faint ones
    out *= 0.26
    # the band's grain: countless faint stars, thickest where its light is
    rng2 = np.random.default_rng(2024)
    g = rng2.random((H, W)).astype(np.float32) ** 40 * (0.15 + 3.0 * band)
    g = gaussian_filter(g, 0.55) * 0.05
    out += g[..., None] * np.array([0.92, 0.95, 1.0])
    return out


# ------------------------------------------------------------------------------------------------ the Blood Moon
def moon(sky, d):
    mx, my = MOON
    px, py = (xx - mx) / MOON_R, (yy - my) / MOON_R             # -1..1 across the disc
    r2 = px * px + py * py
    inside = r2 < 1.2
    zc = np.sqrt(np.clip(1 - r2, 0, 1))
    # the face, mapped onto the sphere so features crowd toward the limb
    lon = np.arctan2(px, zc + 1e-6); lat = np.arcsin(np.clip(py, -1, 1))
    su, sv = lon / math.pi * 1024 + 1024, lat / math.pi * 1024 + 1024
    maria_n = pnoise(2048, 2048, 2.2, 301)
    detail = pnoise(2048, 2048, 1.2, 302)
    mar = map_coordinates(maria_n, [sv, su], order=1, mode='wrap')
    det = map_coordinates(detail, [sv, su], order=1, mode='wrap')
    bias = 0.06 * (-py) + 0.05 * (-px)                             # the seas gather upper left, like the real face
    maria = smooth(0.505, 0.545, mar + bias + 0.05 * (det - 0.5))
    alb = 0.98 - 0.46 * maria + 0.24 * (det - 0.5)
    # craters: small bright-rimmed ones over the highlands, and two great rayed ones
    rng = np.random.default_rng(5)
    cr = np.zeros((H, W), np.float32)
    for _ in range(140):
        a = rng.uniform(0, 2 * np.pi); rr = math.sqrt(rng.uniform(0, 0.92))
        cx, cy = math.cos(a) * rr, math.sin(a) * rr
        s = rng.uniform(0.025, 0.10) * (1 - 0.4 * rr)
        q = np.hypot(px - cx, (py - cy)) / s
        cr += (0.10 * np.exp(-((q - 1) / 0.35) ** 2) - 0.06 * np.exp(-(q / 0.7) ** 2)) * (1 - maria * 0.6)
    for cx, cy, s, rays in ((-0.12, 0.62, 0.035, 0.55), (-0.52, -0.08, 0.03, 0.35)):
        q = np.hypot(px - cx, py - cy)
        ang = np.arctan2(py - cy, px - cx)
        spokes = np.zeros_like(q)
        for k in range(14):
            th = rng.uniform(-np.pi, np.pi); wdt = rng.uniform(0.03, 0.08); ln = rng.uniform(0.4, 1.1)
            dth = np.angle(np.exp(1j * (ang - th)))
            spokes += np.exp(-(dth / wdt) ** 2) * np.exp(-q / ln) * smooth(0.02, 0.06, q)
        cr += rays * 0.10 * np.clip(spokes, 0, 1.5) + 0.22 * np.exp(-(q / s) ** 2)
    alb = alb + cr
    # the Earth's shadow: darkest toward the upper left, copper and gold toward the lower right limb
    k = (px * 0.62 + py * 0.78)
    shade = 0.40 + 0.60 * smooth(-1.3, 1.1, k) ** 1.3
    deep, bright = lin((120, 30, 14)), lin((240, 122, 56))
    c = deep + (bright - deep) * smooth(-0.6, 1.0, k)[..., None]
    face = c * (alb * shade)[..., None] * 0.66
    # the turquoise fringe at the lit limb (sunlight through the ozone of Earth's rim)
    fringe = np.exp(-((np.sqrt(r2) - 0.93) / 0.07) ** 2) * smooth(0.55, 1.0, k)
    face += (fringe * 0.035)[..., None] * np.array([0.35, 0.75, 0.85])
    # limb: the moon barely darkens at its edge; antialiased by area
    edge = np.clip((1 - np.sqrt(r2)) * MOON_R + 0.5, 0, 1)
    face = face * (0.92 + 0.08 * zc)[..., None]
    for c_ in range(3):
        face[..., c_] = gaussian_filter(face[..., c_], 0.7)      # the air's softness
    sky = sky * (1 - edge[..., None]) + face * edge[..., None]
    return sky


# ------------------------------------------------------------------------------------------------ the northern lights
def aurora():
    out = np.zeros((H, W, 3), np.float32)
    x = np.arange(W, dtype=np.float32)
    rng = np.random.default_rng(64)
    for ci, (y0, amp, wl, ph, height, gain) in enumerate(((560, 85, 1500, 0.5, 260, 1.0), (420, 55, 1100, 2.4, 150, 0.36))):
        n1 = pnoise(1, W * 2, 1.9, 400 + ci)[0, :W]
        hem = y0 + amp * np.sin(x / wl * 2 * math.pi + ph) + 120 * (n1 - 0.5) + 18 * np.sin(x / 190 + ph * 2)
        slope = np.gradient(gaussian_filter(hem, 3))
        fold = np.sqrt(1 + (slope * 1.8) ** 2)                      # a curtain seen edge-on where it folds: brighter
        # the rays: a fine shimmer of brighter and dimmer streaks, each with its own reach
        def band1d(beta, seed, sig):
            v = gaussian_filter(pnoise(1, W, beta, seed)[0], sig)
            return (v - v.mean()) / (v.std() + 1e-6)
        rays = np.clip(1 + 0.22 * band1d(0.4, 410 + ci, 1.4) + 0.30 * band1d(0.9, 420 + ci, 2.5), 0.35, 1.9)
        reach = height * np.clip(1 + 0.25 * band1d(0.7, 430 + ci, 2.0), 0.55, 1.6)
        along = np.clip(0.55 + 0.5 * band1d(2.4, 440 + ci, 6), 0.0, 1.3)        # where the curtain is bright
        d = (hem[None, :] - yy) / reach[None, :]                      # 0 at the hem, 1 at the rays' reach
        dn, dp = np.minimum(d, 0), np.maximum(d, 0)
        up = (0.85 * np.exp(-dp * 3.2) + 0.15 * np.exp(-dp * 1.1)) * (1 - np.exp(-dp * 40)) * smooth(1.35, 0.6, dp)
        prof = np.where(d < 0, np.exp(dn * height / 7.0), up)
        inten = prof * (rays * along * np.minimum(fold, 2.2))[None, :] * gain
        dd = np.clip(d, 0, 2)[..., None]
        green = np.array([0.30, 1.0, 0.50]); hemc = np.array([0.55, 1.0, 0.45]); red = np.array([0.75, 0.15, 0.40])
        col = hemc * np.exp(-dd * 9) + green * (1 - np.exp(-dd * 9)) * np.exp(-dd * 1.3) + red * (1 - np.exp(-dd * 1.3)) * 0.6
        out += col * inten[..., None]
    # a soft vertical smear (the rays' own blur) and the air's dimming low down
    for c in range(3):
        out[..., c] = gaussian_filter(out[..., c], (6, 1.3))
    out *= smooth(HORIZON + 20, HORIZON - 160, yy)[..., None]
    lum = out.max(-1)
    a = np.clip(lum * 0.62, 0, 0.72)
    rgb = out / np.maximum(lum[..., None], 1e-4)
    rgb = np.clip(rgb, 0, 1) * 255
    return Image.fromarray(np.dstack([rgb, a * 255]).astype(np.uint8), 'RGBA')


# ------------------------------------------------------------------------------------------------ night clouds
def cloud_layer(w, h, seed, cover, stretch, moonlit, ymask):
    n = pnoise(h, w // stretch, 1.45, seed)
    n = (n - n.min()) / (n.max() - n.min())
    base = np.asarray(Image.fromarray(n.astype(np.float32), 'F').resize((w, h), Image.BICUBIC), np.float32)
    base = 0.5 + (base - base.mean()) / (base.std() * 5.5)
    wn = pnoise(h, w // stretch, 0.9, seed + 1)
    wisps = np.asarray(Image.fromarray(wn.astype(np.float32), 'F').resize((w, h), Image.BICUBIC), np.float32)
    dens = smooth(cover, cover + 0.25, base * 0.8 + wisps * 0.3) * ymask
    yl = np.arange(h, dtype=np.float32)[:, None]
    rgb = np.zeros((h, w, 3), np.float32) + np.array([30, 33, 46], np.float32)
    if moonlit is not None:
        rgb = rgb + moonlit[..., None] * np.array([60, 16, 10], np.float32)
    edge = np.clip(dens - gaussian_filter(dens, 3), 0, 1)
    rgb += edge[..., None] * np.array([30, 30, 40])
    return Image.fromarray(np.dstack([rgb.clip(0, 255), (dens * 0.6).clip(0, 1) * 255]).astype(np.uint8), 'RGBA')


def main():
    print('sky...')
    sky, dmoon = base_sky()
    mw, band = milky_way()
    sky += mw
    print('stars...')
    sky += stars(np.clip(MW_LUMI[0], 0, 1.5) * 0.6 + band * 0.2, dmoon)
    print('moon...')
    sky = moon(sky, dmoon)
    # a fine film grain so the gradients never band
    Image.fromarray(to8(sky)).save(OUT / 'menu_sky.jpg', quality=95)
    print('aurora...')
    aurora().save(OUT / 'menu_aurora2.png', optimize=True)
    print('clouds...')
    v = yy / H
    dm = np.hypot(xx - MOON[0], yy - MOON[1]) / MOON_R
    still = cloud_layer(W, H, 501, 0.56, 3, np.exp(-dm / 6), (smooth(0.18, 0.40, v) * smooth(0.58, 0.46, v)))
    still.save(OUT / 'menu_clouds.png', optimize=True)
    v2 = np.arange(640, dtype=np.float32)[:, None] / 640
    drift = cloud_layer(2048, 640, 511, 0.60, 3, None, smooth(0.0, 0.35, v2) * smooth(1.0, 0.7, v2))
    drift.save(OUT / 'menu_clouds2.png', optimize=True)
    print('menusky: wrote menu_sky.jpg, menu_aurora2.png, menu_clouds.png, menu_clouds2.png')


if __name__ == '__main__':
    main()
