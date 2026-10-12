"""The living aura round a spell emblem: a ring of white flame that licks upward, 16 frames that loop seamlessly
(4x4 atlas of 256x256 frames). The HUD tints it with the spell's colour (and a hotter inner pass with its accent) and
plays it round the medallion, whose rim sits at RING of the frame's half-width.

    python tools/flameaura.py <out dir>      writes flame_ring.png (1024x1024 RGBA)
"""
import pathlib, sys
import numpy as np
from PIL import Image

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'ui')
N, FRAMES, RING = 256, 16, 0.62   # frame size, frame count, medallion rim (fraction of the half-width)


def periodic_noise(n, seed, beta=2.2):
    """seamless fbm (spectral synthesis): tiles in both directions, so it can scroll and loop"""
    r = np.random.default_rng(seed)
    f = np.fft.fftfreq(n)[:, None] ** 2 + np.fft.fftfreq(n)[None, :] ** 2
    f[0, 0] = 1
    x = np.real(np.fft.ifft2(np.fft.fft2(r.normal(size=(n, n))) / f ** (beta / 2)))
    return (x - x.min()) / np.ptp(x)


def main():
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    x = (xx + 0.5) / N * 2 - 1; y = (yy + 0.5) / N * 2 - 1
    r = np.sqrt(x * x + y * y); up = np.clip(-y / np.maximum(r, 1e-3), -1, 1)       # 1 straight up, -1 straight down
    n1 = periodic_noise(N, 3, 1.8); n2 = periodic_noise(N, 7, 1.4)
    ang = np.arctan2(x, -y)                                                             # 0 at the top, round both ways
    atlas = np.zeros((N * 4, N * 4, 4), np.float32)
    for i in range(FRAMES):
        t = i / FRAMES
        # polar flame field: angle round the ring and distance out from it; the noise rises outward and loops
        u = ((ang / (2 * np.pi)) % 1.0)                                                   # 0..1 round the ring
        out = r - RING
        iu = (u * N).astype(int) % N
        iv1 = ((out * 0.9 - t) * N).astype(int) % N                                       # one tile outward per loop
        iv2 = ((out * 1.6 - 2 * t) * N).astype(int) % N
        turb = 0.6 * n1[iv1, iu] + 0.4 * n2[iv2, (iu * 2) % N]
        lick = (0.5 + 0.5 * np.sin(u * 2 * np.pi * 11 + turb * 6 - t * 2 * np.pi)) ** 1.8  # eleven pointed tongues, wavering
        reach = (0.13 + 0.26 * np.clip(up, 0, 1) ** 1.3 + 0.03 * np.clip(-up, 0, 1)) * (0.45 + 0.9 * turb) * (0.35 + 0.95 * lick)
        f = 1 - out / np.maximum(reach, 1e-3)
        tongue = np.clip(f, 0, 1) * (out > -0.01)
        body = np.clip((tongue - 0.15) * 2.4, 0, 1) ** 1.2                                # crisp flame bodies
        core = np.clip((tongue - 0.6) * 3.0, 0, 1)                                        # the hot root of each tongue
        rim = np.exp(-((r - RING) / 0.03) ** 2)
        a = np.clip(body * 0.95 + rim * 0.9 + core * 0.3, 0, 1) * (r < 1.0)
        heat = np.clip(core + rim * 0.8, 0, 1)
        rgb = np.dstack([np.ones_like(a)] * 3) * (0.78 + 0.22 * heat)[..., None]  # white: the HUD tints it
        gy, gx = divmod(i, 4)
        atlas[gy * N:(gy + 1) * N, gx * N:(gx + 1) * N] = np.dstack([rgb, a])
    OUT.mkdir(parents=True, exist_ok=True)
    Image.fromarray((atlas * 255).astype(np.uint8), 'RGBA').save(OUT / 'flame_ring.png')
    print('flame_ring.png')


if __name__ == '__main__':
    main()
