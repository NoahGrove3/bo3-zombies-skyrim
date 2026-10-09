"""Sheogorath's Coffer, dark-fantasy textures from the source model's masks (its own albedo is a flat white
placeholder): blackened iron where the metallic map says metal, old dark-stained planks elsewhere, faint rust
and grime from the AO, and the question-mark sigils glowing violet.
    python tools/coffer_tex.py <texture dir> <out albedo.png> <out glow.png>"""
import sys, pathlib
import numpy as np
from PIL import Image, ImageFilter
D = pathlib.Path(sys.argv[1]); S = 1024
L = lambda n, m='L': np.asarray(Image.open(D / f'initialShadingGroup_{n}.jpeg').convert(m).resize((S, S), Image.LANCZOS), np.float32)
alb, met, ao = L('albedo', 'RGB'), L('metallic') / 255, L('AO') / 255
rng = np.random.default_rng(7)
def noise(sx, sy, octs=5):
    out = np.zeros((S, S), np.float32)
    for o in range(octs):
        n = rng.random((max(2, int(sy * 2 ** o)), max(2, int(sx * 2 ** o))))
        out += np.asarray(Image.fromarray((n * 255).astype(np.uint8)).resize((S, S), Image.BICUBIC), np.float32) / 255 / 2 ** o
    out -= out.min(); return out / out.max()
def close(mask, r):
    im = Image.fromarray((mask * 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.MaxFilter(r)).filter(ImageFilter.MinFilter(r))) > 128
metal = met > 0.5
blue = (alb[..., 2] - alb[..., 0]) > 25
glyph = (close(metal, 31) & ~metal) | (close(blue, 31) & ~blue)
glyph = np.asarray(Image.fromarray((glyph * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))) > 128
grain, fine, grime = noise(3, 70), noise(40, 40, 4), noise(6, 6, 5)
wood = np.array([46, 28, 20], np.float32) * (0.55 + 0.7 * grain[..., None]) * (0.85 + 0.3 * fine[..., None])
iron = np.array([34, 32, 33], np.float32) * (0.75 + 0.5 * fine[..., None])
rust = np.clip((grime - 0.6) / 0.25, 0, 1)[..., None] * 0.35
iron = iron * (1 - rust) + np.array([74, 38, 22], np.float32) * rust
# worn edges of the iron catch the light: where AO changes fast
edge = np.asarray(Image.fromarray((ao * 255).astype(np.uint8)).filter(ImageFilter.FIND_EDGES), np.float32)[..., None] / 255
iron = iron + np.array([90, 84, 76], np.float32) * np.clip(edge * 3, 0, 0.6)
m = metal[..., None].astype(np.float32)
col = (wood * (1 - m) + iron * m) * (0.3 + 0.7 * ao[..., None]) * (0.8 + 0.3 * grime[..., None])
g = glyph[..., None].astype(np.float32)
col = col * (1 - g) + np.array([150, 70, 230], np.float32) * g
Image.fromarray(np.clip(col, 0, 255).astype(np.uint8)).save(sys.argv[2])
glow = np.asarray(Image.fromarray((glyph * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)), np.float32) / 255
gl = np.clip(np.stack([glow * 180, glow * 90, glow * 255], -1), 0, 255)
Image.fromarray(gl.astype(np.uint8)).save(sys.argv[3])
print('coffer textures: glyph pixels', int(glyph.sum()))
