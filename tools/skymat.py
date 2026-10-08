"""Skyrim-style procedural materials for the weapons: aged wood, worn iron/steel, tarnished bronze/gold, leather wrap,
bone, cloth and glowing crystal. Each returns (albedo RGB, normal RGBA with specular in alpha) at 1024px, made to sit
next to Skyrim's own textures (muted, weathered, readable detail rather than flat colour).

    from skymat import make; d, n = make('wood', tint=(92, 62, 40))"""
import numpy as np
from PIL import Image, ImageFilter

SIZE = 1024

def _noise(size, scale, seed, octaves=5):
    """fractal value noise in [0,1]"""
    r = np.random.default_rng(seed); out = np.zeros((size, size), np.float32); amp = 1.0; tot = 0.0
    for o in range(octaves):
        cells = max(2, int(scale * (2 ** o)))
        g = r.random((cells, cells)).astype(np.float32)
        layer = np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((size, size), Image.BICUBIC), np.float32) / 255
        out += layer * amp; tot += amp; amp *= 0.5
    return out / tot

def _stretch(a, sx, sy):
    h, w = a.shape
    im = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).resize((max(2, int(w / sx)), max(2, int(h / sy))), Image.BICUBIC)
    return np.asarray(im.resize((w, h), Image.BICUBIC), np.float32) / 255

def _normal(height, strength, spec):
    gx = np.gradient(height, axis=1) * strength; gy = np.gradient(height, axis=0) * strength
    n = np.dstack([-gx, gy, np.ones_like(height)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
    rgb = ((n * 0.5 + 0.5) * 255).astype(np.uint8)
    s = np.broadcast_to(np.clip(np.asarray(spec, np.float32) * 255, 0, 255), height.shape).astype(np.uint8)
    return Image.fromarray(np.dstack([rgb, s]), 'RGBA')

def _rgb(base, k):
    return Image.fromarray(np.clip(np.array(base, np.float32)[None, None, :] * k[..., None], 0, 255).astype(np.uint8), 'RGB')

def make(kind, tint=None, seed=1, size=SIZE):
    S = size
    if kind in ('wood', 'wood_dark'):
        base = tint or ((96, 66, 42) if kind == 'wood' else (52, 36, 28))
        grain = _stretch(_noise(S, 24, seed, 4), 1, 14)               # long fibres along V
        rings = 0.5 + 0.5 * np.sin(grain * 40 + _noise(S, 6, seed + 1, 3) * 12)
        knots = _noise(S, 5, seed + 2, 3)
        k = 0.62 + 0.28 * rings * (0.6 + 0.4 * knots) + 0.12 * _noise(S, 64, seed + 3, 2)
        h = rings * 0.6 + grain * 0.4
        return _rgb(base, k), _normal(h, 5.0, 0.12 + 0.08 * rings)
    if kind in ('iron', 'steel', 'bronze', 'gold', 'silver'):
        base = tint or {'iron': (92, 92, 96), 'steel': (150, 152, 158), 'bronze': (150, 104, 56), 'gold': (196, 156, 72), 'silver': (190, 192, 200)}[kind]
        brushed = _stretch(_noise(S, 128, seed, 3), 1, 30)
        wear = _noise(S, 8, seed + 1, 5)
        scratches = (np.random.default_rng(seed + 2).random((S, S)) > 0.9985).astype(np.float32)
        scratches = np.asarray(Image.fromarray((scratches * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6)).resize((S, S)), np.float32) / 255
        scratches = _stretch(scratches, 1, 6) * 3
        grime = np.clip((wear - 0.55) * 3, 0, 1)                       # dark tarnish in the low spots
        k = 0.78 + 0.18 * brushed + 0.25 * np.clip(scratches, 0, 1) - 0.35 * grime
        return _rgb(base, k), _normal(brushed * 0.3 + wear * 0.7, 3.0, 0.55 + 0.35 * brushed - 0.4 * grime)
    if kind == 'leather':
        base = tint or (74, 50, 34)
        pores = _noise(S, 160, seed, 2); folds = _noise(S, 10, seed + 1, 4)
        # diagonal strap wraps
        y, x = np.mgrid[0:S, 0:S].astype(np.float32)
        band = ((x + y) / S * 9) % 1.0
        seam = np.clip(1 - np.abs(band - 0.5) * 2.2, 0, 1) ** 6
        k = 0.7 + 0.15 * folds + 0.08 * pores - 0.35 * seam
        return _rgb(base, k), _normal(folds * 0.6 + pores * 0.2 - seam * 0.8, 4.0, 0.18 + 0.1 * folds)
    if kind == 'bone':
        base = tint or (198, 186, 160)
        p = _noise(S, 90, seed, 3); stains = _noise(S, 6, seed + 1, 4)
        k = 0.75 + 0.15 * p - 0.25 * np.clip(stains - 0.55, 0, 1) * 3
        return _rgb(base, k), _normal(p, 4.0, 0.25 + 0.1 * p)
    if kind == 'cloth':
        base = tint or (60, 40, 70)
        y, x = np.mgrid[0:S, 0:S].astype(np.float32)
        weave = 0.5 + 0.25 * np.sin(x * 0.9) + 0.25 * np.sin(y * 0.9)
        k = 0.72 + 0.15 * weave + 0.15 * _noise(S, 12, seed, 4)
        return _rgb(base, k), _normal(weave * 0.5, 2.0, 0.08)
    if kind == 'crystal':
        base = tint or (150, 220, 255)
        veins = np.abs(_noise(S, 6, seed, 5) - 0.5) * 2
        veins = np.clip(1 - veins * 4, 0, 1)
        k = 0.65 + 0.25 * _noise(S, 20, seed + 1, 3) + 0.4 * veins
        return _rgb(base, k), _normal(veins, 2.0, 0.9)
    if kind == 'vines':
        base = tint or (52, 86, 40)
        n = _stretch(_noise(S, 40, seed, 4), 1, 6)
        k = 0.65 + 0.35 * n
        return _rgb(base, k), _normal(n, 4.0, 0.2)
    raise ValueError(kind)

def glow(col, seed=1, size=256):
    """glow map for crystals/runes: bright core with veins"""
    veins = np.abs(_noise(size, 6, seed, 5) - 0.5) * 2
    k = 0.75 + 0.25 * np.clip(1 - veins * 3, 0, 1)
    return _rgb(col, k)
