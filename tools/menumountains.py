#!/usr/bin/env python3
"""(Noah: the menu's mountains, as real as the Echo Altar) the far ranges across the lake, rendered rather than painted.

A height-field is built from warped ridged noise (big massifs at the back, a few landmark peaks framing the altar,
low forested hills by the shore), then rained on: tools/terrain.c runs droplet erosion over it, which carves the
gullies, couloirs and debris fans real mountains have, and remembers where the water ran. The same C file then
ray-marches every pixel (2x2 supersampled) into the eroded terrain with a spruce forest standing on its lower slopes,
lit by the moon from the upper left with soft cast shadows, snow lying where it would (high, gentle, and down the
gullies), sky light in the hollows, and the night air hazing it all blue with distance.

    python3 tools/menumountains.py assets/ui [scale]

Writes menu_mountains.png (2560x1440, alpha clear above the peaks and below the shore) and menu_lake_refl.png (its
reflection; tools/menuwater.py redoes that one with the water's own optics when it runs after this)."""
import ctypes, math, pathlib, subprocess, sys, time
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates, gaussian_filter, zoom

HERE = pathlib.Path(__file__).parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'menumtn')
OUT.mkdir(parents=True, exist_ok=True)
SCALE = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0

W0, H0, HORIZON0 = 2560, 1440, 800
W, H, HORIZON = int(W0 * SCALE), int(H0 * SCALE), int(HORIZON0 * SCALE)
N = 3072
X0, X1 = -26000.0, 26000.0
Z0, Z1 = 2600.0, 42000.0
CAM_Y = 10.0
F = 1280 / math.tan(math.radians(31)) * SCALE
BUMP = float(sys.argv[3]) if len(sys.argv) > 3 else 0.2
HAZE = np.array([34, 41, 62], np.float32)          # the sky low over the far shore (tools/menusky.py's horizon)


def lib():
    so = pathlib.Path('/tmp/terrain.so')
    src = HERE / 'terrain.c'
    if not so.exists() or so.stat().st_mtime < src.stat().st_mtime:
        subprocess.check_call(['gcc', '-O3', '-fopenmp', '-shared', '-fPIC', '-o', str(so), str(src), '-lm'])
    L = ctypes.CDLL(str(so))
    fp = ctypes.POINTER(ctypes.c_float)
    L.erode.argtypes = [fp, fp, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_float]
    L.render.argtypes = [fp, fp, fp, ctypes.c_int] + [ctypes.c_float] * 7 + [ctypes.c_int] * 6 + [fp, fp]
    return L


def ptr(a):
    return a.ctypes.data_as(ctypes.POINTER(ctypes.c_float))


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
    print('terrain...')
    wx = fbm(N, 3, 4, 5) - 0.5; wz = fbm(N, 3, 4, 6) - 0.5
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    rid = fbm(N, 5, 8, 11, 0.5, ridged=True)
    rid = map_coordinates(rid, [np.clip(yy + wz * 160, 0, N - 1), np.clip(xx + wx * 160, 0, N - 1)], order=1)
    soft = fbm(N, 3, 6, 21, 0.5)
    zt = np.linspace(0, 1, N, dtype=np.float32)[:, None]
    env = 200 + 3100 * np.clip((zt - 0.10) / 0.32, 0, 1) ** 1.1           # forested hills by the shore, massifs behind
    h = env * 1.25 * (0.78 * rid ** 1.75 + 0.22 * soft ** 1.2)       # (sharp: aretes between the cirques)
    # the shore's own hills: rounded, wooded, rising a few hundred feet out of the water in front of the range
    hills = fbm(N, 14, 5, 31, 0.5)
    h += 340 * np.clip((hills - 0.36) / 0.4, 0, 1) ** 1.5 * np.exp(-((zt - 0.045) / 0.045) ** 2)
    for px, pz, ph, pr in ((-0.29, 0.30, 2200, 0.036), (0.30, 0.28, 2000, 0.036), (-0.02, 0.55, 900, 0.05), (0.14, 0.40, 1100, 0.03),
                           (-0.16, 0.42, 1200, 0.03), (0.40, 0.45, 1500, 0.04), (-0.42, 0.47, 1600, 0.04)):
        cx = (px + 1) / 2 * (N - 1); cz = pz * (N - 1)
        d2 = ((xx - cx) / (pr * N)) ** 2 + ((yy - cz) / (pr * N * 0.8)) ** 2
        h += ph * np.exp(-np.sqrt(d2) * 2.2) * (0.35 + 1.1 * rid)
    det = fbm(N, 22, 4, 51, 0.55, ridged=True)
    h = h * (0.82 + 0.36 * det)
    # the shore: the land runs down to the water in a ragged line (coves where it dips under)
    shore = 0.012 + 0.03 * fbm(N, 7, 4, 61)[0:1, :]
    h = 6 + (h - 6) * np.clip((zt - shore + 0.004) / 0.03, 0, 1) ** 0.8 - 18 * np.clip(1 - zt / shore, 0, 1)
    h = gaussian_filter(h, 0.8).astype(np.float32)
    return h


def erode_at(L, h, n, drops, seed):
    """rain on the terrain at n x n: returns the change it made and where the water ran (both at full size)"""
    k = n / N
    small = zoom(h, k, order=1).astype(np.float32) if n != N else h.copy()
    cell = (Z1 - Z0) / n
    hc = np.ascontiguousarray(small / cell, np.float32)
    before = hc.copy()
    flow = np.zeros_like(hc)
    L.erode(ptr(hc), ptr(flow), n, drops, seed, 1.0)
    delta = (hc - before) * cell
    if n != N:
        delta = zoom(delta, N / n, order=3)[:N, :N]
        flow = zoom(flow, N / n, order=1)[:N, :N]
    flow = gaussian_filter(flow, 1.0)
    flow = 1 - np.exp(-flow / (np.percentile(flow, 96) + 1e-6))
    return delta.astype(np.float32), flow.astype(np.float32)


def erode(L, h):
    print('erosion...')
    t = time.time()
    d1, f1 = erode_at(L, h, 1024, 1_500_000, 1234)        # the big gullies and fans
    h = h + gaussian_filter(d1, 1.5)
    d2, f2 = erode_at(L, h, N, 3_000_000, 4321)           # the fine runnels down every face
    h2 = (h + gaussian_filter(d2, 0.7)).astype(np.float32)
    flow = np.maximum(f1 * 0.8, f2)
    print(f'  {time.time() - t:.0f}s, carved {np.abs(d1).mean():.1f} + {np.abs(d2).mean():.1f} units on average')
    return h2, flow.astype(np.float32)


def render(L, h, flow):
    print('rendering...')
    ao = np.clip(1 + (h - gaussian_filter(h, 10)) / 160, 0.25, 1.25).astype(np.float32)
    Ld = np.array([-0.80, 0.42, 0.14], np.float32); Ld /= np.linalg.norm(Ld)
    P = np.array([*Ld, 0.66, 0.72, 0.90, 0.026, 0.036, 0.062, *(HAZE / 255) ** 2.2, 30000.0, 900.0, 820.0, BUMP], np.float32)
    out = np.zeros((H, W, 5), np.float32)
    y1 = HORIZON + int(8 * SCALE)
    t = time.time()
    ss = 2
    L.render(ptr(h), ptr(flow), ptr(ao), N, X0, X1, Z0, Z1, float(h.max()), CAM_Y, F, W, H, HORIZON, 0, y1, ss, ptr(P), ptr(out))
    print(f'  {time.time() - t:.0f}s')
    rgb, a, dep = out[..., :3], out[..., 3], out[..., 4]
    # exposure and the display curve (a gentle toe: the night stays deep, the moonlit snow keeps its shape)
    rgb = np.clip(rgb * 1.45, 0, None)
    rgb = rgb / (1 + rgb * 0.35)
    srgb = np.clip(rgb, 0, 1) ** (1 / 2.2) * 255
    # low mist lying on the water along the far shore, and a faint band of it in the valleys
    rows = np.arange(H, dtype=np.float32)[:, None]
    mist = np.clip((rows - (HORIZON - 46 * SCALE)) / (50 * SCALE), 0, 1) ** 1.8
    hz = HAZE * 1.05
    srgb = srgb * (1 - 0.38 * mist[..., None]) + hz * 0.38 * mist[..., None]
    a = a.copy(); a[HORIZON + int(3 * SCALE):] = 0
    return srgb, a


def rgba(rgb, a):
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), np.clip(a, 0, 1) * 255]).astype(np.uint8), 'RGBA')


def reflection(img):
    """the ranges mirrored in the lake (a plain mirror: tools/menuwater.py gives it the water's optics)"""
    m = np.asarray(img, np.float32)
    top = m[HORIZON0 - 640:HORIZON0][::-1].copy()
    t = np.linspace(0, 1, 640, dtype=np.float32)[:, None, None]
    rgb = top[..., :3] * (0.72 - 0.30 * t) + np.array([8, 12, 24], np.float32) * (0.28 + 0.30 * t)
    rgba(rgb, top[..., 3] / 255).save(OUT / 'menu_lake_refl.png', optimize=True)


if __name__ == '__main__':
    L = lib()
    h = heightfield()
    import os
    h, flow = erode(L, h) if not os.environ.get('NOERODE') else (h, np.zeros_like(h))
    rgb, a = render(L, h, flow)
    img = rgba(rgb, a)
    if SCALE == 1.0:
        img.save(OUT / 'menu_mountains.png', optimize=True)
        reflection(img)
    prev = Image.new('RGBA', img.size, (18, 22, 38, 255)); prev.alpha_composite(img)
    prev.convert('RGB').save(OUT / 'mtn_preview.jpg', quality=92)
    print('menumountains: done')
