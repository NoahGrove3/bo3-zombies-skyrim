#!/usr/bin/env python3
"""The Cursed Essence emblem for the HUD: a soul caught in Alduin's curse - a burning wisp of spectral violet flame
with a slit dragon's eye in its heart, ringed by a slowly turning circle of dragon-script ticks. A seamlessly looping
flipbook drawn by src/Hud.cpp (the essence counter, bottom-left).

    python3 tools/essence.py <out.png> [preview.gif]

8x6 frames of 192 px (1536x1152 atlas), 48 frames: played at 24 a second, one loop is 2 s.
"""
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates, gaussian_filter

N, COLS, ROWS = 192, 8, 6
FRAMES = COLS * ROWS
SS = 2  # supersampling


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


def frame(t, A, B, C):
    M = N * SS
    yy, xx = np.mgrid[0:M, 0:M].astype(np.float32)
    x = (xx + 0.5) / M * 2 - 1
    y = 1 - (yy + 0.5) / M * 2
    tau = 2 * np.pi * t
    r = np.hypot(x, y)
    th = np.arctan2(y, x)

    # --- the soul-flame: a teardrop rising from a core at (0, -0.12), its tongues climbing (whole periods per loop)
    cy = -0.14
    dy = y - cy
    rr = np.hypot(x, dy)
    above = np.clip(dy / 0.78, 0, 1)
    halfw = np.maximum(0.46 * (1 - above ** 1.1) * (1 + 0.35 * np.sin(above * 3.1416)), 0.0)
    body = np.where(dy > 0, np.abs(x) / np.maximum(halfw, 1e-3), rr / 0.46)
    sway = sample(C, x * 0.3 + 0.5, y * 0.25 - t) * 0.12
    lick = sample(A, (x + sway) * 1.6, y * 0.45 - t * 2)
    wisp = sample(B, (x + sway * 1.4) * 2.4, y * 0.9 - t * 3)
    n = 0.7 * lick + 0.45 * wisp
    heat = np.clip(1 - body / (1 + 0.38 * np.tanh(n)), 0, 1)
    heat = heat ** (0.6 + 0.5 * above) * np.clip((0.72 - dy) / 0.15, 0, 1)
    heat *= np.clip(0.75 + 0.45 * np.tanh(n + 0.5), 0, 1.2)
    core = np.clip(1 - body / 0.95, 0, 1) ** 0.6 * np.clip((0.55 - dy) / 0.3, 0, 1)  # a steady heart under the tongues
    heat = np.maximum(heat, core * 0.85)
    flame_rgb = ramp(np.clip(heat * 1.15, 0, 1.2), [[0.0, 30, 0, 70], [0.25, 95, 20, 190], [0.5, 150, 60, 255],
                                                   [0.75, 110, 200, 255], [1.0, 205, 255, 245], [1.2, 255, 255, 255]])
    flame_a = np.clip(heat * 1.5, 0, 1)

    # --- the dragon's eye in its heart: an almond of molten gold-green with a slit pupil that breathes
    ex, ey = x / 0.28, (y - cy + 0.02) / 0.15
    almond = 1 - (np.abs(ex) ** 2 + np.abs(ey) ** 2 * (1 + 0.6 * np.abs(ex)))
    eye_a = np.clip(almond / 0.08, 0, 1)
    breathe = 0.5 + 0.5 * np.sin(tau)
    slit_w = 0.035 + 0.035 * breathe
    slit = np.clip((slit_w * (1 - np.abs(ey * 0.9) ** 2) - np.abs(ex * 0.30)) / 0.012, 0, 1) * eye_a
    iris_r = np.hypot(ex, ey)
    streak = sample(A, th * 0.6366, iris_r * 0.25 - t)  # (th/pi*2: radial streaks round the iris)
    iris_h = np.clip(1.15 - iris_r * 0.95 + 0.22 * streak, 0, 1.2)
    iris_rgb = ramp(iris_h, [[0.0, 80, 10, 40], [0.35, 200, 60, 20], [0.7, 255, 170, 40], [1.0, 255, 240, 160], [1.2, 255, 255, 230]])
    iris_rgb = iris_rgb * (1 - slit[..., None]) + np.array([8, 0, 14], np.float32) * slit[..., None]
    lid = np.clip((0.12 - np.abs(almond - 0.04)) / 0.12, 0, 1) * np.clip(1 - almond / 0.25, 0, 1)  # dark lid rim

    # --- the turning ring of dragon-script ticks (12-fold: one twelfth of a turn per loop, so it loops)
    ring_r, ring_w = 0.80, 0.045
    rot = th - tau / 12
    seg = (rot / (2 * np.pi) * 24) % 1.0
    k = np.floor((rot / (2 * np.pi) * 24) % 24).astype(int) % 24
    lens = np.array([0.6, 0.3, 0.8, 0.45, 0.25, 0.7, 0.5, 0.35, 0.9, 0.3, 0.55, 0.4] * 2)
    tick_on = (seg > 0.38) & (seg < 0.62)
    radial = np.abs(r - ring_r)
    ticklen = lens[k] * 0.09
    ticks = tick_on & (r > ring_r - ticklen) & (r < ring_r + 0.01)
    band = np.clip(1 - np.abs(r - (ring_r + 0.03)) / 0.012, 0, 1) + np.clip(1 - np.abs(r - (ring_r - 0.11)) / 0.009, 0, 1) * 0.7
    ring_a = np.clip(band + ticks * np.clip((ring_w - radial * 0.3) / ring_w, 0, 1), 0, 1)
    pulse = 0.7 + 0.3 * np.cos(rot * 3 - tau)  # light running round it
    ring_rgb = np.array([170, 120, 255], np.float32) * pulse[..., None] + np.array([50, 40, 60], np.float32)

    # --- an outer glow
    glow_a = np.clip(np.exp(-((r - 0.05) / 0.55) ** 2) * 0.45, 0, 1)
    glow_rgb = np.zeros_like(flame_rgb) + [90, 40, 200]

    # composite (straight alpha, over)
    rgb = glow_rgb * glow_a[..., None]
    a = glow_a
    for c_rgb, c_a in ((ring_rgb, ring_a * 0.9), (flame_rgb, flame_a)):
        rgb = c_rgb * c_a[..., None] + rgb * (1 - c_a[..., None])
        a = c_a + a * (1 - c_a)
    lid_a = lid * 0.85
    rgb = np.array([20, 0, 30], np.float32) * lid_a[..., None] + rgb * (1 - lid_a[..., None])
    a = lid_a + a * (1 - lid_a)
    rgb = iris_rgb * eye_a[..., None] + rgb * (1 - eye_a[..., None])
    a = eye_a + a * (1 - eye_a)
    # a soft bloom of the eye's light over the flame
    bloom = gaussian_filter(eye_a * (1 - slit), 6 * SS) * 0.6
    rgb = rgb + np.array([255, 160, 60], np.float32) * bloom[..., None] * 0.5
    a = np.maximum(a, bloom * 0.8)
    a = np.clip(a, 0, 1)
    rgb = np.where(a[..., None] > 1e-4, rgb / np.maximum(a[..., None], 1e-4), 0)  # (premultiplied -> straight)
    img = np.dstack([np.clip(rgb, 0, 255), np.clip(a, 0, 1) * 255]).astype(np.float32)
    # downsample
    img = img.reshape(N, SS, N, SS, 4).mean(axis=(1, 3))
    return img.astype(np.uint8)


def main():
    out = sys.argv[1]
    A, B, C = tileable(256, 2.0, 11), tileable(256, 1.6, 12), tileable(256, 2.4, 13)
    atlas = np.zeros((N * ROWS, N * COLS, 4), np.uint8)
    frames = []
    for k in range(FRAMES):
        f = frame(k / FRAMES, A, B, C)
        r, c = divmod(k, COLS)
        atlas[r * N:(r + 1) * N, c * N:(c + 1) * N] = f
        frames.append(f)
    Image.fromarray(atlas, 'RGBA').save(out, optimize=True)
    if len(sys.argv) > 2:
        bg = []
        for f in frames:
            base = np.zeros((N, N, 3), np.float32) + [14, 12, 20]
            al = f[..., 3:4] / 255.0
            im = Image.fromarray(np.clip(base * (1 - al) + f[..., :3] * al, 0, 255).astype(np.uint8))
            bg.append(im.resize((N * 2, N * 2), Image.LANCZOS))
        bg[0].save(sys.argv[2], save_all=True, append_images=bg[1:], duration=42, loop=0)
    print('essence: wrote', out)


main()
