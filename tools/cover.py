"""The Curse of Alduin cover art (original, procedural): a blood moon over the Echo Altar, its three alcoves
lit (Atronach Reforge, Sheogorath's Coffer, Brew Machine), the Risen climbing out of a barrow-yard, an Iron Reaper,
and a caster who hurls an arcane lance from bare hands.   python cover.py out.png [width]"""
import math, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else 'cover.png'
W0 = int(sys.argv[2]) if len(sys.argv) > 2 else 1920
import os
MENU = os.environ.get('MENU') == '1'   # main-menu backdrop: the cover with the bottom-right left open for Skyrim's buttons
S = 2                                   # supersample
W, H = W0 * S, int(W0 * 9 / 16) * S
k = W / 3840                            # layout is in 3840x2160 units
rng = np.random.default_rng(11)
img = np.zeros((H, W, 3), np.float32)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

def P(x, y): return (x * k, y * k)
def C(h): h = h.lstrip('#'); return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], np.float32)

def fbm(w, h, oct=6, base=4, seed=0, pers=0.55):
    r = np.random.default_rng(seed); acc = np.zeros((h, w), np.float32); amp = 1; tot = 0
    for o in range(oct):
        n = base * 2 ** o
        g = r.random((max(2, int(n * h / w)) + 1, n + 1)).astype(np.float32)
        acc += np.asarray(Image.fromarray(g).resize((w, h), Image.BICUBIC)) * amp; tot += amp; amp *= pers
    return acc / tot

def mask(draw_fn, blur=0):
    m = Image.new('L', (W, H), 0); d = ImageDraw.Draw(m); draw_fn(d)
    if blur: m = m.filter(ImageFilter.GaussianBlur(blur * k))
    return np.asarray(m, np.float32) / 255

def over(m, col, a=1.0):
    global img
    m = (m * a)[..., None]
    img = img * (1 - m) + (C(col) if isinstance(col, str) else col) * m

def add(m, col, a=1.0):
    global img
    img = img + m[..., None] * (C(col) if isinstance(col, str) else col) * a

def glow(m, col, r, a=1.0):
    add(np.asarray(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r * k)), np.float32) / 255, col, a)

def poly(pts): return lambda d: d.polygon([P(*p) for p in pts], fill=255)
def ell(x, y, rx, ry): return lambda d: d.ellipse([P(x - rx, y - ry), P(x + rx, y + ry)], fill=255)

# ---------------------------------------------------------------- sky
t = yy / H
sky = (C('#04060b') * (1 - t[..., None]) ** 1.5 + C('#0e1522') * (1 - np.abs(t - 0.45)[..., None] * 2).clip(0) + C('#3b100c') * (t[..., None] ** 2.2) * 1.4)
img[:] = sky
MX, MY, MR = 1920, 720, 400
d2 = np.sqrt((xx - MX * k) ** 2 + (yy - MY * k) ** 2) / k
add(np.exp(-(d2 / 900) ** 2), '#5c140c', 1.0)                  # the red bloom round the moon
add(np.exp(-(d2 / 2200) ** 2), '#2a0b0a', 0.9)
# stars
st = rng.random((H, W)) > 0.9993
add(np.asarray(Image.fromarray((st * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6 * k)), np.float32) / 255 * (1 - t) ** 2 * (d2 > 900), '#c8d4ff', 2.2)
# the blood moon
moon = (d2 < MR).astype(np.float32)
mar = fbm(W, H, 6, 6, seed=3)
limb = np.sqrt(np.clip(1 - (d2 / MR) ** 2, 0, 1))
mcol = (C('#e0502c') * (0.55 + 0.45 * limb[..., None]) * (0.75 + 0.5 * (mar[..., None] - 0.5) * 2.2).clip(0.35, 1.2))
img = img * (1 - moon[..., None]) + mcol * moon[..., None]
add(np.clip(1 - np.abs(d2 - MR) / 22, 0, 1), '#ff8a50', 0.35)
# clouds, lit red where the moon is behind them
cl = fbm(W, H, 7, 3, seed=5)
band = np.exp(-((yy / k - 980) / 260) ** 2) + 0.7 * np.exp(-((yy / k - 380) / 180) ** 2)
cm = np.clip((cl - 0.48) * 4.5, 0, 1) * band.clip(0, 1)
over(cm, (C('#0b0d14') * 0.9 + C('#5a1a12') * np.exp(-(d2 / 700) ** 2)[..., None] * 1.3), 0.85)
add(cm * np.exp(-(d2 / 620) ** 2), '#ff5a2a', 0.25)

# ---------------------------------------------------------------- mountains and fog
def ridge(y0, amp, seed, col, fog_col, fog_a):
    r = np.random.default_rng(seed); xs = np.linspace(0, 3840, 400)
    h = np.zeros_like(xs)
    for o in range(6):
        f = 2 ** o * 1.6; h += np.sin(xs / 3840 * f * 2 * np.pi + r.random() * 6.3) * amp / (1.7 ** o) + (r.random(len(xs)) - 0.5) * amp * 0.05 / (o + 1)
    pts = [(0, 2160)] + list(zip(xs, y0 - np.abs(h))) + [(3840, 2160)]
    m = mask(poly(pts))
    over(m, col)
    fog = np.exp(-((yy / k - (y0 - 40)) / 130) ** 2) * (0.6 + 0.6 * fbm(W, H, 5, 4, seed=seed + 9))
    add(fog * (1 - m * 0.4), fog_col, fog_a)
ridge(1330, 260, 1, C('#141821'), '#2c2f3c', 0.35)
ridge(1450, 170, 2, C('#0d1017'), '#33262a', 0.35)
ridge(1600, 110, 4, C('#080a0f'), '#26303a', 0.40)

# ---------------------------------------------------------------- the Echo Altar
SX, SB = 1920, 1790           # centre, dais top
Wd = 1300
def rect(x0, y0, x1, y1): return poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
stone, dark = C('#1a1716'), C('#0c0b0b')
# dais and steps
over(mask(poly([(SX - Wd / 2 - 120, SB + 150), (SX - Wd / 2, SB), (SX + Wd / 2, SB), (SX + Wd / 2 + 120, SB + 150)])), stone * 0.9)
over(mask(rect(SX - Wd / 2, SB - 8, SX + Wd / 2, SB + 10)), C('#3a2a22'))
for i in range(3):
    over(mask(rect(SX - 330 + i * 30, SB + 40 + i * 40, SX + 330 - i * 30, SB + 80 + i * 40)), stone * (0.75 - 0.1 * i))
# back wall with three alcoves
WT = SB - 560
wall = mask(rect(SX - Wd / 2 + 40, WT, SX + Wd / 2 - 40, SB))
stn = fbm(W, H, 6, 30, seed=61)
mortar = ((yy / k - WT) % 70 < 5).astype(np.float32) * 0.5 + (((xx / k + ((yy / k - WT) // 70 % 2) * 60) % 120) < 4).astype(np.float32) * 0.4
over(wall, (stone * (0.7 + 0.6 * stn[..., None])) * (1 - 0.6 * mortar[..., None]))
add(wall * np.exp(-((yy / k - SB) / 260) ** 2), '#3a1a10', 0.6)
alc = [(-570, -150, '#ff6a1c', 470), (-90, 230, '#a85cff', 380), (290, 540, '#ffb648', 440)]
for x0, x1, col, h in alc:
    m = mask(rect(SX + x0, SB - h, SX + x1, SB))
    over(m, C(col) * 0.03)
    glow(m, col, 70, 0.22)
    ax = SX + (x0 + x1) / 2
    add(np.exp(-(((xx / k - ax) / ((x1 - x0) * 0.5)) ** 2 + ((yy / k - (SB - h * 0.35)) / (h * 0.55)) ** 2)) * m, col, 0.42)
# Reforge: a ring gate of stone, burning inside
rx = SX - 360
ring = mask(ell(rx, SB - 250, 190, 230)) - mask(ell(rx, SB - 250, 140, 180))
over(np.clip(ring, 0, 1), C('#2a2320'))
inner = mask(ell(rx, SB - 250, 140, 180))
fire = fbm(W, H, 5, 24, seed=21)
add(inner * np.clip((fire - 0.35) * 2.5, 0, 1), '#ff7a26', 1.1)
glow(inner, '#ff5a10', 50, 0.6)
for a in np.linspace(0, 2 * np.pi, 9)[:-1]:   # rune sparks on the ring
    glow(mask(ell(rx + math.cos(a) * 165, SB - 250 + math.sin(a) * 205, 9, 9)), '#ffd28a', 6, 1.5)
# Coffer: a chest with a staff spinning above it in violet fire
cx = SX + 70
over(mask(rect(cx - 150, SB - 120, cx + 150, SB)), C('#24160f'))
over(mask(lambda d: d.chord([P(cx - 150, SB - 190), P(cx + 150, SB - 70)], 180, 360, fill=255)), C('#2c1b12'))
over(mask(rect(cx - 150, SB - 135, cx + 150, SB - 118)), C('#6b4a24'))
glow(mask(rect(cx - 140, SB - 128, cx + 140, SB - 124)), '#c890ff', 12, 1.4)
st = mask(poly([(cx - 150, SB - 300), (cx + 140, SB - 380), (cx + 148, SB - 368), (cx - 142, SB - 288)]))
over(st, C('#3a2a3c'))
glow(mask(ell(cx + 150, SB - 378, 22, 22)), '#d7a0ff', 30, 1.6)
glow(st, '#a860ff', 25, 0.5)
# Brew Machine: a tall iron cabinet with a glowing window of bottles
bx = SX + 415
over(mask(rect(bx - 110, SB - 430, bx + 110, SB)), C('#191514'))
win = mask(rect(bx - 80, SB - 390, bx + 80, SB - 170))
over(win, C('#5a3510'))
for i in range(3):
    for j in range(3):
        glow(mask(ell(bx - 50 + i * 50, SB - 350 + j * 70, 13, 22)), ['#ff5040', '#50d0ff', '#ffd040'][(i + j) % 3], 10, 1.2)
glow(win, '#ffb040', 40, 0.4)
# timber lintel, posts, canopy with crossed dragon heads on the gable
over(mask(rect(SX - Wd / 2 + 20, WT - 40, SX + Wd / 2 - 20, WT + 20)), C('#22170f'))
for px in (SX - Wd / 2 - 10, SX + Wd / 2 - 50):
    over(mask(rect(px, WT - 120, px + 60, SB + 10)), C('#1d140d'))
roof = [(SX - Wd / 2 - 170, WT - 60), (SX, WT - 380), (SX + Wd / 2 + 170, WT - 60), (SX + Wd / 2 + 120, WT - 20), (SX, WT - 320), (SX - Wd / 2 - 120, WT - 20)]
over(mask(poly(roof)), C('#100c0a'))
over(mask(poly([(SX - Wd / 2 - 120, WT - 20), (SX, WT - 320), (SX + Wd / 2 + 120, WT - 20), (SX + Wd / 2 - 20, WT - 20), (SX, WT - 260), (SX - Wd / 2 + 20, WT - 20)])), C('#18100b'))
for sgn in (-1, 1):   # the crossed dragon-head finials (Nordic gables)
    pts = [(SX, WT - 360), (SX + sgn * 60, WT - 470), (SX + sgn * 140, WT - 520), (SX + sgn * 190, WT - 500), (SX + sgn * 150, WT - 485),
           (SX + sgn * 170, WT - 455), (SX + sgn * 110, WT - 470), (SX + sgn * 70, WT - 410), (SX + sgn * 25, WT - 340)]
    over(mask(poly(pts)), C('#0c0908'))
    glow(mask(ell(SX + sgn * 150, WT - 500, 6, 6)), '#ff9050', 8, 1.5)
# rim of moonlight on the roof
add(mask(poly([(SX - Wd / 2 - 170, WT - 60), (SX, WT - 380), (SX + Wd / 2 + 170, WT - 60), (SX + Wd / 2 + 160, WT - 52), (SX, WT - 368), (SX - Wd / 2 - 160, WT - 52)])), '#ff6a3a', 0.45)
# braziers either side
flame_n = fbm(W, H, 5, 60, seed=71)
flame_t = np.asarray(Image.fromarray(fbm(W // 4, H // 4, 5, 90, seed=72)).resize((W, H), Image.BICUBIC))  # tall tongues
flame_t = np.asarray(Image.fromarray(fbm(W, H // 3, 5, 70, seed=73)).resize((W, H), Image.BICUBIC))
for bx2 in (SX - Wd / 2 - 160, SX + Wd / 2 + 160):
    over(mask(poly([(bx2 - 60, SB - 120), (bx2 + 60, SB - 120), (bx2 + 25, SB - 60), (bx2 + 15, SB + 120), (bx2 - 15, SB + 120), (bx2 - 25, SB - 60)])), C('#14100d'))
    fy = (SB - 120 - yy / k) / 320; fx = (xx / k - bx2) / 85
    env_ = np.clip(1 - (fx / np.maximum(0.02, (1 - np.clip(fy, 0, 1)) ** 0.8 * 1.0)) ** 2, 0, 1) * (fy > -0.05) * (fy < 1)
    fl = np.clip(env_ * 1.3 - 0.3 - fy * 0.7 + (flame_t - 0.5) * 2.6, 0, 1) * np.clip(env_ * 4, 0, 1)
    add(fl, '#b02a06', 1.1); add(np.clip(fl * 1.5 - 0.5, 0, 1), '#ff7a20', 0.9); add(np.clip(fl * 2 - 1.3, 0, 1), '#ffd890', 0.7); glow(fl, '#ff4a10', 120, 0.9)

# ---------------------------------------------------------------- the barrow-yard foreground
gy = fbm(W, H, 5, 6, seed=31)
ground_top = 1800 + 60 * np.sin(xx / k / 3840 * 7) + 40 * (gy - 0.5)
gm = (yy / k > ground_top).astype(np.float32)
over(gm, C('#07080a'))
add(gm * np.exp(-((yy / k - 1820) / 70) ** 2), '#3a2a24', 0.6)
def stone_(x, y, w, h, tilt, kind):
    ca, sa = math.cos(tilt), math.sin(tilt)
    def rot(px, py): return (x + px * ca - py * sa, y + px * sa + py * ca)
    if kind == 0:   # rounded slab
        pts = [rot(-w / 2, 0), rot(-w / 2, -h + w / 2)] + [rot(math.cos(a) * w / 2, -h + w / 2 - math.sin(a) * w / 2) for a in np.linspace(math.pi, 0, 14)] + [rot(w / 2, 0)]
    elif kind == 1:  # cross
        pts = [rot(-w * .15, 0), rot(-w * .15, -h * .62), rot(-w / 2, -h * .62), rot(-w / 2, -h * .78), rot(-w * .15, -h * .78), rot(-w * .15, -h), rot(w * .15, -h), rot(w * .15, -h * .78), rot(w / 2, -h * .78), rot(w / 2, -h * .62), rot(w * .15, -h * .62), rot(w * .15, 0)]
    else:            # Nordic standing stone
        pts = [rot(-w / 2, 0), rot(-w * .4, -h * .8), rot(-w * .1, -h), rot(w * .35, -h * .9), rot(w / 2, 0)]
    m = mask(poly(pts)); over(m, C('#121116') * (0.8 + 0.4 * stn[..., None]))
    add(np.clip(m - np.roll(np.roll(m, int(5 * k), axis=0), int(4 * k), axis=1), 0, 1), '#ff6a40', 0.7)
for x, y, w, h, tl, kd in [(180, 2010, 150, 260, -0.12, 0), (420, 1950, 110, 230, 0.15, 1), (640, 2060, 170, 210, 0.05, 2), (3260, 1990, 130, 250, 0.1, 0),
                            (3520, 2070, 150, 300, -0.2, 1), (3740, 1960, 120, 200, 0.08, 2), (920, 1900, 80, 140, -0.1, 0), (2950, 1910, 90, 150, 0.12, 1)]:
    stone_(x, y, w, h, tl, kd)

def risen(x, y, s, lean, arm, eye='#7fe8ff', blade=False):
    """a hunched, ragged draugr silhouette; feet at (x, y), ~520*s tall, facing arm (+1 right, -1 left)"""
    f = 1 if arm > 0 else -1
    def p(px, py): return (x + f * px * s, y + py * s)
    m = Image.new('L', (W, H), 0); d = ImageDraw.Draw(m)
    def limb(a, b, w):
        (ax, ay), (bx_, by) = p(*a), p(*b); L_ = math.hypot(bx_ - ax, by - ay) or 1
        nx_, ny_ = -(by - ay) / L_ * w * s / 2, (bx_ - ax) / L_ * w * s / 2
        d.polygon([P(ax + nx_, ay + ny_), P(bx_ + nx_, by + ny_), P(bx_ - nx_, by - ny_), P(ax - nx_, ay - ny_)], fill=255)
        d.ellipse([P(bx_ - w * s / 2, by - w * s / 2), P(bx_ + w * s / 2, by + w * s / 2)], fill=255)
    # legs: one striding, one dragging
    limb((-30, -250), (-45, -130), 34); limb((-45, -130), (-70, -5), 28); limb((-70, -5), (-25, 0), 18)
    limb((10, -250), (55, -135), 34); limb((55, -135), (45, -5), 28); limb((45, -5), (85, 0), 18)
    # hunched torso and ragged hem
    d.polygon([P(*q) for q in [p(-55, -255), p(-78, -330), p(-72, -410), p(-40, -462), p(5, -470), p(35, -440), p(40, -360), p(25, -255)]], fill=255)
    hem = [p(-60, -250)]
    for i, hx in enumerate(np.linspace(-60, 35, 9)):
        hem.append(p(hx, -250 + (60 + 25 * ((i * 7) % 3)) * (i % 2)))
    hem.append(p(35, -250))
    d.polygon([P(*q) for q in hem], fill=255)
    # head pushed forward and down
    hx_, hy_ = p(40, -478)
    d.ellipse([P(hx_ - 36 * s, hy_ - 40 * s), P(hx_ + 36 * s, hy_ + 36 * s)], fill=255)
    # arms: one reaching, one hanging, clawed hands
    limb((5, -440), (95, -385), 26); limb((95, -385), (200, -405), 22)
    for cl in (-18, -2, 14):
        limb((200, -405), (240, -405 + cl - 8), 7)
    limb((-30, -440), (-10, -340), 24); limb((-10, -340), (30, -270), 20)
    for cl in (-10, 6):
        limb((30, -270), (50 + cl, -240), 6)
    if blade:   # the Iron Reaper: a great hooked blade over its shoulder
        limb((-40, -470), (-150, -760), 16)
        d.polygon([P(*q) for q in [p(-150, -760), p(-140, -790), p(80, -760), p(190, -680), p(70, -720)]], fill=255)
        d.polygon([P(*q) for q in [p(-72, -400), p(-110, -470), p(-60, -520), p(10, -500), p(-20, -460)]], fill=255)   # pauldron
    m = np.asarray(m.filter(ImageFilter.GaussianBlur(1.2 * k)), np.float32) / 255
    over(m, C('#030304'))
    sh_ = max(2, int(2.6 * k * s))
    rim = np.clip(m - np.roll(np.roll(m, sh_, axis=0), -f * sh_, axis=1), 0, 1)
    add(rim, '#ff6a40', 0.6)   # moonlight on the edges
    for side in (-1, 1):
        ex_, ey_ = p(48 + side * 13, -488)
        e = mask(lambda d, ex_=ex_, ey_=ey_: d.ellipse([P(ex_ - 6 * s, ey_ - 4 * s), P(ex_ + 6 * s, ey_ + 4 * s)], fill=255))
        add(e, eye, 2.5); glow(e, eye, 16 * s, 1.6)
for args, kw in [((1240, 1880, 0.42, 50, 1), {}), ((1010, 1930, 0.55, 30, 1), {}), ((780, 2000, 0.8, 60, 1), {}), ((250, 2200, 1.45, 40, 1), {}),
                 ((560, 2150, 1.15, 70, 1), {'eye': '#a0f0ff'}), ((2680, 1900, 0.5, -30, -1), {}), ((3040, 1990, 0.85, -40, -1), {}),
                 ((3420, 2180, 1.7, -50, -1), {'eye': '#ff6a40', 'blade': True})]:
    if MENU and args[0] == 3420:
        continue   # the menu's buttons stand there
    risen(*args, **kw)

# hands clawing up out of the barrow earth
for hx, hy, hs in [(1500, 2120, 1.0), (2300, 2140, 0.9), (2600, 2090, 0.7), (1000, 2130, 0.8)]:
    pts = [(hx - 25 * hs, hy), (hx - 20 * hs, hy - 120 * hs), (hx - 45 * hs, hy - 170 * hs), (hx - 30 * hs, hy - 175 * hs), (hx - 12 * hs, hy - 140 * hs), (hx - 10 * hs, hy - 205 * hs),
           (hx + 4 * hs, hy - 205 * hs), (hx + 6 * hs, hy - 150 * hs), (hx + 18 * hs, hy - 195 * hs), (hx + 30 * hs, hy - 190 * hs), (hx + 22 * hs, hy - 130 * hs), (hx + 25 * hs, hy)]
    over(mask(poly(pts)), C('#060607'))

# ---------------------------------------------------------------- the caster: back to us, both hands thrust out, an arcane lance
HX, HY = 2700, 2200
cloak = [(HX - 300, HY), (HX - 250, HY - 300), (HX - 215, HY - 500), (HX - 190, HY - 585), (HX - 120, HY - 640), (HX - 80, HY - 720), (HX - 20, HY - 760), (HX + 50, HY - 745),
         (HX + 105, HY - 690), (HX + 150, HY - 630), (HX + 205, HY - 585), (HX + 235, HY - 480), (HX + 260, HY - 300), (HX + 320, HY)]
hm = mask(poly(cloak))
# both arms reach forward and up toward the horde, sleeves hanging; open hands with the spell between them
OX, OY = 2330, 1500          # the spell held between the palms
hm = np.maximum(hm, mask(poly([(HX - 120, HY - 640), (HX - 200, HY - 600), (OX + 20, OY + 60), (OX + 60, OY + 20), (HX - 150, HY - 690)])))      # near arm (left)
hm = np.maximum(hm, mask(poly([(HX + 110, HY - 690), (HX + 40, HY - 720), (OX + 110, OY - 95), (OX + 150, OY - 60), (HX + 150, HY - 630)])))    # far arm, over the shoulder
for hx, hy, ang0 in ((OX + 40, OY + 40, 2.55), (OX + 125, OY - 80, 3.6)):   # hands: palm and splayed fingers
    hm = np.maximum(hm, mask(ell(hx, hy, 26, 22)))
    for f in range(4):
        a_ = ang0 + (f - 1.5) * 0.32
        hm = np.maximum(hm, mask(poly([(hx + math.cos(a_ + 1.57) * 6, hy + math.sin(a_ + 1.57) * 6), (hx + math.cos(a_) * 58, hy + math.sin(a_) * 58),
                                       (hx + math.cos(a_ - 1.57) * 6, hy + math.sin(a_ - 1.57) * 6)])))
hm = np.maximum(hm, mask(poly([(HX - 80, HY - 720), (HX - 10, HY - 860), (HX + 60, HY - 760)])))   # the hood's point
over(hm, C('#030304'))
rim = np.clip(hm - np.roll(np.roll(hm, int(6 * k), axis=0), int(6 * k), axis=1), 0, 1)
add(rim, '#ff7a4a', 0.9)
near = np.exp(-(((xx / k - OX) ** 2 + (yy / k - OY) ** 2) / 420.0 ** 2))
rim2 = np.clip(hm - np.roll(hm, -int(7 * k), axis=1), 0, 1) * (yy / k < HY - 300)
add(rim2 * (0.4 + near), '#60ffc0', 0.8)   # the spell's light on arms and hood
TX, TY = OX - 30, OY - 10
gem = mask(ell(TX, TY, 34, 34))
add(gem, '#9effd8', 2.0); glow(gem, '#40ffb0', 60, 1.8); glow(gem, '#20c080', 240, 0.7)
for i in range(9):   # wisps curling off the hands
    a0 = rng.uniform(0, 2 * math.pi); pts = []
    for s in np.linspace(0, 1, 7):
        r_ = 40 + 110 * s; a1 = a0 + 1.4 * s
        pts.append((TX + math.cos(a1) * r_, TY + math.sin(a1) * r_ * 0.8))
    add(mask(lambda d, pts=pts: d.line([P(*q) for q in pts], fill=255, width=max(1, int(3 * k)))), '#90ffd8', 0.5)
# the lance: a bright core to the risen at the left, with a burst where it strikes
EX, EY = 905, 1660
L = math.hypot(EX - TX, EY - TY)
ang = math.atan2(EY - TY, EX - TX)
nx, ny = -math.sin(ang), math.cos(ang)
core = mask(poly([(TX + nx * 10, TY + ny * 10), (EX + nx * 4, EY + ny * 4), (EX - nx * 4, EY - ny * 4), (TX - nx * 10, TY - ny * 10)]))
add(core, '#e8fff4', 1.5); glow(core, '#40ffb0', 18, 1.6); glow(core, '#10a070', 70, 0.8)
for i in range(7):   # crackle along it
    t0 = rng.random(); pts = []
    for s in np.linspace(t0, min(1, t0 + 0.12), 6):
        off = rng.normal(0, 30)
        pts.append((TX + (EX - TX) * s + nx * off, TY + (EY - TY) * s + ny * off))
    add(mask(lambda d, pts=pts: d.line([P(*q) for q in pts], fill=255, width=max(1, int(3 * k)))), '#b0ffe0', 0.9)
burst = mask(ell(EX, EY, 70, 70))
small = mask(ell(EX, EY, 26, 26))
glow(small, '#ffffff', 10, 2.5); glow(burst, '#60ffc0', 50, 1.6); glow(burst, '#20c080', 220, 0.9)
for a in np.linspace(0, 2 * math.pi, 9)[:-1] + 0.3:
    r1, r2 = 60, 110 + 90 * rng.random()
    ray = mask(poly([(EX + math.cos(a) * r1, EY + math.sin(a) * r1), (EX + math.cos(a + 0.05) * r2, EY + math.sin(a + 0.05) * r2), (EX + math.cos(a - 0.05) * r2, EY + math.sin(a - 0.05) * r2)]))
    glow(ray, '#80ffd0', 14, 0.6)

# ---------------------------------------------------------------- ground fog, embers, grade
fogn = fbm(W, H, 6, 5, seed=41)
fog = np.clip((fogn - 0.35) * 2, 0, 1) * np.exp(-((yy / k - 1950) / 220) ** 2)
add(fog, '#2b3a40', 0.55); add(fog * np.exp(-((xx / k - EX) / 700) ** 2), '#1a6050', 0.5)
em_o = Image.new('L', (W, H), 0); em_g = Image.new('L', (W, H), 0); do_, dg_ = ImageDraw.Draw(em_o), ImageDraw.Draw(em_g)
for _ in range(260):   # embers drifting up from the braziers and the strike
    ex = rng.choice([SX - Wd / 2 - 160, SX + Wd / 2 + 160, EX, rng.uniform(0, 3840)]) + rng.normal(0, 260)
    ey = rng.uniform(600, 2000); r = rng.uniform(1.5, 4.5); v = int(rng.uniform(120, 255))
    (do_ if rng.random() < 0.8 else dg_).ellipse([P(ex - r, ey - r * 1.6), P(ex + r, ey + r * 1.6)], fill=v)
for m_, col in ((em_o, '#ffb060'), (em_g, '#90ffd0')):
    a_ = np.asarray(m_, np.float32) / 255
    add(a_, col, 1.3); glow(a_, '#ff6020' if col == '#ffb060' else '#30ff a0'.replace(' ', ''), 6, 0.8)
vig = np.clip(1 - (((xx / W - 0.5) * 1.15) ** 2 + ((yy / H - 0.48) * 1.25) ** 2) * 1.35, 0, 1) ** 1.1
img *= (0.25 + 0.75 * vig)[..., None]
img = img / (1 + img / 300)   # soft highlight roll-off
img = img * 1.12
img += (rng.random((H, W, 1)) - 0.5) * 7   # grain

# ---------------------------------------------------------------- title
base = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).resize((W0, int(W0 * 9 / 16)), Image.LANCZOS)
w, h = base.size; kk = w / 3840
FB = '/usr/local/lib/python3.11/dist-packages/matplotlib/mpl-data/fonts/ttf/STIXGeneralBol.ttf'
import matplotlib, os
FB = os.path.join(os.path.dirname(matplotlib.__file__), 'mpl-data/fonts/ttf/STIXGeneralBol.ttf')
FR = os.path.join(os.path.dirname(matplotlib.__file__), 'mpl-data/fonts/ttf/STIXGeneral.ttf')

def spaced(text, font, track):
    widths = [font.getlength(c) for c in text]
    return widths, sum(widths) + track * (len(text) - 1)

def draw_text(layer, text, font, cy, track, fill):
    d = ImageDraw.Draw(layer); widths, tot = spaced(text, font, track); x = (w - tot) / 2
    for c, cw in zip(text, widths):
        d.text((x, cy), c, font=font, fill=fill, anchor='lm'); x += cw + track

title = 'THE CURSE OF ALDUIN'
fT = ImageFont.truetype(FB, int(250 * kk))
fS = ImageFont.truetype(FR, int(64 * kk))
ty = 300 * kk
m = Image.new('L', base.size, 0); draw_text(m, title, fT, ty, 34 * kk, 255)
ma = np.asarray(m, np.float32) / 255
arr = np.asarray(base, np.float32)
sh = np.asarray(m.filter(ImageFilter.GaussianBlur(14 * kk)), np.float32)[..., None] / 255
arr = arr * (1 - sh * 0.85)
gl = np.asarray(m.filter(ImageFilter.GaussianBlur(40 * kk)), np.float32)[..., None] / 255
arr += gl * C('#ff4a1a') * 0.55
gy_ = np.mgrid[0:h, 0:w][0].astype(np.float32)
grad = np.clip((gy_ - (ty - 110 * kk)) / (220 * kk), 0, 1)[..., None]
metal = C('#f4e2c0') * (1 - grad) + C('#b0582a') * grad
tex = fbm(w, h, 5, 40, seed=51)[..., None]
metal = metal * (0.82 + 0.3 * tex)
# carved bevel: light from above
emb = np.asarray(m.filter(ImageFilter.GaussianBlur(3 * kk)), np.float32) / 255
bev = np.clip((emb - np.roll(emb, int(4 * kk) or 1, axis=0)) * 3, -1, 1)[..., None]
metal = metal * (1 + 0.35 * bev)
arr = arr * (1 - ma[..., None]) + metal * ma[..., None]
# outline
edge = np.clip(np.asarray(m.filter(ImageFilter.MaxFilter(5)), np.float32) / 255 - ma, 0, 1)[..., None]
arr = arr * (1 - edge * 0.9) + C('#1a0806') * edge * 0.9
out = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
lay = Image.new('RGBA', out.size, (0, 0, 0, 0))
draw_text(lay, 'EVERY NIGHT AFTER HELGEN, THE DEAD RISE', fS, ty + 175 * kk, 14 * kk, (232, 200, 170, 235))
d = ImageDraw.Draw(lay)
for sgn in (-1, 1):   # knotwork-ish rules either side of the line
    x0 = w / 2 + sgn * 1060 * kk; x1 = w / 2 + sgn * 1500 * kk
    d.line([(x0, ty + 175 * kk), (x1, ty + 175 * kk)], fill=(200, 120, 80, 200), width=max(1, int(3 * kk)))
    d.polygon([(x0, ty + 175 * kk - 12 * kk), (x0 + sgn * 18 * kk, ty + 175 * kk), (x0, ty + 175 * kk + 12 * kk), (x0 - sgn * 18 * kk, ty + 175 * kk)], fill=(230, 140, 90, 230))
fB = ImageFont.truetype(FR, int(50 * kk))
if not MENU: draw_text(lay, 'ROUND-BASED UNDEAD SURVIVAL  •  HAND MAGIC & THE THU\'UM  •  THE UNDYING DIRGE', fB, h - 90 * kk, 10 * kk, (210, 190, 170, 200))
out = Image.alpha_composite(out.convert('RGBA'), lay).convert('RGB')
if MENU:  # Skyrim's own buttons (Continue, New, Load...) are drawn over the bottom-right: a darker, quieter corner for them
    ww, hh = out.size
    yy_, xx_ = np.mgrid[0:hh, 0:ww].astype(np.float32)
    dx = np.clip((0.80 - xx_ / ww) / 0.10, 0, None); dy = np.clip((0.62 - yy_ / hh) / 0.08, 0, None)
    shade = np.clip(1.0 - np.sqrt(dx ** 2 + dy ** 2), 0, 1)
    rgb = np.asarray(out, np.float32) * (1 - 0.55 * shade[..., None])
    out = Image.fromarray(rgb.astype(np.uint8), 'RGB')
out.save(OUT, quality=95)
print('wrote', OUT, out.size)
