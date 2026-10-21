#!/usr/bin/env python3
"""(Noah: the menu's mountains, as real as the Echo Altar) the far ranges across the lake, rendered from a real
height-field instead of painted: ridged, warped multifractal terrain with big massifs at the back, lit by the moon
from the upper left, snow lying where it would (high, and on gentle slopes), bare rock on the steep faces, pine
forest low on the near hills, and the night air hazing everything blue with distance. A column ray-caster (front to
back, per screen column) draws it into the menu's painting space; tools/menulake.py's shore forest and lake
reflection are laid on as before.

    python3 tools/menumountains.py assets/ui

Writes menu_mountains.png (2560x1440, alpha clear above the peaks) and menu_lake_refl.png (its reflection)."""
import math, pathlib, sys
import numpy as np
from PIL import Image, ImageFilter
from scipy.ndimage import map_coordinates, gaussian_filter

sys.path.insert(0, str(pathlib.Path(__file__).parent))
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'menumtn')
OUT.mkdir(parents=True, exist_ok=True)
sys.argv = [sys.argv[0], str(OUT)]
import menulake as ML

W, H, HORIZON = ML.W, ML.H, ML.HORIZON
N = 3072
X0, X1 = -26000.0, 26000.0
Z0, Z1 = 2600.0, 42000.0
CAM_Y = 60.0
F = 1280 / math.tan(math.radians(31))


def noise2(n, base, seed):
    rng = np.random.default_rng(seed)
    g = rng.random((base + 3, base + 3)).astype(np.float32)
    c = np.linspace(0, base, n, dtype=np.float32)
    yy, xx = np.meshgrid(c, c, indexing='ij')
    return map_coordinates(g, [yy, xx], order=3, mode='nearest')


def fbm(n, base, octaves, seed, pers=0.5, ridged=False):
    out = np.zeros((n, n), np.float32); amp = 1.0; tot = 0.0
    for o in range(octaves):
        v = noise2(n, base * 2 ** o, seed + o * 17)
        if ridged:
            v = (1.0 - np.abs(2 * v - 1)) ** 2
        out += v * amp; tot += amp; amp *= pers
    return out / tot


def heightfield():
    """rows: depth (Z0 near .. Z1 far), cols: x"""
    print('terrain...')
    wx = fbm(N, 3, 4, 5) - 0.5; wz = fbm(N, 3, 4, 6) - 0.5
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    rid = fbm(N, 5, 8, 11, 0.5, ridged=True)
    rid = map_coordinates(rid, [np.clip(yy + wz * 140, 0, N - 1), np.clip(xx + wx * 140, 0, N - 1)], order=1)
    soft = fbm(N, 3, 6, 21, 0.5)
    zt = np.linspace(0, 1, N, dtype=np.float32)[:, None]
    env = 140 + 2600 * np.clip((zt - 0.03) / 0.42, 0, 1) ** 1.1          # low hills by the shore, massifs behind
    h = env * (0.68 * rid ** 1.45 + 0.32 * soft ** 1.3)
    # a few great peaks (the range's landmarks), placed in painting space so they frame the altar
    for px, pz, ph, pr in ((-0.29, 0.30, 2100, 0.035), (0.30, 0.28, 1900, 0.035), (-0.02, 0.55, 900, 0.05), (0.14, 0.40, 1100, 0.03), (-0.16, 0.42, 1200, 0.03), (0.40, 0.45, 1500, 0.04), (-0.42, 0.47, 1600, 0.04)):
        cx = (px + 1) / 2 * (N - 1); cz = pz * (N - 1)
        d2 = ((xx - cx) / (pr * N)) ** 2 + ((yy - cz) / (pr * N * 0.8)) ** 2
        h += ph * np.exp(-np.sqrt(d2) * 2.2) * (0.35 + 1.1 * rid)   # (a cone, not a dome: sharp summits)
    # rock: fine ridged detail that grows with height - gullies, aretes and buttresses down every face
    det = fbm(N, 22, 5, 51, 0.55, ridged=True)
    h = h * (0.78 + 0.44 * det)
    # a touch of erosion: valleys carved along the soft noise's low lines
    h -= 260 * np.clip(0.35 - soft, 0, 1) * zt
    h = gaussian_filter(h, 1.4)
    return h.astype(np.float32)


def render(h):
    print('rendering...')
    gz, gx = np.gradient(h, (Z1 - Z0) / N, (X1 - X0) / N)
    nrm = np.dstack([-gx, np.ones_like(h), -gz]); nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    slope = 1 - nrm[..., 1]                                              # 0 flat .. 1 vertical
    L = np.array([-0.78, 0.38, 0.30], np.float32); L /= np.linalg.norm(L)  # the moon: up-left, a little behind
    lam = np.clip(nrm @ L, 0, 1)
    # snow: above a ragged snowline, and only where it can lie
    snowline = 850 + 450 * fbm(N, 6, 4, 33)
    snow = np.clip((h - snowline) / 300, 0, 1) * np.clip((0.42 - slope) / 0.16, 0, 1)
    snow = np.maximum(snow, np.clip((h - 1900) / 400, 0, 1) * np.clip((0.62 - slope) / 0.2, 0, 1))
    grain = fbm(N, 64, 3, 41)
    forestm = np.clip((700 - h) / 300, 0, 1) * np.clip((0.6 - slope) / 0.3, 0, 1) * (1 - snow)
    rock = np.array([58, 62, 74], np.float32) * (0.6 + 0.8 * grain[..., None])
    snowc = np.array([205, 218, 240], np.float32)
    pine = np.array([14, 22, 26], np.float32) * (0.8 + 0.4 * grain[..., None])
    alb = rock * (1 - snow[..., None]) + snowc * snow[..., None]
    alb = alb * (1 - forestm[..., None]) + pine * forestm[..., None]
    amb = np.array([0.30, 0.35, 0.50], np.float32)
    sky_occ = 0.6 + 0.4 * nrm[..., 1]
    col = alb * (amb[None, None] * sky_occ[..., None] + np.array([0.85, 0.92, 1.08], np.float32)[None, None] * lam[..., None] * 1.25)
    # the column ray-caster: front to back
    rgb = np.zeros((H, W, 3), np.float32); a = np.zeros((H, W), np.float32)
    ybuf = np.full(W, HORIZON + 6, np.float32)                     # nothing below the shore (the lake is the game's)
    rows = np.arange(H, dtype=np.float32)[:, None]
    sx = (np.arange(W, dtype=np.float32) - W / 2) / F
    z = Z0; dz = 4.0
    haze = np.array([44, 54, 82], np.float32)
    while z < Z1:
        x = sx * z
        u = (x - X0) / (X1 - X0) * (N - 1); v = np.full(W, (z - Z0) / (Z1 - Z0) * (N - 1), np.float32)
        ok = (u >= 0) & (u <= N - 1)
        hh = map_coordinates(h, [v, np.clip(u, 0, N - 1)], order=3)
        top = HORIZON - (hh - CAM_Y) * F / z
        draw = ok & (top < ybuf)
        if draw.any():
            c = np.stack([map_coordinates(col[..., k], [v, np.clip(u, 0, N - 1)], order=1) for k in range(3)], 1)
            fog = 1 - np.exp(-(z - Z0) / 34000.0)
            c = c * (1 - fog) + haze * fog * 0.95
            m = draw[None, :] & (rows >= np.floor(top)[None, :]) & (rows < ybuf[None, :])
            # (anti-aliased crest: the top pixel takes its coverage)
            rgb[m] = np.broadcast_to(c[None, :, :], (H, W, 3))[m]
            a[m] = 1.0
            frac = 1 - (top - np.floor(top))
            tp = np.clip(np.floor(top).astype(int) - 1, 0, H - 1)
            cov = draw & (a[tp, np.arange(W)] < 1)
            a[tp[cov], np.arange(W)[cov]] = np.maximum(a[tp[cov], np.arange(W)[cov]], 1 - frac[cov])
            rgb[tp[cov], np.arange(W)[cov]] = c[cov]
            ybuf = np.where(draw, np.minimum(ybuf, np.floor(top)), ybuf)
        z += dz; dz *= 1.005
    # low mist along the shore
    mist = np.clip((rows - (HORIZON - 70)) / 70.0, 0, 1) ** 1.6
    rgb = rgb * (1 - 0.55 * mist[..., None]) + haze * 0.55 * mist[..., None]
    rgb[HORIZON + 4:] = 0; a[HORIZON + 4:] = 0
    return rgb, a


if __name__ == '__main__':
    h = heightfield()
    rgb, a = render(h)
    # the shore's own dark line under the trees, as menulake draws it
    yy = np.arange(H)[:, None]
    shore = HORIZON - 6 - 10 * ML.fbm1(W, 6, 40, 77)
    tm = ((yy >= shore[None, :]) & (yy <= HORIZON + 2)).astype(np.float32)
    rgb = rgb * (1 - tm[..., None]) + np.array([14, 17, 28])[None, None] * tm[..., None]
    a = np.maximum(a, tm)
    img = ML.forest(ML.rgba(rgb, a))
    img.save(OUT / 'menu_mountains.png', optimize=True)
    ML.reflection(img)
    prev = Image.new('RGBA', (W, H), (18, 22, 38, 255)); prev.alpha_composite(img)
    prev.convert('RGB').resize((1280, 720), Image.LANCZOS).save('/tmp/claude-0/mtn_new.jpg', quality=90)
    print('menumountains: wrote menu_mountains.png, menu_lake_refl.png')
