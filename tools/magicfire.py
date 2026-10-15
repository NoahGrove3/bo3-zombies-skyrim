#!/usr/bin/env python3
"""The shrine orb's aura: a fireball of purple magic flame - the orb wrapped in fire that climbs up its sides and
rises in tall, wispy tongues over it (Noah's reference: a ball engulfed in flame, the fire streaming upward) - a
slow, seamlessly looping flipbook for an always-facing quad (tools/shrine.py aura -> crystal.nif; the main menu draws
the same atlas, src/Hud.cpp DrawMenu), rendered additive by the game.

    python3 tools/magicfire.py <out.png> [preview.gif]

8x8 frames of 256 px (2048 px atlas), 64 frames: played at 12 a second, one loop is ~5.3 s - slow, as the rocks.
Each frame: x and y in -1..1 (y up); the orb's centre at (0, ORB_Y), radius ORB_R; the flames reach to the top.
"""
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates

N, COLS = 256, 8
FRAMES = COLS * COLS
ORB_Y, ORB_R = -0.30, 0.36


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
    y = 1 - (yy + 0.5) / N * 2
    dy = y - ORB_Y
    r = np.hypot(x, dy)
    # the fire's body: round about the orb, drawn up over it into a tall teardrop whose tip licks at the top
    top = 1.0 - ORB_Y                                        # how far up the flames can climb above the orb's centre
    above = np.clip(dy / top, 0, 1)
    halfw = np.maximum((ORB_R + 0.30) * (1 - above ** 1.15) * (1 + 0.25 * np.sin(above * 3.1416)), 0.02)       # the fire's half-width at this height
    below = np.clip(-dy / (ORB_R + 0.2), 0, 1)
    body = np.where(dy > 0, np.abs(x) / halfw, r / (ORB_R + 0.30 - 0.10 * below))
    # rising, swaying flame: noise stretched upward that climbs (whole periods per loop, so it loops), bent sideways
    sway = sample(C, x * 0.35 + 0.5, y * 0.25 - t * 1) * 0.10
    lick = sample(A, (x + sway) * 1.9, y * 0.40 - t * 2)          # the big tongues
    wisp = sample(B, (x + sway * 1.5) * 2.6, y * 0.9 - t * 4)     # thin wisps and their frayed edges
    n = 0.70 * lick + 0.40 * wisp
    reach = 1.0 + 0.35 * np.tanh(n)                               # tongues reach further, gaps fall short
    heat = np.clip(1.0 - body / reach, 0, 1)
    heat = heat ** (0.7 + 0.5 * above)                            # thinning as it rises
    heat *= np.clip(0.75 + 0.45 * np.tanh(n + 0.4), 0, 1.2)         # broken into flames, not a smooth blob
    heat *= np.clip((1 - np.maximum(np.abs(x), np.abs(y))) / 0.08, 0, 1)   # nothing at the frame's edge
    # the fire that clings to the orb: a skin of flame hugging its edge all round, licking up along the curve of it
    # (from underneath, round the sides, into the plume) - so the fire comes out of the orb, not from behind it
    phi = np.arctan2(x, dy)                                       # 0 straight up, +-pi straight down
    along = (np.pi - np.abs(phi)) / np.pi                         # 0 at the bottom .. 1 at the top, up either side
    side = np.sign(phi + 1e-6)
    s_out = r - ORB_R                                             # outside the edge (<0: just inside it, over the limb)
    cling_n = sample(B, side * s_out * 2.2 + 0.25 * side, along * 1.6 - t * 3) * 0.75 + sample(A, along * 3.1 + 0.4 * side, s_out * 1.3 - t * 2) * 0.45
    thick = 0.05 + 0.20 * along ** 1.4                            # thin underneath, swelling as it climbs the sides
    cling = np.clip(1.0 - np.maximum(s_out, 0) / (thick * (1.0 + 0.6 * np.tanh(cling_n))), 0, 1)
    cling *= np.clip((s_out + 0.07) / 0.07, 0, 1)                 # it starts just inside the edge (over the limb)
    cling = cling ** 1.2 * np.clip(0.75 + 0.5 * np.tanh(cling_n + 0.3), 0, 1.2)
    # over the orb's face only a thin veil (the orb shows through); the edge itself burns brightest
    face = np.clip((r - ORB_R * 0.80) / (ORB_R * 0.22), 0, 1)
    rim = np.exp(-np.abs(r - ORB_R) / 0.03) * 0.6
    heat = np.maximum(heat * (0.25 + 0.75 * face), cling * 1.05) + rim * np.clip(0.7 + 0.5 * n, 0, 1)
    a = np.clip(heat * 1.35, 0, 1) ** 1.1
    # colour by heat: deep violet wisps -> purple -> lilac -> white-hot at the roots
    stops = np.array([[0.00, 45, 0, 100], [0.25, 105, 25, 205], [0.55, 165, 70, 255], [0.85, 215, 150, 255], [1.20, 250, 235, 255]], np.float32)
    h = np.clip(heat * 1.1, 0, 1.2)
    rgb = np.stack([np.interp(h, stops[:, 0], stops[:, k]) for k in (1, 2, 3)], -1)
    return np.dstack([rgb, a * 255]).astype(np.uint8)


def main():
    out = sys.argv[1]
    A, B, C = tileable(256, 2.0, 1), tileable(256, 1.6, 2), tileable(256, 2.4, 3)
    atlas = np.zeros((N * COLS, N * COLS, 4), np.uint8)
    frames = []
    for k in range(FRAMES):
        f = frame(k / FRAMES, A, B, C)
        r, c = divmod(k, COLS)
        atlas[r * N:(r + 1) * N, c * N:(c + 1) * N] = f
        frames.append(f)
    Image.fromarray(atlas, 'RGBA').save(out)
    if len(sys.argv) > 2:  # a preview over a dark ground, with a stand-in orb
        yy, xx = np.mgrid[0:N, 0:N]
        orb = np.hypot((xx + 0.5) / N * 2 - 1, (1 - (yy + 0.5) / N * 2) - ORB_Y) < ORB_R
        bg = []
        for f in frames:
            base = np.zeros((N, N, 3), np.float32) + [12, 10, 24]
            base[orb] = [120, 170, 240]
            al = f[..., 3:4] / 255.0
            bg.append(Image.fromarray(np.clip(base + f[..., :3] * al, 0, 255).astype(np.uint8)))
        bg[0].save(sys.argv[2], save_all=True, append_images=bg[1:], duration=83, loop=0)
    print('magicfire: wrote', out)


main()
