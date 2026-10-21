#!/usr/bin/env python3
"""(Noah: make the entire menu hyper realistic) the lake, with a real water surface's optics instead of a gradient:

  * Fresnel: near the far shore we look across the water at a grazing angle and it is nearly a mirror; toward us we
    look down into it and it turns dark and clear, showing its own deep blue-green;
  * the sky in it - the Milky Way, the stars, the Blood Moon - mirrored through a field of small wind ripples drawn in
    true perspective (wide and slow near us, packed into fine lines toward the shore), which breaks the moon into a
    long wavering path of red light and smears the far reflections a little, the way calm water does;
  * the far ranges' reflection given the same ripples and the same Fresnel darkening.

    python3 tools/menuwater.py assets/ui         (after tools/menusky.py and tools/menumountains.py)

Writes menu_lake.png (2560x640: the water below the far shore, y 800..1440) and menu_lake_refl.png (the ranges
mirrored in it, alpha where they are). The menu draws both in rows that sway, so the water moves."""
import math, pathlib, sys
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates, gaussian_filter1d

D = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'assets/ui')
W, H, HORIZON, LH = 2560, 1440, 800, 640
F = 1280 / math.tan(math.radians(31))
CAM_H = 30.0                     # how high over the water we look from
SLOPE = 0.0022                   # the ripples' typical steepness: a still night, a breath of wind


def lin(a):
    return (np.asarray(a, np.float32) / 255) ** 2.2


def pnoise(n, beta, seed):
    rng = np.random.default_rng(seed)
    wn = rng.standard_normal((n, n))
    f = np.sqrt(np.fft.fftfreq(n)[:, None] ** 2 + np.fft.fftfreq(n)[None, :] ** 2); f[0, 0] = 1
    amp = 1 / f ** beta; amp[0, 0] = 0
    amp *= np.exp(-(f / 0.25) ** 2)
    v = np.real(np.fft.ifft2(np.fft.fft2(wn) * amp))
    return ((v - v.mean()) / v.std()).astype(np.float32)


def ripples():
    """per lake pixel: the surface's slope across (sx) and along (sz) the view, already averaged over what each pixel
    covers, and how much of the slope got averaged away (it blurs the reflection instead)"""
    n = 1024
    hgt = pnoise(n, 1.9, 7) + 0.5 * pnoise(n, 1.5, 8)
    j = np.arange(1, LH + 1, dtype=np.float32)[:, None]           # rows below the horizon (1 = right at the shore)
    x = np.arange(W, dtype=np.float32)[None, :] - W / 2
    z = CAM_H * F / j                                              # how far away that row of water is
    lam = 0.9                                                       # world units per noise texel
    v = z / lam
    foot = np.maximum(z / F, CAM_H * F / j ** 2) / lam            # texels under one pixel (the larger way)
    sx = np.zeros((LH, W), np.float32); sz = np.zeros((LH, W), np.float32); lost = np.zeros((LH, 1), np.float32)
    levels = [0, 1, 2, 4, 8, 16, 32, 64]
    gx, gz = np.gradient(hgt)
    pre = [(gaussian_filter(gx, s, mode='wrap') if s else gx, gaussian_filter(gz, s, mode='wrap') if s else gz) for s in levels]
    rms0 = math.hypot(gx.std(), gz.std())
    for r in range(LH):
        fr = float(foot[r, 0])
        li = int(np.searchsorted(levels, fr * 0.5))
        li = min(li, len(levels) - 1)
        a, b = pre[li]
        vv = np.full(W, float(np.mod(v[r, 0], n)), np.float32)
        uu = np.mod(x[0] * float(z[r, 0]) / F / lam / 7.0, n)
        sx[r] = map_coordinates(a, [vv, uu], order=1, mode='wrap')
        sz[r] = map_coordinates(b, [vv, uu], order=1, mode='wrap')
        kept = math.hypot(a.std(), b.std()) / rms0
        lost[r, 0] = math.sqrt(max(0.0, 1 - kept * kept))
    k = SLOPE / (math.hypot(gx.std(), gz.std()) + 1e-9)
    return sx * k, sz * k, lost


def fresnel(j):
    th = np.arctan(j / F)                                           # how far below the horizon we look
    c = np.sin(th)                                                  # cos of the angle to the surface's normal
    return 0.02 + 0.98 * (1 - c) ** 5


def reflect(img, sx, sz, lost, alpha_too=False):
    """the picture above the shore (H x W, float, linear), seen in the water: rows 800..1440"""
    j = np.arange(1, LH + 1, dtype=np.float32)[:, None]
    # where each water pixel's reflected ray lands in the picture: straight up the mirror, nudged by the ripples
    dy = 2 * sz * F
    dx = 2 * sx * F * 0.15
    ys = HORIZON - j - dy
    xs = np.arange(W, dtype=np.float32)[None, :] + dx
    # what the ripples too small to see do: a soft vertical smear, more of it the farther the water
    out = np.zeros((LH, W, img.shape[2]), np.float32)
    sig_levels = [0, 3, 8, 16, 32]
    blurred = [img if s == 0 else gaussian_filter1d(img, s, axis=0, mode='nearest') for s in sig_levels]
    sig = (lost * 2 * SLOPE * F * 0.8)[:, 0]
    for r in range(LH):
        s = sig[r]
        i = int(np.searchsorted(sig_levels, s)); i = max(1, min(i, len(sig_levels) - 1))
        t = np.clip((s - sig_levels[i - 1]) / (sig_levels[i] - sig_levels[i - 1]), 0, 1)
        for c in range(img.shape[2]):
            a = map_coordinates(blurred[i - 1][..., c], [np.clip(ys[r], 0, H - 1), np.clip(xs[r], 0, W - 1)], order=1)
            b = map_coordinates(blurred[i][..., c], [np.clip(ys[r], 0, H - 1), np.clip(xs[r], 0, W - 1)], order=1)
            out[r, :, c] = a * (1 - t) + b * t
    return out


def main():
    print('ripples...')
    sx, sz, lost = ripples()
    j = np.arange(1, LH + 1, dtype=np.float32)[:, None]
    R = fresnel(j)[..., None]
    print('the sky in the water...')
    sky = lin(Image.open(D / 'menu_sky.jpg').convert('RGB'))
    refl_sky = reflect(sky, sx, sz, lost)
    body = lin((5, 10, 14)) * (0.6 + 0.4 * np.exp(-(j - 1) / 200))[..., None]   # the lake's own colour, deep
    water = R * refl_sky + (1 - R) * body
    # a faint sheen of the night sky on the ripple faces turned toward it (they catch more of the sky's glow)
    tilt = np.clip(-sz * 60, -1, 1)[..., None]
    water *= 1 + 0.25 * tilt
    srgb = np.clip(water, 0, 1) ** (1 / 2.2) * 255
    srgb += np.random.default_rng(9).random(srgb.shape) - 0.5
    Image.fromarray(np.dstack([srgb.clip(0, 255), np.full((LH, W), 255)]).astype(np.uint8), 'RGBA').save(D / 'menu_lake.png', optimize=True)
    print('the ranges in the water...')
    mt = np.asarray(Image.open(D / 'menu_mountains.png').convert('RGBA'), np.float32) / 255
    pm = np.dstack([mt[..., :3] ** 2.2 * mt[..., 3:4], mt[..., 3:4]])       # premultiplied, linear
    rm = reflect(pm, sx, sz, lost)
    a = np.clip(rm[..., 3:4], 0, 1)
    col = rm[..., :3] / np.maximum(a, 1e-4)
    # the same optics: the mirror's strength, and the water's own dark showing through toward us
    col = col * R / np.maximum(R, 1e-3)
    seen = R * col + (1 - R) * body
    # alpha: where the ranges stand in the reflection, how much they replace the sky's reflection under them
    out_a = a[..., 0]
    srgb = np.clip(seen, 0, 1) ** (1 / 2.2) * 255
    Image.fromarray(np.dstack([srgb.clip(0, 255), out_a * 255]).astype(np.uint8), 'RGBA').save(D / 'menu_lake_refl.png', optimize=True)
    print('menuwater: wrote menu_lake.png, menu_lake_refl.png')


if __name__ == '__main__':
    main()
