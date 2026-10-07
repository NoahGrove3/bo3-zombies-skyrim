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
def quicksilver(d):  # droplet with a spark
    d.polygon(P((.5, .2), (.66, .48), (.68, .58), (.62, .7), (.5, .76), (.38, .7), (.32, .58), (.34, .48)), fill=W)
    d.line(P((.52, .38), (.44, .55), (.56, .55), (.48, .7)), fill=(20, 20, 20, 255), width=16)
def twinstrike(d):   # two arrows
    for off in (-.09, .09):
        d.line(P((.3 + off, .72), (.66 + off, .32)), fill=W, width=22)
        d.polygon(P((.72 + off, .25), (.6 + off, .3), (.68 + off, .38)), fill=W)
def hearthfire(d):   # flame
    d.polygon(P((.5, .18), (.62, .38), (.6, .3), (.72, .52), (.68, .7), (.5, .8), (.32, .7), (.28, .52), (.4, .34), (.42, .44)), fill=W)
    d.polygon(P((.5, .48), (.58, .62), (.54, .74), (.46, .74), (.42, .62)), fill=(30, 30, 30, 255))
def windrunner(d):   # wing chevrons + speed lines
    for k in range(3):
        y = .36 + k * .12; d.line(P((.24, y), (.44, y)), fill=W, width=16)
    d.line(P((.5, .3), (.72, .5), (.5, .7)), fill=W, width=26, joint='curve')
    d.line(P((.6, .3), (.8, .5), (.6, .7)), fill=W, width=18, joint='curve')
def hawkeye(d):      # eye with crosshair
    d.ellipse(P((.24, .38), (.76, .62)), outline=W, width=18)
    d.ellipse(P((.43, .43), (.57, .57)), fill=W)
    for a, b in (((.5, .2), (.5, .32)), ((.5, .68), (.5, .8)), ((.16, .5), (.24, .5)), ((.76, .5), (.84, .5))):
        d.line(P(a, b), fill=W, width=16)
def mammoth(d):      # curved tusk
    d.arc(P((.26, .26), (.74, .74)), 160, 340, fill=W, width=42)
    d.ellipse(P((.24, .48), (.34, .58)), fill=W)
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

if __name__ == '__main__':
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build/icons'); out.mkdir(parents=True, exist_ok=True)
    perks = json.load(open(ROOT / 'sheets' / 'perks.json'))['rows']
    for p in perks:
        im, d = badge(p['color']); DRAW[p['id']](d)
        shadow = im.filter(ImageFilter.GaussianBlur(3))
        im.resize((128, 128), Image.LANCZOS).save(out / f"{p['id']}.png")
        print('icon', p['id'])
