"""The shrine's floating rocks, shared by the game's model (tools/shrine.py) and the main menu's painting
(tools/menushrine.py): one 2K slate texture set, mapped round each rock like a sleeve - u runs along the rock's
length, v round it (so the texture is seamless top to bottom) - with a bracelet of the Curse's runes round each end
of the stone: two carved bands whose runes glow blue (the game pulses them).

    rock_maps(n=2048) -> diffuse (RGB), normal (RGBA), glow (RGB) PIL images
    rock_uv(local x along, local y across, local z through, length) -> (u, v)  for a vertex of a rock
"""
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

BANDS = (0.16, 0.84)       # the bracelets' centres along the rock (u)
BAND_W = 0.095             # their width (u)
GLYPHS = 11                # runes round each bracelet
STROKES = [[(0, -1, 0, 1), (0, -0.3, 0.6, -0.9)], [(-0.5, 1, 0, -1), (0.5, 1, 0, -1), (-0.3, 0.3, 0.3, 0.3)], [(0, -1, 0, 1), (-0.6, 0, 0.6, 0)],
           [(-0.6, -1, 0.6, 1), (0.6, -1, -0.6, 1)], [(0, -1, 0, 1), (0, 0.1, 0.6, 0.9)], [(-0.6, -0.8, 0.6, -0.8), (0, -0.8, 0, 1)],
           [(-0.5, -1, -0.5, 1), (-0.5, 0, 0.5, -0.6), (-0.5, 0, 0.5, 0.6)], [(0, -1, 0.6, 0), (0.6, 0, 0, 1), (0, 1, -0.6, 0), (-0.6, 0, 0, -1)],
           [(-0.5, -1, 0.5, -1), (0, -1, 0, 1), (-0.5, 1, 0.5, 1)], [(0, -1, 0, 1), (0, -1, 0.55, -0.45), (0, -0.2, 0.55, 0.35)]]


def tileable(n, beta, seed):
    """seamless noise both ways: a random spectrum falling off as 1/f^beta, scaled 0..1"""
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(n)
    fx, fy = np.meshgrid(f, f)
    rad = np.sqrt(fx ** 2 + fy ** 2)
    rad[0, 0] = 1
    spec = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) / rad ** beta
    spec[0, 0] = 0
    o = np.real(np.fft.ifft2(spec)).astype(np.float32)
    return (o - o.min()) / (o.max() - o.min())


def rock_uv(x, y, z, L):
    """u along the rock (0 at one end, 1 at the other), v round it (0..1, the seam where it starts again)"""
    return 0.5 + x / L, (math.atan2(z / 0.09, y / 0.2) / (2 * math.pi)) % 1.0


def fix_seam(uvs):
    """a triangle across the seam: lift its low v's by one turn (the texture repeats round)"""
    vs = [v for _, v in uvs]
    if max(vs) - min(vs) > 0.5:
        return [(u, v + 1.0 if v < 0.5 else v) for u, v in uvs]
    return uvs


def rock_maps(n=2048, seed=5):
    rng = np.random.default_rng(seed)
    # the stone: dark slate in layers along its length, worn and pitted
    broad = tileable(n, 2.2, seed)
    grain = tileable(n, 1.4, seed + 1)
    grit = tileable(n, 0.9, seed + 2)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    strata = 0.5 + 0.5 * np.sin((yy * 23 + broad * 3.0) * 2 * math.pi)       # slate's layers run along the rock
    h = broad * 0.45 + grain * 0.30 + grit * 0.15 + strata * 0.10
    # cracks: thin dark lines (drawn three times over so they wrap round the seam)
    cr = Image.new('L', (n, n * 3), 0)
    cd = ImageDraw.Draw(cr)
    for _ in range(22):
        x, y = rng.uniform(0, n), rng.uniform(0, n) + n
        a = rng.uniform(0, 6.28)
        pts = [(x, y)]
        for _ in range(14):
            a += rng.uniform(-0.55, 0.55)
            x += math.cos(a) * n * 0.025
            y += math.sin(a) * n * 0.025
            pts.append((x, y))
        cd.line(pts, fill=255, width=max(2, n // 700))
    crk = np.asarray(cr, np.float32).reshape(3, n, n).max(0) / 255
    crk = np.asarray(Image.fromarray((crk * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(n / 1400)), np.float32) / 255
    # the bracelets: two carved border lines and a row of runes between them, round each end
    gl = Image.new('L', (n, n), 0)
    bd = Image.new('L', (n, n), 0)
    g = ImageDraw.Draw(gl)
    b = ImageDraw.Draw(bd)
    k = 0
    for bc in BANDS:
        u0, u1 = (bc - BAND_W / 2) * n, (bc + BAND_W / 2) * n
        for uu in (u0, u1):
            b.line([(uu, 0), (uu, n)], fill=255, width=max(3, n // 400))
        b.rectangle([u0, 0, u1, n], outline=None, fill=None)
        gh = (u1 - u0) * 0.36   # a rune's half-height (along the rock)
        gw = gh * 0.62          # and half-width (round it)
        for j in range(GLYPHS):
            cv = (j + 0.5) / GLYPHS * n
            for x0, y0, x1, y1 in STROKES[(k * 3 + j * 7) % len(STROKES)]:
                # the rune stands across the band: its own up runs along the rock
                for off in (-n, 0, n):
                    g.line([(bc * n + y0 * gh, cv + x0 * gw + off), (bc * n + y1 * gh, cv + x1 * gw + off)], fill=255, width=max(4, int(gh * 0.24)))
        k += 1
    glyph = np.asarray(gl.filter(ImageFilter.GaussianBlur(n / 1200)), np.float32) / 255
    border = np.asarray(bd.filter(ImageFilter.GaussianBlur(n / 1600)), np.float32) / 255
    inband = np.zeros((n, n), np.float32)
    for bc in BANDS:
        inband = np.maximum(inband, np.clip(1 - np.abs(xx - bc) / (BAND_W / 2), 0, 1) ** 0.4)
    # height: the bands are a smoothed, slightly sunk collar; runes and borders are cut into it
    height = h * (1 - 0.6 * inband) + 0.35 * inband - crk * 0.5 - glyph * 0.55 - border * 0.4
    base = np.array([66, 64, 80], np.float32)
    rgb = base[None, None] * (0.45 + 1.05 * h[..., None])
    rgb *= (1 - 0.55 * crk[..., None])
    rgb = rgb * (1 - 0.25 * inband[..., None]) + np.array([46, 46, 60]) * 0.25 * inband[..., None]   # the collar: smoother, a little darker
    rgb *= (1 - 0.6 * glyph[..., None]) * (1 - 0.45 * border[..., None])
    rgb += np.array([25, 55, 95]) * glyph[..., None] * 0.6                                          # the grooves hold blue light even unlit
    gy, gx = np.gradient(height * (n / 256) * 2.2)
    nrm = np.dstack([-gx, -gy, np.ones_like(gx)])
    nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    nimg = Image.fromarray(np.dstack([(nrm[..., 0] * 0.5 + 0.5) * 255, (nrm[..., 1] * 0.5 + 0.5) * 255, (nrm[..., 2] * 0.5 + 0.5) * 255,
                                      np.clip(40 + 120 * inband + 60 * h, 0, 255)]).astype(np.uint8), 'RGBA')   # (alpha: shine - the collar is polished)
    halo = np.asarray(Image.fromarray((glyph * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(n / 300)), np.float32) / 255
    glow = np.clip(glyph * 1.0 + border * 0.45 + halo * 0.5, 0, 1)
    gimg = Image.fromarray((np.dstack([0.32 * glow, 0.66 * glow, 1.0 * glow]) * 255).astype(np.uint8), 'RGB')
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB'), nimg, gimg
