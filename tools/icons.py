"""Undying Nights perk icons: original round badges, drawn here from simple shapes.
Writes <out>/<perk>.png (128x128 RGBA) for the HUD.   python tools/icons.py <out_dir>"""
import json, math, sys, pathlib
from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
S = 512  # draw big, scale down for smooth edges
W = (255, 255, 255, 255)

def rgb(h): return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
def badge(color):
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    c = rgb(color)
    d.ellipse((8, 8, S - 8, S - 8), fill=(10, 10, 12, 255))
    d.ellipse((8, 8, S - 8, S - 8), outline=c + (255,), width=34)
    inner = tuple(int(v * 0.35) for v in c) + (255,)
    d.ellipse((60, 60, S - 60, S - 60), fill=inner)
    # rune ticks around the rim
    for k in range(12):
        a = k * math.pi / 6; r0, r1 = S / 2 - 52, S / 2 - 38
        d.line([(S / 2 + r0 * math.cos(a), S / 2 + r0 * math.sin(a)), (S / 2 + r1 * math.cos(a), S / 2 + r1 * math.sin(a))], fill=(0, 0, 0, 180), width=8)
    return im, d

def P(*pts): return [(x * S, y * S) for x, y in pts]

def trollshide(d):   # shield with claw marks
    d.polygon(P((.5, .22), (.74, .3), (.72, .55), (.5, .8), (.28, .55), (.26, .3)), outline=W, width=22)
    for k in range(3):
        x = .40 + k * .08; d.line(P((x, .36), (x + .06, .62)), fill=W, width=18)
def quicksilver(d):  # a drop of quicksilver with a falling mark under it: spells cost less
    d.polygon(P((.5, .18), (.66, .46), (.68, .56), (.62, .67), (.5, .72), (.38, .67), (.32, .56), (.34, .46)), outline=W, width=20)
    d.line(P((.42, .52), (.58, .52)), fill=W, width=16)
    d.line(P((.5, .76), (.5, .86)), fill=W, width=14)
    d.line(P((.44, .81), (.5, .87), (.56, .81)), fill=W, width=12, joint='curve')
def twinstrike(d):   # two spell-bolts striking together, a burst where they meet
    for s_ in (-1, 1):
        d.line(P((.5 + s_ * .26, .74), (.5 + s_ * .04, .4)), fill=W, width=20)
        d.polygon(P((.5 + s_ * .01, .3), (.5 + s_ * .12, .42), (.5 + s_ * .0, .44)), fill=W)
    for a in range(8):
        t = a * math.pi / 4; r = .06 if a % 2 else .11
        d.line(P((.5, .3), (.5 + r * math.cos(t), .3 + r * math.sin(t))), fill=W, width=9)
def hearthfire(d):   # flame
    d.polygon(P((.5, .18), (.62, .38), (.6, .3), (.72, .52), (.68, .7), (.5, .8), (.32, .7), (.28, .52), (.4, .34), (.42, .44)), fill=W)
    d.polygon(P((.5, .48), (.58, .62), (.54, .74), (.46, .74), (.42, .62)), fill=(30, 30, 30, 255))
def windrunner(d):   # wing chevrons + speed lines
    for k in range(3):
        y = .36 + k * .12; d.line(P((.24, y), (.44, y)), fill=W, width=16)
    d.line(P((.5, .3), (.72, .5), (.5, .7)), fill=W, width=26, joint='curve')
    d.line(P((.6, .3), (.8, .5), (.6, .7)), fill=W, width=18, joint='curve')
def hawkeye(d):      # an open eye over a falling drop: magicka returns sooner
    d.ellipse(P((.22, .3), (.78, .54)), outline=W, width=18)
    d.ellipse(P((.43, .35), (.57, .49)), fill=W)
    d.polygon(P((.5, .58), (.58, .7), (.56, .77), (.5, .8), (.44, .77), (.42, .7)), fill=W)
def mammoth(d):      # a great curled tusk, banded: a broad back for more magicka
    d.arc(P((.22, .2), (.78, .76)), 150, 360, fill=W, width=48)
    for a in (200, 250, 300):
        t = math.radians(a); x, y = .5 + .25 * math.cos(t), .48 + .25 * math.sin(t)
        d.line(P((x - .05 * math.cos(t), y - .05 * math.sin(t)), (x + .05 * math.cos(t), y + .05 * math.sin(t))), fill=(0, 0, 0, 255), width=10)
    d.ellipse(P((.2, .44), (.32, .56)), fill=W)
def frostbite(d):    # snowflake
    for k in range(6):
        a = k * math.pi / 3; x, y = .5 + .28 * math.cos(a), .5 + .28 * math.sin(a)
        d.line(P((.5, .5), (x, y)), fill=W, width=18)
        for s in (-1, 1):
            b = a + s * 0.6; mx, my = .5 + .17 * math.cos(a), .5 + .17 * math.sin(a)
            d.line(P((mx, my), (mx + .08 * math.cos(b), my + .08 * math.sin(b))), fill=W, width=12)
    d.ellipse(P((.45, .45), (.55, .55)), fill=W)
def stormcrown(d):   # crown with a bolt
    d.polygon(P((.26, .64), (.26, .36), (.38, .5), (.5, .3), (.62, .5), (.74, .36), (.74, .64)), outline=W, width=18)
    d.line(P((.54, .5), (.46, .6), (.56, .6), (.48, .74)), fill=W, width=14)

DRAW = {'trollshide': trollshide, 'quicksilver': quicksilver, 'twinstrike': twinstrike, 'hearthfire': hearthfire,
        'windrunner': windrunner, 'hawkeye': hawkeye, 'mammoth': mammoth, 'frostbite': frostbite, 'stormcrown': stormcrown}

def medallion(color, glyph):
    """Dark fantasy medallion: hammered black iron, a bronze rim chased with knotwork dots, the perk's sigil cut
    into the face and burning with its colour."""
    import numpy as np
    c = rgb(color); R = S // 2
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32); r = np.hypot(xx - R, yy - R) / R
    rng = np.random.default_rng(len(color) * 7 + sum(c))
    noise = np.asarray(Image.fromarray((rng.random((64, 64)) * 255).astype(np.uint8)).resize((S, S), Image.BICUBIC), np.float32) / 255
    ang = np.arctan2(yy - R, xx - R)
    light = 0.5 + 0.5 * np.cos(ang + 2.3)                       # lit from the upper left
    face = np.stack([28, 26, 27], -1) * (0.75 + 0.5 * noise[..., None]) * (1.05 - 0.45 * r[..., None])
    bronze = np.array([112, 84, 50], np.float32)
    rim_band = (r > .80) & (r <= .97)
    rim = bronze * (0.45 + 0.75 * light[..., None]) * (0.85 + 0.3 * noise[..., None])
    inner_lip = (r > .74) & (r <= .80)
    lip = np.stack([18, 16, 16], -1) * np.ones_like(face)
    img = np.where(rim_band[..., None], rim, np.where(inner_lip[..., None], lip, face))
    alpha = np.clip((0.985 - r) * 60, 0, 1)
    # knotwork: bronze studs round the rim
    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB').convert('RGBA'); out.putalpha(Image.fromarray((alpha * 255).astype(np.uint8)))
    d = ImageDraw.Draw(out)
    for k in range(8):
        a = k * math.pi / 4 + math.pi / 8; x, y = R + .885 * R * math.cos(a), R + .885 * R * math.sin(a)
        rr = 11
        d.ellipse((x - rr, y - rr, x + rr, y + rr), fill=(40, 30, 20, 255)); d.ellipse((x - rr + 3, y - rr + 2, x + rr - 5, y + rr - 5), fill=(160, 126, 80, 255))
    # a thin ring of the brew's light just inside the lip
    ring = Image.new('L', (S, S), 0); rd = ImageDraw.Draw(ring); rr = int(.72 * R)
    rd.ellipse((R - rr, R - rr, R + rr, R + rr), outline=255, width=5)
    rglow = Image.new('RGBA', (S, S), c + (0,)); rglow.putalpha(ring.filter(ImageFilter.GaussianBlur(5)).point(lambda v: int(min(255, v * 1.6))))
    out = Image.alpha_composite(out, rglow)
    # the sigil: drawn white on a mask, then cut in (dark groove), lit edge, and a coloured fire inside
    m = Image.new('L', (S, S), 0); md = ImageDraw.Draw(m); glyph(_Mask(md))
    m = m.resize((S, S))
    mi = m.filter(ImageFilter.GaussianBlur(1.2))
    glow = Image.new('RGBA', (S, S), c + (0,)); glow.putalpha(m.filter(ImageFilter.GaussianBlur(22)).point(lambda v: int(min(255, v * 1.5))))
    out = Image.alpha_composite(out, glow)
    groove = Image.new('RGBA', (S, S), (6, 4, 4, 0)); groove.putalpha(m.filter(ImageFilter.MaxFilter(9)).point(lambda v: int(v * 0.9)))
    out = Image.alpha_composite(out, groove)
    hot = tuple(min(255, int(v * 0.6 + 110)) for v in c)
    fire = Image.new('RGBA', (S, S), hot + (0,)); fire.putalpha(mi)
    out = Image.alpha_composite(out, fire)
    core = Image.new('RGBA', (S, S), (255, 248, 230, 0)); core.putalpha(m.filter(ImageFilter.MinFilter(7)).point(lambda v: int(v * 0.55)))
    out = Image.alpha_composite(out, core)
    a = np.asarray(out.getchannel('A'), np.float32) * alpha
    out.putalpha(Image.fromarray(a.astype(np.uint8)))
    return out

class _Mask:
    """lets the shape functions (written for an RGBA draw with W fill) draw onto an L mask"""
    def __init__(self, d): self.d = d
    def __getattr__(self, name):
        f = getattr(self.d, name)
        def call(*a, **k):
            for key in ('fill', 'outline'):
                if key in k and k[key] is not None: k[key] = 255 if k[key] == W else 0
            return f(*a, **k)
        return call

if __name__ == '__main__':
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build/icons'); out.mkdir(parents=True, exist_ok=True)
    perks = json.load(open(ROOT / 'sheets' / 'perks.json'))['rows']
    for p in perks:
        medallion(p['color'], DRAW[p['id']]).resize((256, 256), Image.LANCZOS).save(out / f"{p['id']}.png")
        print('icon', p['id'])
