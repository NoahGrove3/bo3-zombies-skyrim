"""(Noah, Nov 28: "The tally marks themselves needs to look higher quality and textured" - his pick: A, Carved runes) the night's
tally strokes as grooves cut into dark stone with ember glowing deep inside: four variants, each a horizontal stroke 512 x 64
(the stroke runs along x; the HUD lays each one along its mark and cuts the newest in along u).

    python3 tools/tallytex.py            -> assets/ui/tally.png (512 x 256, RGBA)"""
import pathlib
import numpy as np
from PIL import Image

OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'ui' / 'tally.png'
W, H, N = 512, 64, 4


def noise(w, h, cell, seed):
    r = np.random.default_rng(seed)
    a = r.random((h // cell + 2, w // cell + 2))
    return np.asarray(Image.fromarray((a * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)).astype(float) / 255


def stroke(seed):
    x = np.linspace(0, 1, W)[None, :]
    y = np.linspace(-1, 1, H)[:, None]
    # the chisel's path: thickest a little before the middle, tapering to points, the edges chipped
    taper = np.clip(np.minimum(x / 0.10, (1 - x) / 0.14), 0, 1) ** 0.55 * (0.9 + 0.1 * np.sin(x * 11 + seed))
    chip = noise(W, H, 10, seed) * 2 - 1
    grain = noise(W, H, 3, seed + 7)
    fine = noise(W, H, 1, seed + 13)
    half = 0.62 * taper * (1 + 0.28 * chip)          # the groove's half-width, in -1..1 of the strip
    d = np.abs(y + 0.06 * chip) / np.maximum(half, 1e-3)   # 0 at the groove's floor, 1 at its lip
    inside = np.clip((1.0 - d) * 6, 0, 1)
    lip = np.clip((1.35 - d) * 4, 0, 1) - inside            # the broken stone round the cut
    # the ember deep in the groove: hottest at its floor, mottled; the walls darker, the lip charred
    heat = np.clip(1.0 - d * 1.9, 0, 1) ** 1.2 * (0.7 + 0.3 * grain) * (0.85 + 0.15 * fine)
    wall = np.clip(inside - heat * 1.4, 0, 1)                # the groove's walls: dark, lit from below by the ember
    stone = lip * (0.55 + 0.45 * grain)                     # the chipped lip: pale weathered stone
    r = 255 * np.clip(1.2 * heat, 0, 1) + 70 * wall * (0.5 + 0.5 * (1 - d)) + 120 * stone
    g = 255 * np.clip(0.56 * heat ** 1.25, 0, 1) + 30 * wall * (0.5 + 0.5 * (1 - d)) + 104 * stone
    b = 255 * np.clip(0.2 * heat ** 2.2, 0, 1) + 16 * wall + 90 * stone
    a = np.clip(inside + 0.75 * lip, 0, 1) * 255
    return np.dstack([np.clip(r, 0, 255), np.clip(g, 0, 255), np.clip(b, 0, 255), a]).astype(np.uint8)


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    atlas = np.zeros((H * N, W, 4), np.uint8)
    for i in range(N):
        atlas[i * H:(i + 1) * H] = stroke(31 + i * 17)
    Image.fromarray(atlas, 'RGBA').save(OUT)
    print(OUT, atlas.shape)


if __name__ == '__main__':
    main()
