"""HUD icons for the hand spells (512x192, transparent): the spell's sigil burning in its colour at the right, a
gathered orb of its power, and the power streaming off to the left like a cast leaving the hand.
    python tools/spellicons.py <out dir>      (writes w_<spell id>.png for every hand spell in sheets/weapons.json)"""
import json, math, pathlib, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
ROOT = pathlib.Path(__file__).parent.parent
OUT = pathlib.Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)
W, H, SS = 512, 192, 3
def rgb(h): return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))

# sigils, drawn in a unit box (-1..1) around the orb's centre
def flame(d, P):   d.polygon(P([(0, -.95), (.35, -.35), (.25, -.5), (.6, .1), (.45, .6), (0, .85), (-.45, .6), (-.6, .1), (-.3, -.3), (-.2, -.1)]), fill=255)
def flake(d, P):
    for k in range(6):
        a = k * math.pi / 3; d.line(P([(0, 0), (.9 * math.cos(a), .9 * math.sin(a))]), fill=255, width=SS * 7)
        for sgn in (-1, 1):
            b = a + sgn * .6; m = (.5 * math.cos(a), .5 * math.sin(a)); d.line(P([m, (m[0] + .3 * math.cos(b), m[1] + .3 * math.sin(b))]), fill=255, width=SS * 5)
def bolt(d, P):    d.polygon(P([(.15, -.95), (-.45, .1), (-.05, .1), (-.25, .95), (.5, -.2), (.08, -.2), (.35, -.95)]), fill=255)
def skull(d, P):
    d.ellipse(P([(-.6, -.75), (.6, .35)]), fill=255); d.rectangle(P([(-.35, .2), (.35, .7)]), fill=255)
    for x in (-.25, .25): d.ellipse(P([(x - .17, -.32), (x + .17, -.02)]), fill=0)
    for x in (-.18, 0, .18): d.line(P([(x, .35), (x, .7)]), fill=0, width=SS * 4)
def eye(d, P):
    d.polygon(P([(-.95, 0), (-.4, -.45), (.4, -.45), (.95, 0), (.4, .45), (-.4, .45)]), fill=255); d.ellipse(P([(-.3, -.3), (.3, .3)]), fill=0); d.ellipse(P([(-.12, -.12), (.12, .12)]), fill=255)
def moon(d, P):    d.ellipse(P([(-.8, -.8), (.8, .8)]), fill=255); d.ellipse(P([(-.35, -.95), (1.05, .65)]), fill=0)
def star(d, P):    d.polygon(P([(math.cos(k * math.pi / 5 - math.pi / 2) * (.95 if k % 2 == 0 else .4), math.sin(k * math.pi / 5 - math.pi / 2) * (.95 if k % 2 == 0 else .4)) for k in range(10)]), fill=255)
def sun(d, P):
    d.ellipse(P([(-.45, -.45), (.45, .45)]), fill=255)
    for k in range(12): a = k * math.pi / 6; d.line(P([(.58 * math.cos(a), .58 * math.sin(a)), (.95 * math.cos(a), .95 * math.sin(a))]), fill=255, width=SS * 6)
def drop(d, P):    d.polygon(P([(0, -.95), (.45, -.15), (.55, .25), (.35, .7), (0, .85), (-.35, .7), (-.55, .25), (-.45, -.15)]), fill=255)
def thorn(d, P):
    d.line(P([(0, .95), (0, -.9)]), fill=255, width=SS * 7)
    for y, s in ((-.5, 1), (-.1, -1), (.3, 1)): d.polygon(P([(0, y), (s * .65, y - .3), (0, y + .15)]), fill=255)
def bone(d, P):
    d.line(P([(-.6, .6), (.6, -.6)]), fill=255, width=SS * 12)
    for c in ((-.6, .6), (.6, -.6)):
        for o in ((-.15, -.1), (.1, .15)): d.ellipse(P([(c[0] + o[0] - .2, c[1] + o[1] - .2), (c[0] + o[0] + .2, c[1] + o[1] + .2)]), fill=255)
def spiral(d, P):
    pts = [(.9 * t / 12 * math.cos(t), .9 * t / 12 * math.sin(t)) for t in np.linspace(0.3, 12, 120)]
    d.line(P(pts), fill=255, width=SS * 7)
def wind(d, P):
    for y, l in ((-.45, .8), (0, 1), (.45, .7)):
        d.line(P([(-.9, y), (l * .6, y)]), fill=255, width=SS * 6); d.arc(P([(l * .6 - .25, y - .5), (l * .6 + .25, y)]), 270, 90, fill=255, width=SS * 6)
def hexs(d, P):
    d.polygon(P([(math.cos(k * math.pi / 3) * .85, math.sin(k * math.pi / 3) * .85) for k in range(6)]), outline=255, width=SS * 7)
    d.polygon(P([(math.cos(k * math.pi / 3 + math.pi / 6) * .4, math.sin(k * math.pi / 3 + math.pi / 6) * .4) for k in range(6)]), fill=255)
def shards(d, P):
    for a, l in ((-.4, .9), (.1, .75), (.6, .85), (1.2, .6)):
        c, s = math.cos(a - math.pi / 2), math.sin(a - math.pi / 2)
        d.polygon(P([(c * .1 - s * .12, s * .1 + c * .12), (c * l, s * l), (c * .1 + s * .12, s * .1 - c * .12)]), fill=255)
def mind(d, P):
    d.ellipse(P([(-.75, -.75), (.75, .75)]), outline=255, width=SS * 6)
    for k in range(3): a = k * 2.09; d.line(P([(0, 0), (.75 * math.cos(a), .75 * math.sin(a))]), fill=255, width=SS * 5)
    d.ellipse(P([(-.2, -.2), (.2, .2)]), fill=255)
def madness(d, P):
    d.polygon(P([(-.9, -.1), (-.3, -.8), (0, -.2), (.3, -.85), (.9, -.1), (.5, .7), (0, .3), (-.5, .7)]), fill=255)
def storm(d, P):
    d.ellipse(P([(-.8, -.85), (.2, -.15)]), fill=255); d.ellipse(P([(-.3, -.95), (.8, -.2)]), fill=255)
    d.polygon(P([(.1, -.2), (-.3, .45), (0, .45), (-.2, .95), (.4, .2), (.1, .2), (.3, -.2)]), fill=255)
SIG = {'sidearm': mind, 'centurion': skull, 'triad': shards, 'spider': thorn, 'drill': drop, 'steampump': bone, 'marksman': moon,
       'animunculi': hexs, 'kagrenac': eye, 'mammothlmg': spiral, 'longshot': star, 'aetherium': sun, 'flamerepeater': flame,
       'frostlancer': flake, 'stormcaller': bolt, 'embermortar': wind, 'wabbajack': madness, 'skyforgearc': storm}

rows = [r for r in json.load(open(ROOT / 'sheets/weapons.json'))['rows'] if r['kind'] in ('tome', 'wonder') and not r['retired']]
for r in rows:
    c = rgb(r['element']); w, h = W * SS, H * SS
    cx, cy, R = w * 0.73, h * 0.5, h * 0.36
    rng = np.random.default_rng(sum(map(ord, r['id'])))
    # the power streaming off to the left: tapering wisps
    trail = Image.new('L', (w, h), 0); td = ImageDraw.Draw(trail)
    for k in range(9):
        y0 = cy + rng.uniform(-R * .5, R * .5); amp = rng.uniform(R * .1, R * .35); ph = rng.uniform(0, 6.28)
        pts = [(cx - t, y0 + amp * math.sin(t / (R * .9) + ph) * (t / (w * .65))) for t in np.linspace(0, w * .65, 60)]
        for i in range(len(pts) - 1):
            wdt = int(SS * 7 * (1 - i / len(pts)) + 1)
            td.line([pts[i], pts[i + 1]], fill=int(200 * (1 - i / len(pts))), width=wdt)
    trail = trail.filter(ImageFilter.GaussianBlur(SS * 3))
    # the orb and a ring of runes round it
    orb = Image.new('L', (w, h), 0); od = ImageDraw.Draw(orb)
    od.ellipse((cx - R, cy - R, cx + R, cy + R), fill=110)
    ring = Image.new('L', (w, h), 0); rd = ImageDraw.Draw(ring)
    rd.ellipse((cx - R * 1.12, cy - R * 1.12, cx + R * 1.12, cy + R * 1.12), outline=255, width=SS * 3)
    for k in range(16):
        a = k * math.pi / 8; r0, r1 = R * 1.12, R * (1.22 if k % 2 else 1.3)
        rd.line([(cx + r0 * math.cos(a), cy + r0 * math.sin(a)), (cx + r1 * math.cos(a), cy + r1 * math.sin(a))], fill=255, width=SS * 3)
    sig = Image.new('L', (w, h), 0); sd = ImageDraw.Draw(sig)
    P = lambda pts: [(cx + x * R * .72, cy + y * R * .72) for x, y in pts]
    SIG.get(r['id'], star)(sd, P)
    glow = Image.merge('L', [Image.fromarray(np.maximum.reduce([np.asarray(trail, np.float32), np.asarray(orb, np.float32), np.asarray(sig, np.float32), np.asarray(ring, np.float32) * .7]).astype(np.uint8))]).filter(ImageFilter.GaussianBlur(SS * 9))
    def layer(mask, col, k=1.0):
        im = Image.new('RGBA', (w, h), col + (0,)); im.putalpha(mask.point(lambda v: int(min(255, v * k)))); return im
    hot = tuple(min(255, int(v * .55 + 120)) for v in c)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    for lyr in (layer(glow, c, 1.1), layer(trail, c, 1.0), layer(orb, tuple(int(v * .35) for v in c), 1.6), layer(ring, hot, .9), layer(sig.filter(ImageFilter.MaxFilter(5)), (16, 10, 8)), layer(sig, hot), layer(sig.filter(ImageFilter.MinFilter(5)), (255, 250, 236), .7)):
        out = Image.alpha_composite(out, lyr)
    out.resize((W, H), Image.LANCZOS).save(OUT / f"w_{r['id']}.png")
    print('icon', r['id'])
