#!/usr/bin/env python3
"""The main menu's title, THE CURSE OF ALDUIN, in cut bone and cold iron (no tagline under it) - rendered at twice its size
and brought down, so the edges are clean - and its shine: a white layer of the letters' lit faces, which the HUD sweeps
across the title now and then (src/Hud.cpp, the main menu).

    python3 tools/menutitle.py <out dir>      ->  menu_title.png, menu_title_shine.png  (3200 x 430)
"""
import sys, pathlib
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.')
FONTS = pathlib.Path(__file__).resolve().parent.parent / 'assets/fonts'
TW, TH, SS = 3200, 430, 2
W, H = TW * SS, TH * SS
TITLE = 'THE CURSE OF ALDUIN'
rng = np.random.default_rng(51)


def C(h): h = h.lstrip('#'); return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], np.float32)
def arr(im): return np.asarray(im, np.float32) / 255
def blur(m, r): return arr(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)))


def fbm(oct, base, seed):
    r = np.random.default_rng(seed); acc = np.zeros((H, W), np.float32); amp = 1; tot = 0
    for o in range(oct):
        n = base * 2 ** o
        g = r.random((max(2, int(n * H / W)) + 1, n + 1)).astype(np.float32)
        acc += np.asarray(Image.fromarray(g).resize((W, H), Image.BICUBIC)) * amp; tot += amp; amp *= 0.55
    return acc / tot


track = 34 * SS
font = ImageFont.truetype(str(FONTS / 'Cinzel-Bold.ttf'), 250 * SS)
while sum(font.getlength(c) for c in TITLE) + track * (len(TITLE) - 1) > W * 0.95:
    font = ImageFont.truetype(str(FONTS / 'Cinzel-Bold.ttf'), int(font.size * 0.97))
m = Image.new('L', (W, H), 0)
d = ImageDraw.Draw(m)
widths = [font.getlength(c) for c in TITLE]
x = (W - (sum(widths) + track * (len(TITLE) - 1))) / 2
for c, cw in zip(TITLE, widths):
    d.text((x, H * 0.47), c, font=font, fill=255, anchor='lm'); x += cw + track
ma = arr(m)

# the letters' relief: a soft height field from the mask, lit from the upper left
hgt = blur(ma, 7 * SS) * 0.6 + blur(ma, 2.5 * SS) * 0.4
gy, gx = np.gradient(hgt)
nx, ny, nz = -gx * 60, -gy * 60, np.ones_like(hgt)
nl = np.sqrt(nx ** 2 + ny ** 2 + nz ** 2)
L = np.array([-0.45, -0.6, 0.66]); L /= np.linalg.norm(L)
lit = (nx * L[0] + ny * L[1] + nz * L[2]) / nl
spec = np.clip(lit, 0, 1) ** 24                                   # the polished bevel's glint
yy = np.mgrid[0:H, 0:W][0].astype(np.float32) / H
grad = np.clip((yy - 0.22) / 0.5, 0, 1)[..., None]
metal = C('#efe8dc') * (1 - grad) ** 1.3 + C('#56606c') * grad    # bone above, cold iron below
tex = fbm(6, 36, 7)[..., None]
metal = metal * (0.86 + 0.22 * tex)
metal = metal * (0.55 + 0.6 * np.clip(lit, 0, 1))[..., None] + spec[..., None] * 255 * 0.55
# fine hairline cracks
cr = Image.new('L', (W, H), 0); dc = ImageDraw.Draw(cr)
for _ in range(22):
    x0, y0 = rng.uniform(0, W), rng.uniform(H * 0.2, H * 0.6); pts = [(x0, y0)]
    for _ in range(5):
        x0 += rng.uniform(-14, 14) * SS; y0 += rng.uniform(6, 18) * SS; pts.append((x0, y0))
    dc.line(pts, fill=255, width=SS)
crack = arr(cr) * ma
metal = metal * (1 - crack[..., None] * 0.6)

# the cut edge (dark), a cold rim light outside it, a restrained blood glow beneath
edge = np.clip(arr(m.filter(ImageFilter.MaxFilter(2 * 3 + 1))) - ma, 0, 1)
rim = np.clip(arr(m.filter(ImageFilter.MaxFilter(2 * 6 + 1))) - arr(m.filter(ImageFilter.MaxFilter(2 * 3 + 1))), 0, 1) * np.clip(1 - yy * 1.3, 0, 1)
glow = blur(ma, 26 * SS)
rgb = glow[..., None] * C('#8a1c12') * 1.2
a = np.clip(glow * 0.7, 0, 1)
rgb = rgb * (1 - rim[..., None] * 0.5) + C('#9fb4cc') * rim[..., None] * 0.5
a = np.maximum(a, rim * 0.5)
rgb = rgb * (1 - edge[..., None]) + C('#07080b') * edge[..., None]
a = np.maximum(a, edge)
rgb = rgb * (1 - ma[..., None]) + metal * ma[..., None]
a = np.maximum(a, ma)

def down(img):
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGBA').resize((TW, TH), Image.LANCZOS)

down(np.dstack([rgb, a * 255])).save(OUT / 'menu_title.png', optimize=True)
# the shine: the lit faces of the letters, white (swept across by the HUD)
sh = np.clip(np.clip(lit, 0, 1) ** 3 * 1.2 + spec, 0, 1) * ma
down(np.dstack([np.full((H, W, 3), 255, np.float32), sh * 255])).save(OUT / 'menu_title_shine.png', optimize=True)
print('menutitle: wrote', OUT / 'menu_title.png', OUT / 'menu_title_shine.png')
