#!/usr/bin/env python3
"""Two more looping flipbooks for the shrine (8x8 frames of 256 px, 64 frames, played at 12 a second = ~5.3 s):

  orb_swirl   the orb's living light: veins of energy drifting over a ball as it slowly turns, a soft core and a
              bright limb - additive, on an always-facing quad just in front of the in-game orb (tools/shrine.py),
              so the orb itself is alive, not a still texture turning
  motes       motes of light rising from the shrine's pool up to the orb: a column of sparks spiralling up,
              swelling and fading - drawn tall (the quad is twice as tall as wide; the frames are drawn squashed for it)

    python3 tools/orbfx.py <out dir> [preview]
"""
import math, pathlib, sys
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates

N, COLS = 256, 8
FRAMES = COLS * COLS


def tileable(n, beta, seed):
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(n)
    fx, fy = np.meshgrid(f, f)
    rad = np.sqrt(fx ** 2 + fy ** 2)
    rad[0, 0] = 1
    spec = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) / rad ** beta
    spec[0, 0] = 0
    o = np.real(np.fft.ifft2(spec))
    return ((o - o.mean()) / o.std()).astype(np.float32)


def sample(tex, u, v):
    n = tex.shape[0]
    return map_coordinates(tex, [(v * n) % n, (u * n) % n], order=1, mode='grid-wrap')


def atlas_of(frames):
    a = np.zeros((N * COLS, N * COLS, 4), np.uint8)
    for k, f in enumerate(frames):
        r, c = divmod(k, COLS)
        a[r * N:(r + 1) * N, c * N:(c + 1) * N] = f
    return Image.fromarray(a, 'RGBA')


def orb_swirl():
    A, B, C = tileable(256, 2.7, 11), tileable(256, 2.4, 12), tileable(256, 2.6, 13)
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    x = (xx + 0.5) / N * 2 - 1
    y = 1 - (yy + 0.5) / N * 2
    r = np.hypot(x, y)
    inside = r < 1.0
    z = np.sqrt(np.clip(1 - r * r, 0, 1))
    lat = np.arcsin(np.clip(y, -1, 1)) / np.pi + 0.5          # 0..1 bottom to top
    frames = []
    for k in range(FRAMES):
        t = k / FRAMES
        lon = (np.arctan2(x, z) / (2 * np.pi)) + t * 0.25        # a quarter turn per loop (the noise repeats 4x round)
        w = sample(C, lon * 4 + 0.3, lat * 2 + t * 1) * 0.06    # the swirl folds as it drifts
        e = sample(A, lon * 4 + w, lat * 1.5 - t * 1 + w)
        f = sample(B, lon * 8 - w * 2, lat * 3 + t * 2)
        veins = np.clip(1 - np.abs(e) * 2.4, 0, 1) ** 3.0        # thin bright lines where the field crosses zero
        fine = np.clip(1 - np.abs(f) * 3.0, 0, 1) ** 4 * 0.35
        core = np.clip(1 - r, 0, 1) ** 1.5
        limb = np.clip((r - 0.78) / 0.22, 0, 1) ** 2 * np.clip((1.0 - r) / 0.04, 0, 1)
        light = (veins * 0.95 + fine) * (0.35 + 0.65 * z) + core * 0.25 + limb * 0.55
        light *= inside * np.clip((1.0 - r) / 0.03, 0, 1)
        a = np.clip(light, 0, 1)
        rgb = np.dstack([110 + 145 * a, 180 + 75 * a, np.full_like(a, 255)])
        frames.append(np.dstack([rgb, a * 255]).astype(np.uint8))
    return frames


def motes(count=70, seed=21):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    x = (xx + 0.5) / N * 2 - 1
    y = 1 - (yy + 0.5) / N * 2
    P = [dict(off=rng.uniform(0, 1), loops=int(rng.integers(1, 3)), r0=rng.uniform(0.15, 0.75), ph=rng.uniform(0, 6.283),
              turns=rng.uniform(0.6, 1.6) * rng.choice([-1, 1]), size=rng.uniform(0.008, 0.02), hot=rng.uniform(0, 1)) for _ in range(count)]
    frames = []
    for k in range(FRAMES):
        t = k / FRAMES
        acc = np.zeros((N, N), np.float32)
        hot = np.zeros((N, N), np.float32)
        for p in P:
            for trail in range(4):  # a short trail of fading copies under each mote
                life = (t * p['loops'] + p['off'] - trail * 0.012) % 1.0
                yy_ = -1.0 + 2.1 * life                                   # up from the pool past the orb
                rad = p['r0'] * (1 - 0.65 * life)                          # drawn in toward the orb as it climbs
                ang = p['ph'] + p['turns'] * 2 * np.pi * life
                xx_ = math.sin(ang) * rad
                depth = 0.6 + 0.4 * math.cos(ang)                          # the far side of the spiral dimmer
                fade = math.sin(math.pi * min(1.0, life)) ** 0.8 * depth * (0.55 ** trail)
                s = p['size'] * (1 - 0.15 * trail)
                d2 = (x - xx_) ** 2 + ((y - yy_) * 0.5) ** 2                  # (squashed: the quad is twice as tall)
                acc += fade * (np.exp(-d2 / (s * s)) + 0.25 * np.exp(-d2 / (s * s * 9)))
                hot += fade * p['hot'] * np.exp(-d2 / (s * s * 0.3))
        a = np.clip(acc, 0, 1)
        h = np.clip(hot, 0, 1)
        rgb = np.dstack([120 + 120 * h + 60 * a, 190 + 50 * h + 20 * a, np.full_like(a, 255)])
        frames.append(np.dstack([np.clip(rgb, 0, 255), a * 255]).astype(np.uint8))
    return frames


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    sw, mo = orb_swirl(), motes()
    atlas_of(sw).save(out / 'orb_swirl.png')
    atlas_of(mo).save(out / 'motes.png')
    if len(sys.argv) > 2:
        strip = Image.new('RGB', (N * 6, N), (10, 10, 20))
        for i, k in enumerate((0, 20, 40)):
            for j, fr in enumerate((sw, mo)):
                f = fr[k].astype(np.float32)
                strip.paste(Image.fromarray(np.clip(np.array([10, 10, 20]) + f[..., :3] * f[..., 3:4] / 255, 0, 255).astype(np.uint8)), ((i * 2 + j) * N, 0))
        strip.save(sys.argv[2])
    print('orbfx: wrote orb_swirl.png, motes.png')


main()
