#!/usr/bin/env python3
"""The night's emblem for the HUD (Noah: "remake the Nights Survived and wave box ... animated to match the Cursed essence
box" - "Blood moon", "Blood red"): a blood moon, its cratered face slowly turning and breathing a red glow, thin mist
drifting across it, ringed by the same slowly turning circle of dragon-script ticks as the essence's emblem. A seamlessly
looping flipbook drawn by src/Hud.cpp (the night box, top-left).

    python3 tools/bloodmoon.py <out.png> [preview.gif]

8x6 frames of 192 px (1536x1152 atlas), 48 frames: played at 24 a second, one loop is 2 s.
"""
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates

N, COLS, ROWS = 192, 8, 6
FRAMES = COLS * ROWS
SS = 2


def tileable(n, beta, seed):
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(n)
    fx, fy = np.meshgrid(f, f)
    rad = np.sqrt(fx ** 2 + fy ** 2)
    rad[0, 0] = 1
    spec = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) / rad ** beta
    spec[0, 0] = 0
    out = np.real(np.fft.ifft2(spec))
    return (out - out.mean()) / out.std()


def sample(tex, u, v):
    n = tex.shape[0]
    return map_coordinates(tex, [(v * n) % n, (u * n) % n], order=1, mode='grid-wrap')


def ramp(h, stops):
    s = np.array(stops, np.float32)
    return np.stack([np.interp(h, s[:, 0], s[:, k]) for k in (1, 2, 3)], -1)


def craters(n, seed):
    """A tileable field of soft crater bowls with bright rims."""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    f = np.zeros((n, n), np.float32)
    for _ in range(70):
        cx, cy, rr = rng.random(), rng.random(), 0.015 + 0.07 * rng.random() ** 2
        dx = (xx - cx + 0.5) % 1 - 0.5
        dy = (yy - cy + 0.5) % 1 - 0.5
        d = np.sqrt(dx * dx + dy * dy) / rr
        f += np.where(d < 1, -(1 - d * d) * 0.8, 0) + np.exp(-((d - 1) / 0.18) ** 2) * 0.5
    return f


def frame(t, A, B, Cr):
    M = N * SS
    yy, xx = np.mgrid[0:M, 0:M].astype(np.float32)
    x = (xx + 0.5) / M * 2 - 1
    y = 1 - (yy + 0.5) / M * 2
    tau = 2 * np.pi * t
    r = np.hypot(x, y)
    th = np.arctan2(y, x)

    # --- the moon: a sphere of radius R, its surface turning one full tile per loop (so it loops), lit from the upper left
    R = 0.56
    inside = r < R
    zz = np.sqrt(np.clip(R * R - r * r, 0, None)) / R
    nx, ny = x / R, y / R
    lon = np.arctan2(nx, zz) / np.pi * 0.5          # (-0.25..0.25 across the face)
    lat = np.arcsin(np.clip(ny, -1, 1)) / np.pi
    u, v = lon + 0.3, lat + 0.5                     # (its face held still: the glow breathes and the mist drifts)
    maria = sample(A, u * 1.0, v * 1.0) * 0.6 + sample(B, u * 3.0, v * 3.0) * 0.25
    cr = sample(Cr, u * 2.0, v * 2.0)
    light = np.clip(0.2 + 0.9 * (-0.45 * nx + 0.5 * ny + 0.75 * zz), 0, 1.25)
    limb = np.clip(zz, 0, 1) ** 0.35
    tone = np.clip(0.6 + 0.2 * maria + 0.045 * cr, 0, 1) * light * (0.35 + 0.65 * limb)
    breath = 0.88 + 0.12 * np.cos(tau)               # (its red glow breathing once a loop)
    moon_rgb = ramp(np.clip(tone, 0, 1.2) * breath, [[0.0, 30, 2, 4], [0.3, 110, 10, 10], [0.6, 200, 35, 22],
                                                     [0.85, 245, 95, 60], [1.05, 255, 190, 150], [1.2, 255, 230, 210]])
    edge = np.clip((R - r) / (2.0 / N), 0, 1)
    moon_a = np.where(inside, edge, 0)

    # --- mist drifting left to right across it (whole tiles per loop)
    mist = sample(B, x * 0.7 - t, y * 2.2 + 0.3) * 0.6 + sample(A, x * 1.4 - 2 * t, y * 3.0) * 0.4
    band = np.exp(-((y + 0.08 + 0.06 * np.sin(x * 2 + tau)) / 0.16) ** 2) + 0.6 * np.exp(-((y - 0.25) / 0.09) ** 2)
    mist_a = np.clip((mist + 0.6) * 0.6, 0, 1) * band * np.clip(1 - r / 0.9, 0, 1) * 0.75
    mist_rgb = np.zeros(x.shape + (3,), np.float32) + [40, 10, 14]

    # --- the turning ring of dragon-script ticks (as the essence's, in blood red)
    ring_r, ring_w = 0.80, 0.045
    rot = th - tau / 12
    seg = (rot / (2 * np.pi) * 24) % 1.0
    k = np.floor((rot / (2 * np.pi) * 24) % 24).astype(int) % 24
    lens = np.array([0.6, 0.3, 0.8, 0.45, 0.25, 0.7, 0.5, 0.35, 0.9, 0.3, 0.55, 0.4] * 2)
    tick_on = (seg > 0.38) & (seg < 0.62)
    radial = np.abs(r - ring_r)
    ticks = tick_on & (r > ring_r - lens[k] * 0.09) & (r < ring_r + 0.01)
    band2 = np.clip(1 - np.abs(r - (ring_r + 0.03)) / 0.012, 0, 1) + np.clip(1 - np.abs(r - (ring_r - 0.11)) / 0.009, 0, 1) * 0.7
    ring_a = np.clip(band2 + ticks * np.clip((ring_w - radial * 0.3) / ring_w, 0, 1), 0, 1)
    pulse = 0.7 + 0.3 * np.cos(rot * 3 - tau)
    ring_rgb = np.array([230, 60, 45], np.float32) * pulse[..., None] + np.array([40, 10, 10], np.float32)

    # --- its halo
    glow_a = np.clip(np.exp(-((r - 0.45) / 0.32) ** 2) * 0.5 * (0.85 + 0.15 * np.cos(tau)), 0, 1)
    glow_rgb = np.zeros_like(moon_rgb) + [200, 25, 15]

    rgb = glow_rgb * glow_a[..., None]
    a = glow_a
    for c_rgb, c_a in ((ring_rgb, ring_a * 0.9), (moon_rgb, moon_a), (mist_rgb, mist_a)):
        rgb = c_rgb * c_a[..., None] + rgb * (1 - c_a[..., None])
        a = c_a + a * (1 - c_a)
    a = np.clip(a, 0, 1)
    rgb = np.where(a[..., None] > 1e-4, rgb / np.maximum(a[..., None], 1e-4), 0)
    img = np.dstack([np.clip(rgb, 0, 255), a * 255]).astype(np.float32)
    img = img.reshape(N, SS, N, SS, 4).mean(axis=(1, 3))
    return img.astype(np.uint8)


def main():
    out = sys.argv[1]
    A, B = tileable(256, 2.2, 21), tileable(256, 1.7, 22)
    Cr = craters(256, 23)
    Cr = (Cr - Cr.mean()) / Cr.std()
    atlas = np.zeros((N * ROWS, N * COLS, 4), np.uint8)
    frames = []
    for i in range(FRAMES):
        f = frame(i / FRAMES, A, B, Cr)
        rr, cc = divmod(i, COLS)
        atlas[rr * N:(rr + 1) * N, cc * N:(cc + 1) * N] = f
        frames.append(f)
    Image.fromarray(atlas, 'RGBA').save(out, optimize=True)
    if len(sys.argv) > 2:
        bg = []
        for f in frames:
            base = np.zeros((N, N, 3), np.float32) + [16, 12, 12]
            al = f[..., 3:4] / 255.0
            bg.append(Image.fromarray(np.clip(base * (1 - al) + f[..., :3] * al, 0, 255).astype(np.uint8)).resize((N * 2, N * 2), Image.LANCZOS))
        bg[0].save(sys.argv[2], save_all=True, append_images=bg[1:], duration=42, loop=0)
    print('bloodmoon: wrote', out)


main()
