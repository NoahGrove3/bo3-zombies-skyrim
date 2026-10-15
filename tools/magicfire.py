#!/usr/bin/env python3
"""The shrine orb's aura: purple fire made of magic, licking up and out round the orb - a looping flipbook for an
always-facing quad (tools/shrine.py aura -> crystal.nif), rendered additive by the game.

    python3 tools/magicfire.py <out.png> [preview.gif]

8x8 frames of 256 px (2048 px atlas), 64 frames looping seamlessly. The orb sits in the middle of each frame with
radius R0 (of the half-size); the flames rise from its rim, tallest over the top.
"""
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates

N, COLS = 256, 8
FRAMES = COLS * COLS
R0 = 0.42  # the orb's radius, in half-sizes of the frame


def tileable(n, beta, seed):
    """seamless noise (periodic both ways): random spectrum falling off as 1/f^beta"""
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


def frame(t, A, B, C):
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    x = (xx + 0.5) / N * 2 - 1
    y = 1 - (yy + 0.5) / N * 2          # +1 at the top
    r = np.hypot(x, y)
    th = np.arctan2(x, y)               # 0 straight up
    up = np.clip(y / np.maximum(r, 1e-4), -1, 1)   # 1 over the top, -1 under
    u = th / (2 * np.pi)
    reach = 0.30 + 0.42 * np.clip(up, 0, 1) ** 1.2 + 0.06 * (up + 1)   # how far the flames reach: far over the top
    sd = (r - R0) / reach               # 0 at the orb's rim, 1 at the flames' usual reach
    # the fire flows outward (and up): noise stretched along the flow (long tongues), sliding by whole periods per loop
    v = sd * 0.55 - y * 0.25
    w = sample(C, u * 2 + 0.31, v * 0.5 - t * 1) * 0.05
    T = sample(A, u * 7 + w, v * 0.9 - t * 3)                  # the tongues: smooth, long
    D = sample(B, u * 16 - w * 3, v * 2.2 - t * 6)              # ragged licks at their edges
    tongue = 0.55 + 0.45 * np.tanh(T * 0.9) + 0.10 * D          # how far this tongue reaches (1 = usual)
    heat = np.clip(1.0 - sd / np.maximum(tongue * 1.25, 0.05), 0, 1)
    heat = heat ** 1.15 * (0.85 + 0.25 * np.tanh(T))
    heat *= np.clip((r - R0 * 0.9) / (R0 * 0.15), 0, 1)       # the orb itself shows through
    heat *= np.clip((1.0 - r) / 0.10, 0, 1)                    # nothing at the frame's edge
    glow = np.exp(-np.clip(r - R0, 0, None) / 0.07) * np.clip((r - R0 * 0.9) / (R0 * 0.1), 0, 1) * 0.6
    a = np.clip(np.maximum(np.clip(heat * 1.6, 0, 1) ** 1.2, glow), 0, 1)
    heat = heat * 1.15
    # colour by heat: deep violet embers -> purple -> magenta-lilac -> white-hot cores
    stops = np.array([[0.00, 50, 0, 110], [0.25, 110, 20, 200], [0.55, 170, 60, 255], [0.85, 220, 140, 255], [1.20, 255, 235, 255]], np.float32)
    h = np.clip(heat, 0, 1.2)
    rgb = np.stack([np.interp(h, stops[:, 0], stops[:, k]) for k in (1, 2, 3)], -1)
    return np.dstack([rgb, a * 255]).astype(np.uint8)


def main():
    out = sys.argv[1]
    A, B, C = tileable(256, 2.0, 1), tileable(256, 1.6, 2), tileable(256, 2.2, 3)
    atlas = np.zeros((N * COLS, N * COLS, 4), np.uint8)
    frames = []
    for k in range(FRAMES):
        f = frame(k / FRAMES, A, B, C)
        r, c = divmod(k, COLS)
        atlas[r * N:(r + 1) * N, c * N:(c + 1) * N] = f
        frames.append(f)
    Image.fromarray(atlas, 'RGBA').save(out)
    if len(sys.argv) > 2:  # a preview over a dark ground, with a stand-in orb
        bg = []
        for f in frames:
            base = np.zeros((N, N, 3), np.float32) + [12, 10, 24]
            yy, xx = np.mgrid[0:N, 0:N]
            orb = np.hypot(xx - N / 2, yy - N / 2) < R0 * N / 2
            base[orb] = [140, 190, 255]
            al = f[..., 3:4] / 255.0
            bg.append(Image.fromarray(np.clip(base + f[..., :3] * al, 0, 255).astype(np.uint8)))
        bg[0].save(sys.argv[2], save_all=True, append_images=bg[1:], duration=42, loop=0)
    print('magicfire: wrote', out)


main()
