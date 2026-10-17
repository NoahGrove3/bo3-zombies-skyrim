"""The dragon's breath in its bond's colour (src/Movement.cpp: TintBreath): greyscale-to-palette ramps the game swaps into
the fire breath's effect shaders - the flame's brightness picks a colour along the ramp, dark (left) to white-hot (right).
    python tools/firepalette.py <Textures dir>
Writes Textures/CurseOfAlduin/Dragon/fire_b1.dds (Bond I, the snow dragon: pale ice-blue fire) and fire_b2.dds (Bond II,
the glacier dragon: deep glacier-blue fire). Bond III, the black dragon with ember eyes, keeps Skyrim's own red fire."""
import sys, pathlib
import numpy as np
from PIL import Image

RAMPS = {
    # (position along the ramp, colour) - the dim smoke-edge of the flame, its body, its white-hot heart
    'fire_b1': [(0.0, (0, 0, 0)), (0.18, (20, 45, 90)), (0.45, (70, 150, 235)), (0.72, (150, 215, 255)), (1.0, (245, 252, 255))],
    'fire_b2': [(0.0, (0, 0, 0)), (0.18, (5, 25, 85)), (0.45, (25, 95, 230)), (0.72, (80, 170, 255)), (1.0, (215, 240, 255))],
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
