"""The dragon's breath in its bond's colour (src/Movement.cpp: TintBreath): greyscale-to-palette ramps the game swaps into
the fire breath's effect shaders - the flame's brightness picks a colour along the ramp, dark (left) to white-hot (right).
    python tools/firepalette.py <Textures dir>
(Noah) before any bond: Skyrim's own fire; Bond I blue (fire_b1.dds), Bond II yellow (fire_b2.dds), Bond III green (fire_b3.dds)."""
import sys, pathlib
import numpy as np
from PIL import Image

RAMPS = {
    # (position along the ramp, colour) - the dim smoke-edge of the flame, its body, its white-hot heart
    'fire_b1': [(0.0, (0, 0, 0)), (0.18, (10, 30, 95)), (0.45, (35, 105, 240)), (0.72, (110, 185, 255)), (1.0, (230, 245, 255))],
    'fire_b2': [(0.0, (0, 0, 0)), (0.18, (90, 55, 0)), (0.45, (230, 175, 10)), (0.72, (255, 225, 70)), (1.0, (255, 252, 215))],
    'fire_b3': [(0.0, (0, 0, 0)), (0.18, (5, 60, 10)), (0.45, (40, 200, 40)), (0.72, (130, 255, 110)), (1.0, (230, 255, 220))],
}


def ramp(stops, n=256):
    x = (np.arange(n) + 0.5) / n
    out = np.zeros((n, 3))
    for c in range(3):
        out[:, c] = np.interp(x, [s[0] for s in stops], [s[1][c] for s in stops])
    return out


def main():
    d = pathlib.Path(sys.argv[1]) / 'CurseOfAlduin' / 'Dragon'
    d.mkdir(parents=True, exist_ok=True)
    for name, stops in RAMPS.items():
        row = ramp(stops)
        img = np.zeros((4, 256, 4), np.uint8)
        img[..., :3] = row[None, :, :].astype(np.uint8)
        img[..., 3] = 255
        Image.fromarray(img, 'RGBA').save(d / f'{name}.dds')  # (uncompressed: a ramp must not band)
        print(d / f'{name}.dds')


if __name__ == '__main__':
    main()
