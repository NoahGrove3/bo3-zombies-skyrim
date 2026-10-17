"""The dragon's breath in its bond's colour (src/Movement.cpp: TintBreath): greyscale-to-palette ramps the game swaps into
the fire breath's effect shaders - the flame's brightness picks a colour along the ramp, dark (left) to white-hot (right).
    python tools/firepalette.py <Textures dir>
(Noah) before any bond: Skyrim's own fire; Bond I blue (fire_b1.dds), Bond II yellow (fire_b2.dds), Bond III green (fire_b3.dds)."""
import sys, pathlib
import numpy as np
from PIL import Image

RAMPS = {
    # (position along the ramp, colour) - the dim smoke-edge of the flame, its body, its hottest heart; kept saturated
    # to the top (the breath is drawn additively: a white heart washed every colour out)
    'fire_b1': [(0.0, (0, 0, 0)), (0.2, (4, 18, 80)), (0.5, (15, 70, 210)), (0.8, (45, 135, 255)), (1.0, (110, 185, 255))],
    'fire_b2': [(0.0, (0, 0, 0)), (0.2, (70, 40, 0)), (0.5, (200, 140, 0)), (0.8, (250, 200, 20)), (1.0, (255, 235, 110))],
    'fire_b3': [(0.0, (0, 0, 0)), (0.2, (0, 50, 6)), (0.5, (20, 170, 25)), (0.8, (70, 235, 60)), (1.0, (150, 255, 130))],
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
