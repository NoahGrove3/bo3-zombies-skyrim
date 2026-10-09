"""Dark fantasy UI pieces for the HUD (drawn once, committed to assets/ui):
  frame.png    256x256 nine-slice panel (corners 80px): smoky black field, iron and bronze double border,
               Nordic corner brackets with a diamond stud
  divider.png  1024x48 ornament: a bronze line fading out both ways, a winged diamond in the middle
    python tools/uiart.py <out dir>"""
import math, sys, pathlib
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
OUT = pathlib.Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)
SS = 4
BRONZE = (168, 128, 72); IRON = (74, 68, 62); DARK = (12, 10, 10)

def frame():
    S = 256 * SS
    rng = np.random.default_rng(5)
    n = np.asarray(Image.fromarray((rng.random((32, 32)) * 255).astype(np.uint8)).resize((S, S), Image.BICUBIC), np.float32) / 255
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
    edge = np.minimum(np.minimum(xx, 1 - xx), np.minimum(yy, 1 - yy))
    shade = 0.75 + 0.35 * n
    rgb = np.stack([np.full((S, S), c, np.float32) for c in DARK], -1) * shade[..., None]
    a = np.clip(0.80 + 0.15 * (1 - np.clip(edge * 6, 0, 1)), 0, 1)
    im = Image.fromarray(np.dstack([np.clip(rgb, 0, 255), a * 255]).astype(np.uint8), 'RGBA')
    d = ImageDraw.Draw(im)
    o = 6 * SS
    d.rectangle((o, o, S - o, S - o), outline=IRON + (255,), width=3 * SS)
    o2 = 12 * SS
    d.rectangle((o2, o2, S - o2, S - o2), outline=BRONZE + (200,), width=1 * SS)
    # corner brackets: an L of bronze with a spike and a diamond stud, mirrored to all four corners
    br = Image.new('RGBA', (80 * SS, 80 * SS), (0, 0, 0, 0)); bd = ImageDraw.Draw(br)
    L = 56 * SS; t = 5 * SS
    bd.polygon([(2 * SS, 2 * SS), (L, 2 * SS), (L - 10 * SS, 2 * SS + t), (2 * SS + t, 2 * SS + t), (2 * SS + t, L - 10 * SS), (2 * SS, L)], fill=BRONZE + (255,))
    bd.polygon([(L, 2 * SS), (L + 14 * SS, 4.5 * SS), (L, 7 * SS)], fill=BRONZE + (255,))
    bd.polygon([(2 * SS, L), (4.5 * SS, L + 14 * SS), (7 * SS, L)], fill=BRONZE + (255,))
    c = 20 * SS; r = 9 * SS
    bd.polygon([(c, c - r), (c + r, c), (c, c + r), (c - r, c)], fill=(40, 30, 22, 255), outline=BRONZE + (255,))
    bd.polygon([(c, c - r * .45), (c + r * .45, c), (c, c + r * .45), (c - r * .45, c)], fill=(200, 60, 40, 255))
    sh = br.filter(ImageFilter.GaussianBlur(3 * SS)); sh = Image.merge('RGBA', (*[Image.new('L', sh.size, 0)] * 3, sh.getchannel('A').point(lambda v: v * 0.8)))
    for fx, fy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        piece = br
        shp = sh
        if fx: piece, shp = piece.transpose(Image.FLIP_LEFT_RIGHT), shp.transpose(Image.FLIP_LEFT_RIGHT)
        if fy: piece, shp = piece.transpose(Image.FLIP_TOP_BOTTOM), shp.transpose(Image.FLIP_TOP_BOTTOM)
        x = S - piece.size[0] if fx else 0; y = S - piece.size[1] if fy else 0
        im.alpha_composite(shp, (x + 2 * SS, y + 2 * SS)); im.alpha_composite(piece, (x, y))
    im.resize((256, 256), Image.LANCZOS).save(OUT / 'frame.png')

def divider():
    W, H = 1024 * SS, 48 * SS
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    cy = H // 2
    for x in range(0, W, SS):
        f = 1 - abs(x - W / 2) / (W / 2); f = f ** 0.6
        d.line([(x, cy - SS), (x + SS, cy - SS)], fill=BRONZE + (int(255 * f),), width=2 * SS)
    cx = W // 2; r = 14 * SS
    for s in (-1, 1):  # wings
        d.polygon([(cx + s * r * 0.8, cy), (cx + s * 70 * SS, cy - 9 * SS), (cx + s * 52 * SS, cy), (cx + s * 70 * SS, cy + 9 * SS)], fill=BRONZE + (255,))
    d.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], fill=(36, 26, 20, 255), outline=BRONZE + (255,), width=2 * SS)
    d.polygon([(cx, cy - r * .5), (cx + r * .5, cy), (cx, cy + r * .5), (cx - r * .5, cy)], fill=(210, 64, 40, 255))
    im.resize((1024, 48), Image.LANCZOS).save(OUT / 'divider.png')

frame(); divider(); print('ui art written')
