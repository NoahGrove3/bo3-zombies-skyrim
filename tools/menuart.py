"""The Curse of Alduin main menu art (Spec 13): layered, original, procedural. Writes RGBA layers the HUD animates with
parallax, drifting fog, flickering fire, pulsing runes, embers and restrained lightning.

    python tools/menuart.py <out dir> [width]

Layers (all the same size, 6% larger than the screen so parallax never shows an edge):
  menu_sky.png    night sky: cold blue-black, stars, the blood moon, clouds lit at the rims      (opaque)
  menu_far.png    distant mountains in moonlit haze, a ruined watchtower                        (alpha)
  menu_mid.png    the Echo Altar in ruins: dais, rune-stone and cradle, broken arches  (alpha)
  menu_near.png   foreground: a dead tree, barrow stones, an iron fence, the risen walking in    (alpha)
  menu_title.png  the title treatment (cut iron and bone, a cold rim, a blood under-glow)       (alpha)
Feature positions the HUD animates (normalised to the layer): see FEATURES below (kept in step with Hud.cpp).
Palette: deep blacks, charcoal, cold blue, desaturated green runes, restrained blood red (the moon)."""
import math, sys, pathlib
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'menuart')
OUT.mkdir(parents=True, exist_ok=True)
W = int(sys.argv[2]) if len(sys.argv) > 2 else 2720
H = int(W * 9 / 16)
k = W / 3840.0
rng = np.random.default_rng(23)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
FONTS = pathlib.Path(__file__).resolve().parent.parent / 'assets/fonts'

# feature positions in 3840x2160 design space (also used by Hud.cpp, normalised)
MOON = (1460, 610, 250)
ORB = (1470, 1020)            # the rune-stone's crown (green soul-fire)
RING = (1470, 1500, 430, 95)  # the rune ring on the ground round the dais (cx, cy, rx, ry)
BRAZIERS = [(1010, 1330), (1930, 1330)]
FEATURES = dict(moon=MOON, orb=ORB, ring=RING, braziers=BRAZIERS)


def P(x, y): return (x * k, y * k)
def C(h): h = h.lstrip('#'); return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], np.float32)


def fbm(oct=6, base=4, seed=0, pers=0.55, w=None, h=None):
    w, h = w or W, h or H
    r = np.random.default_rng(seed); acc = np.zeros((h, w), np.float32); amp = 1; tot = 0
    for o in range(oct):
        n = base * 2 ** o
        g = r.random((max(2, int(n * h / w)) + 1, n + 1)).astype(np.float32)
        acc += np.asarray(Image.fromarray(g).resize((w, h), Image.BICUBIC)) * amp; tot += amp; amp *= pers
    return acc / tot


def mask(fn, blur=0):
    m = Image.new('L', (W, H), 0); fn(ImageDraw.Draw(m))
    if blur: m = m.filter(ImageFilter.GaussianBlur(blur * k))
    return np.asarray(m, np.float32) / 255


def poly(pts): return lambda d: d.polygon([P(*p) for p in pts], fill=255)
def ell(x, y, rx, ry): return lambda d: d.ellipse([P(x - rx, y - ry), P(x + rx, y + ry)], fill=255)
def rect(x0, y0, x1, y1): return poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
def blur(m, r): return np.asarray(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r * k)), np.float32) / 255


class Layer:
    def __init__(self, opaque=False):
        self.rgb = np.zeros((H, W, 3), np.float32); self.a = np.ones((H, W), np.float32) if opaque else np.zeros((H, W), np.float32)
    def over(self, m, col, a=1.0):
        m = np.clip(m * a, 0, 1); col = C(col) if isinstance(col, str) else col
        self.rgb = self.rgb * (1 - m[..., None]) + col * m[..., None]; self.a = self.a + m * (1 - self.a)
    def add(self, m, col, a=1.0):  # light (only where the layer has something, or as a glow with its own alpha)
        col = C(col) if isinstance(col, str) else col
        self.rgb = self.rgb + m[..., None] * col * a
    def glow(self, m, col, r, a=1.0):
        g = blur(m, r) * a; col = C(col) if isinstance(col, str) else col
        self.rgb = self.rgb * (1 - np.clip(g, 0, 1)[..., None] * 0) + g[..., None] * col
        self.a = np.clip(self.a + g * 0.6, 0, 1)
    def save(self, name):
        rgb = np.clip(self.rgb / (1 + self.rgb / 400), 0, 255)  # soft highlight roll-off
        # premultiplied-safe: colour where alpha is 0 is irrelevant; keep edges clean
        out = np.dstack([rgb, np.clip(self.a, 0, 1) * 255]).astype(np.uint8)
        Image.fromarray(out, 'RGBA').save(OUT / name, optimize=True)
        print('wrote', name)


# ================================================================ sky
sky = Layer(opaque=True)
t = yy / H
sky.rgb[:] = C('#04060b') * (1 - t[..., None]) ** 1.4 + C('#0d1624') * np.clip(1 - np.abs(t - 0.5) * 2.2, 0, 1)[..., None] + C('#16141a') * (t[..., None] ** 2)
mx, my, mr = MOON
d2 = np.sqrt((xx / k - mx) ** 2 + (yy / k - my) ** 2)
sky.add(np.exp(-(d2 / 760) ** 2), '#3a0f0c', 1.0)
sky.add(np.exp(-(d2 / 1900) ** 2), '#141826', 1.0)
st = rng.random((H, W)) > 0.9991
sky.add(blur(st.astype(np.float32), 0.5) * (1 - t) ** 1.6 * (d2 > 700), '#cfd8ff', 2.4)
moon = (d2 < mr).astype(np.float32)
mar = fbm(6, 6, seed=3)
limb = np.sqrt(np.clip(1 - (d2 / mr) ** 2, 0, 1))
mcol = C('#b23220') * (0.5 + 0.5 * limb[..., None]) * (0.75 + 0.55 * (mar[..., None] - 0.5) * 2.0).clip(0.4, 1.2)
sky.rgb = sky.rgb * (1 - moon[..., None]) + mcol * moon[..., None]
sky.add(np.clip(1 - np.abs(d2 - mr) / 18, 0, 1), '#ff7048', 0.3)
cl = fbm(7, 3, seed=5)
band = np.exp(-((yy / k - 820) / 260) ** 2) + 0.8 * np.exp(-((yy / k - 330) / 170) ** 2)
cm = np.clip((cl - 0.47) * 4.0, 0, 1) * band.clip(0, 1)
cloudcol = C('#0a0d15') * 0.95 + C('#3c1410') * np.exp(-(d2 / 640) ** 2)[..., None] * 1.2
sky.over(cm, cloudcol, 0.85)
sky.add(cm * np.exp(-(d2 / 560) ** 2), '#ff5030', 0.22)
sky.add(cm * (1 - np.exp(-(d2 / 900) ** 2)), '#3a4a66', 0.18)  # cold moonlit tops away from the moon
sky.save('menu_sky.png')

# ================================================================ far: mountains in haze, a ruined watchtower
far = Layer()
def ridge(L, y0, amp, seed, col, haze, ha):
    r = np.random.default_rng(seed); xs = np.linspace(-200, 4040, 500); h = np.zeros_like(xs)
    for o in range(6):
        f = 2 ** o * 1.4
        h += np.sin(xs / 3840 * f * 2 * np.pi + r.random() * 6.3) * amp / (1.7 ** o)
    pts = [(-200, 2160)] + list(zip(xs, y0 - np.abs(h))) + [(4040, 2160)]
    m = mask(poly(pts))
    tex = fbm(5, 18, seed=seed + 3)
    L.over(m, C(col) * (0.85 + 0.3 * tex[..., None]))
    hz = np.exp(-((yy / k - (y0 - 30)) / 120) ** 2) * m
    L.add(hz, haze, ha)
    return m
ridge(far, 1180, 300, 1, '#121722', '#2d3a52', 0.55)
m2 = ridge(far, 1330, 190, 2, '#0c1019', '#28324a', 0.5)
# a ruined watchtower on the second ridge (far right), one window still lit
tx = 3020
far.over(mask(poly([(tx - 46, 1240), (tx - 38, 1010), (tx - 20, 990), (tx + 6, 1012), (tx + 30, 985), (tx + 40, 1020), (tx + 48, 1240)])), '#0a0d14')
far.add(mask(rect(tx - 6, 1060, tx + 6, 1082)), '#ffb070', 1.6)
far.glow(mask(rect(tx - 6, 1060, tx + 6, 1082)), '#ff8a40', 14, 0.6)
fog = np.exp(-((yy / k - 1290) / 140) ** 2) * (0.55 + 0.6 * fbm(5, 4, seed=41))
far.rgb = far.rgb + fog[..., None] * C('#1e2838') * 0.7
far.a = np.clip(far.a + fog * 0.35, 0, 1)
far.save('menu_far.png')

# ================================================================ mid: the Echo Altar, in ruins
mid = Layer()
ox, oy = ORB
cx, cy, rx, ry = RING
stone = C('#22252c'); dark = C('#0b0b0d')
# ground plane in front of the shrine (dark earth, catches the orb's green)
ground = mask(poly([(-200, 1420), (600, 1380), (1470, 1360), (2400, 1390), (4040, 1440), (4040, 2160), (-200, 2160)]))
gtex = fbm(6, 20, seed=71)
mid.over(ground, C('#0a0b0e') * (0.8 + 0.5 * gtex[..., None]))
# the dais: three round tiers seen from the front (a lit top, a shadowed side)
for i, (w_, h_, y_) in enumerate([(600, 54, 1452), (500, 48, 1412), (400, 44, 1374)]):
    side = np.clip(mask(ell(cx, y_ + h_, w_, w_ * 0.16)) + mask(rect(cx - w_, y_, cx + w_, y_ + h_)), 0, 1)
    mid.over(side, stone * (0.55 - 0.05 * i))
    topm = mask(ell(cx, y_, w_, w_ * 0.16))
    mid.over(topm, stone * (0.95 - 0.08 * i))
    mid.add(np.clip(topm - mask(ell(cx, y_ + 6, w_ - 14, w_ * 0.16 - 4)), 0, 1), '#3a4a62', 0.5)  # moonlit lip
# the rune-stone pillar and the cradle of four prongs
pil = mask(poly([(ox - 70, 1375), (ox - 56, 1110), (ox - 40, 1085), (ox + 40, 1085), (ox + 56, 1110), (ox + 70, 1375)]))
ptex = fbm(5, 26, seed=81)
mid.over(pil, stone * (0.85 + 0.5 * ptex[..., None]))
mid.add(pil * np.clip((xx / k - (ox - 70)) / 140, 0, 1), '#2a3a52', 0.25)  # moonlight from the left
for gy in (1150, 1210, 1270, 1330):  # carved runes, faintly lit green
    for gx in (-26, 0, 26):
        mid.add(mask(poly([(ox + gx, gy - 14), (ox + gx + 8, gy), (ox + gx, gy + 14), (ox + gx - 8, gy)])), '#6fdba8', 0.7)
for sgn in (-1, 1):
    for j, dx in enumerate((34, 14)):
        x0 = ox + sgn * dx
        mid.over(mask(poly([(x0 - 6, 1090), (x0 + 6, 1090), (x0 + sgn * (26 + 10 * j) + 4, 1000 - 18 * j), (x0 + sgn * (24 + 10 * j) - 4, 996 - 18 * j)])), '#16161a')
# broken Nordic arches either side, carved with knotwork
def pillar(x, y_top, y_bot, w_, broken):
    pts = [(x - w_, y_bot), (x - w_ * 0.9, y_top + (40 if broken else 0)), (x - w_ * 0.2, y_top), (x + w_ * 0.5, y_top + (70 if broken else 0)), (x + w_ * 0.9, y_top + 10), (x + w_, y_bot)]
    m = mask(poly(pts))
    tex = fbm(5, 30, seed=int(x))
    mid.over(m, stone * (0.6 + 0.5 * tex[..., None]))
    mid.add(m * np.clip(((x + w_) - xx / k) / (2 * w_), 0, 1) * 0, '#000000', 0)
    for yk in range(int(y_top + 80), int(y_bot - 40), 90):  # bands of knotwork
        mid.over(mask(rect(x - w_ * 0.95, yk, x + w_ * 0.95, yk + 10)), '#0d0d10', 0.9)
    return m
pillar(820, 900, 1420, 70, False); pillar(1060, 1010, 1400, 58, True)
pillar(1880, 980, 1400, 58, True); pillar(2120, 860, 1430, 72, False)
# the lintel stone that still spans the left pair, the right one fallen
mid.over(mask(poly([(720, 900), (1150, 940), (1140, 990), (730, 950)])), stone * 0.75)
mid.over(mask(poly([(2020, 1395), (2240, 1370), (2260, 1420), (2030, 1440)])), stone * 0.6)   # fallen
# braziers (fire drawn in code: flicker)
for bx, by in BRAZIERS:
    mid.over(mask(poly([(bx - 40, by), (bx + 40, by), (bx + 26, by + 26), (bx - 26, by + 26)])), '#1a1512')
    mid.over(mask(rect(bx - 8, by + 26, bx + 8, by + 120)), '#141110')
    mid.over(mask(poly([(bx - 30, by + 120), (bx + 30, by + 120), (bx + 20, by + 135), (bx - 20, by + 135)])), '#141110')
    f = mask(poly([(bx - 30, by - 4), (bx - 24, by - 40), (bx - 8, by - 70), (bx, by - 96), (bx + 10, by - 66), (bx + 26, by - 40), (bx + 30, by - 4)]))
    mid.over(f, '#ff8a30', 0.95); mid.glow(f, '#ff6a10', 40, 0.45)
# the rune ring on the ground round the dais (pulsed in code; drawn faintly here)
ringm = np.clip(mask(ell(cx, cy, rx, ry)) - mask(ell(cx, cy, rx - 12, ry - 4)), 0, 1)
mid.add(ringm, '#4fbf8a', 0.9); mid.glow(ringm, '#2a8f60', 10, 0.35)
for a in np.linspace(0, 2 * math.pi, 16, endpoint=False):
    gx, gy = cx + math.cos(a) * (rx + 30), cy + math.sin(a) * (ry + 8)
    mid.add(mask(poly([(gx, gy - 9), (gx + 6, gy), (gx, gy + 9), (gx - 6, gy)])), '#7fe8b8', 0.8)
# the orb's light on the stones
light = np.exp(-(((xx / k - ox) / 420) ** 2 + ((yy / k - oy - 200) / 380) ** 2))
mid.rgb = mid.rgb + (mid.a * light)[..., None] * C('#1f5a40') * 0.28
moonlit = np.clip((1460 - np.abs(xx / k - 1460)) / 1460, 0, 1) * 0.0 + np.exp(-((yy / k - 900) / 500) ** 2)
mid.rgb = mid.rgb + (mid.a * moonlit)[..., None] * C('#1a2436') * 0.35
mid.save('menu_mid.png')

# ================================================================ near: the barrow-yard and the risen
near = Layer()
base = mask(poly([(-200, 1720), (500, 1680), (1300, 1760), (2200, 1740), (3000, 1700), (4040, 1760), (4040, 2160), (-200, 2160)]))
near.over(base, C('#07080a') * (0.8 + 0.4 * fbm(5, 16, seed=91)[..., None]))
# a dead tree on the left edge, branches clawing at the sky
def branch(d, x, y, ang, ln, wdt, depth):
    if depth == 0 or ln < 18:
        return
    x2, y2 = x + math.cos(ang) * ln, y + math.sin(ang) * ln
    d.line([P(x, y), P(x2, y2)], fill=255, width=max(1, int(wdt * k)))
    for s in (-1, 1):
        if rng.random() < 0.85:
            branch(d, x2, y2, ang + s * (0.35 + rng.random() * 0.5), ln * (0.62 + rng.random() * 0.2), wdt * 0.62, depth - 1)
tree = mask(lambda d: (d.polygon([P(150, 2160), P(210, 1500), P(260, 1380), P(330, 1500), P(380, 2160)], fill=255), branch(d, 260, 1400, -1.75, 300, 46, 7), branch(d, 250, 1520, -2.4, 260, 32, 6), branch(d, 290, 1600, -0.7, 240, 28, 6)))
near.over(tree, '#050506')
# barrow stones and grave markers
for gx, gy, gw, gh, tilt in [(560, 1760, 70, 170, -0.08), (760, 1800, 52, 120, 0.12), (2540, 1770, 60, 150, 0.06), (2790, 1810, 80, 190, -0.1), (3300, 1760, 64, 160, 0.04)]:
    pts = [(gx - gw, gy), (gx - gw * 0.9, gy - gh * 0.8), (gx, gy - gh), (gx + gw * 0.9, gy - gh * 0.8), (gx + gw, gy)]
    pts = [(px + (py - gy) * tilt, py) for px, py in pts]
    near.over(mask(poly(pts)), '#0b0c0f')
    near.add(mask(poly(pts)) * np.clip((gx - xx / k) / gw, 0, 1), '#22304a', 0.35)  # moon rim
# an iron fence along the right, half fallen
for i, fx in enumerate(range(2380, 3900, 46)):
    lean = 0.0 if i % 7 else 0.25
    near.over(mask(poly([(fx - 4, 1760), (fx + 4, 1760), (fx + 3 + 280 * lean, 1560 + 30 * lean), (fx, 1540 + 30 * lean), (fx - 3 + 280 * lean, 1560 + 30 * lean)])), '#060607')
near.over(mask(rect(2370, 1600, 3900, 1608)), '#060607')
# the risen, walking in toward the shrine (eyes added in code; they blink)
def risen(x, y, s, flip):
    pts = [(-30, 0), (-24, -170), (-40, -190), (-34, -250), (-14, -290), (6, -300), (24, -282), (30, -250), (52, -220), (90, -235), (96, -224), (54, -196), (40, -170), (34, 0), (14, 0), (6, -110), (-6, -110), (-14, 0)]
    pts = [(x + (px * (-1 if flip else 1)) * s, y + py * s) for px, py in pts]
    m = mask(poly(pts))
    near.over(m, '#040405')
    near.add(np.clip(m - np.roll(m, int(3 * k), axis=1), 0, 1), '#1c2a40', 0.8)
for x, y, s, flip in [(420, 1830, 1.15, False), (880, 1800, 0.9, False), (2380, 1790, 0.95, True), (3200, 1850, 1.25, True), (3580, 1800, 0.85, True)]:
    risen(x, y, s, flip)
# a soft, darker veil over the right third, where the menu lives (kept uncluttered)
veil = np.clip((xx / k - 2600) / 900, 0, 1) * np.clip((yy / k - 600) / 900, 0, 1)
near.rgb = near.rgb * (1 - veil[..., None] * 0.0)
near.a = np.clip(near.a + veil * 0.55, 0, 1)
near.save('menu_near.png')

# ================================================================ title: cut iron and bone, a cold rim, a blood under-glow
TW, TH = int(3500 * k), int(560 * k)
fT = ImageFont.truetype(str(FONTS / 'Cinzel-Bold.ttf'), int(240 * k))
fS = ImageFont.truetype(str(FONTS / 'CormorantGaramond-SemiBold.ttf'), int(70 * k))
def spaced(d, text, font, cy, track, fill, w=TW):
    widths = [font.getlength(c) for c in text]; tot = sum(widths) + track * (len(text) - 1); x = (w - tot) / 2
    for c, cw in zip(text, widths):
        d.text((x, cy), c, font=font, fill=fill, anchor='lm'); x += cw + track
TITLE = 'THE CURSE OF ALDUIN'
while sum(fT.getlength(c) for c in TITLE) + 30 * k * (len(TITLE) - 1) > TW * 0.94:  # (a long name: the letters shrink to fit)
    fT = ImageFont.truetype(str(FONTS / 'Cinzel-Bold.ttf'), int(fT.size * 0.96))
m = Image.new('L', (TW, TH), 0); spaced(ImageDraw.Draw(m), TITLE, fT, 190 * k, 30 * k, 255)
ma = np.asarray(m, np.float32) / 255
ty_, tx_ = np.mgrid[0:TH, 0:TW].astype(np.float32)
grad = np.clip((ty_ - 70 * k) / (240 * k), 0, 1)[..., None]
metal = C('#e8e2d6') * (1 - grad) ** 1.2 + C('#5c6672') * grad   # bone above, cold iron below
tex = fbm(5, 40, seed=51, w=TW, h=TH)[..., None]
metal = metal * (0.78 + 0.35 * tex)
emb = np.asarray(m.filter(ImageFilter.GaussianBlur(3 * k)), np.float32) / 255
bev = np.clip((emb - np.roll(emb, max(1, int(4 * k)), axis=0)) * 3, -1, 1)[..., None]
metal = metal * (1 + 0.4 * bev)
# cracks through the letters
cr = Image.new('L', (TW, TH), 0); dcr = ImageDraw.Draw(cr)
for _ in range(26):
    x0, y0 = rng.uniform(0, TW), rng.uniform(70 * k, 300 * k); pts = [(x0, y0)]
    for _ in range(6):
        x0 += rng.uniform(-18, 18) * k; y0 += rng.uniform(6, 22) * k; pts.append((x0, y0))
    dcr.line(pts, fill=255, width=max(1, int(2 * k)))
crack = (np.asarray(cr, np.float32) / 255) * ma
metal = metal * (1 - crack[..., None] * 0.75)
under = np.asarray(m.filter(ImageFilter.GaussianBlur(34 * k)), np.float32) / 255
rim = np.clip(np.asarray(m.filter(ImageFilter.MaxFilter(5)), np.float32) / 255 - ma, 0, 1)
rgb = np.zeros((TH, TW, 3), np.float32)
alpha = np.clip(under * 0.75 + rim + ma, 0, 1)
rgb += under[..., None] * C('#7a1810') * 1.3                      # the blood under-glow
rgb = rgb * (1 - rim[..., None]) + C('#0a0c10') * rim[..., None]  # dark cut edge
rgb = rgb * (1 - ma[..., None]) + metal * ma[..., None]
lay = Image.new('L', (TW, TH), 0); spaced(ImageDraw.Draw(lay), 'EVERY NIGHT AFTER HELGEN, THE DEAD RISE', fS, 400 * k, 13 * k, 255)
la = np.asarray(lay, np.float32) / 255
rgb = rgb * (1 - la[..., None]) + C('#b9c4d0') * la[..., None]
alpha = np.clip(alpha + la, 0, 1)
dl = Image.new('L', (TW, TH), 0); dd = ImageDraw.Draw(dl)
for sgn in (-1, 1):
    x0, x1 = TW / 2 + sgn * 1090 * k, TW / 2 + sgn * 1450 * k; yl = 400 * k
    dd.line([(x0, yl), (x1, yl)], fill=200, width=max(1, int(3 * k)))
    dd.polygon([(x0, yl - 11 * k), (x0 + sgn * 16 * k, yl), (x0, yl + 11 * k), (x0 - sgn * 16 * k, yl)], fill=255)
dla = np.asarray(dl, np.float32) / 255
rgb = rgb * (1 - dla[..., None]) + C('#7a8c9c') * dla[..., None]
alpha = np.clip(alpha + dla, 0, 1)
# (Noah: the tagline taken off, the title sharper and with a sweeping shine) the title is tools/menutitle.py's now
import subprocess; subprocess.run([sys.executable, str(pathlib.Path(__file__).parent / 'menutitle.py'), str(OUT)], check=True)
print('wrote menu_title.png', (TW, TH))

# a flat preview of the whole menu (for review)
comp = Image.open(OUT / 'menu_sky.png').convert('RGBA')
for n in ('menu_far.png', 'menu_mid.png', 'menu_near.png'):
    comp = Image.alpha_composite(comp, Image.open(OUT / n))
tl = Image.open(OUT / 'menu_title.png')
comp.alpha_composite(tl, ((W - tl.width) // 2, int(120 * k)))
comp.convert('RGB').resize((1600, 900)).save(OUT / 'preview.jpg', quality=88)
print('features', FEATURES)
