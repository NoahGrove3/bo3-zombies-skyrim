"""The Curse of Alduin spell emblems for the ten spells (sheet: spellcraft): a forged iron medallion with the spell's sigil
burning in its colour (the HUD dock and the shrine's roll use these; the HUD lays a living flame aura round it from
flame_ring.png, tools/flameaura.py).

    python tools/spellicons.py <out dir>      writes w_<id>.png (256x256 RGBA, the medallion centred) for every spellcraft row
"""
import json, math, pathlib, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'assets' / 'ui')
SS = 4                                   # draw at 4x, scale down for smooth edges
W, H = 256, 256
CX, CY, R = 128, 128, 104                # the medallion (the HUD's aura hugs its rim)


def rgb(h): return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def canvas():
    return Image.new('RGBA', (W * SS, H * SS), (0, 0, 0, 0))


def P(pts, r=R * 0.62):
    """sigil coordinates (unit, centred, y down) -> big canvas"""
    return [((CX + x * r) * SS, (CY + y * r) * SS) for x, y in pts]


def glow(img, rad, strength=1.0):
    g = img.filter(ImageFilter.GaussianBlur(rad * SS))
    a = np.asarray(g, np.float32); a[..., 3] = np.clip(a[..., 3] * strength, 0, 255)
    return Image.fromarray(a.astype(np.uint8), 'RGBA')


def arc_pts(cx, cy, r, a0, a1, n=40):
    return [(cx + r * math.cos(a0 + (a1 - a0) * t / n), cy + r * math.sin(a0 + (a1 - a0) * t / n)) for t in range(n + 1)]


# ------------------------------------------------------------------------------------------------- the sigils
# each draws into d (ImageDraw on the big canvas) with fill colour f and line width scale s (in big-canvas pixels)
def sig_arcane(d, f, s):     # an eight-pointed star in a ring, a spark at its heart
    pts = []
    for k in range(16):
        a = k * math.pi / 8 - math.pi / 2; rr = 1.0 if k % 2 == 0 else 0.42
        pts.append((rr * math.cos(a), rr * math.sin(a)))
    d.polygon(P(pts), outline=f, width=int(5 * s))
    d.line(P(arc_pts(0, 0, 0.72, 0, 2 * math.pi, 60)), fill=f, width=int(4 * s))
    d.ellipse(P([(-0.16, -0.16), (0.16, 0.16)]), fill=f)


def chaikin(pts, n=3):
    for _ in range(n):
        q = []
        for k in range(len(pts)):
            a, b = pts[k], pts[(k + 1) % len(pts)]
            q += [(a[0] * 0.75 + b[0] * 0.25, a[1] * 0.75 + b[1] * 0.25), (a[0] * 0.25 + b[0] * 0.75, a[1] * 0.25 + b[1] * 0.75)]
        pts = q
    return pts


def sig_fire(d, f, s):       # a flame: a tall tongue with a side lick, hollowed by a smaller one inside
    outer = chaikin([(0.02, -1.0), (0.2, -0.58), (0.46, -0.42), (0.38, -0.12), (0.56, 0.12), (0.5, 0.46), (0.24, 0.7), (0, 0.74),
                     (-0.26, 0.68), (-0.5, 0.44), (-0.54, 0.08), (-0.36, -0.22), (-0.3, 0.04), (-0.16, -0.5)])
    d.polygon(P(outer), fill=f)
    inner = chaikin([(0.04, -0.3), (0.2, -0.02), (0.26, 0.3), (0.12, 0.52), (-0.1, 0.52), (-0.24, 0.3), (-0.16, 0.0)])
    d.polygon(P(inner), fill=(0, 0, 0, 255))


def sig_ice(d, f, s):        # a snow crystal: six arms, each with two branch pairs, a faceted heart
    for k in range(6):
        a = k * math.pi / 3 - math.pi / 2
        ux, uy = math.cos(a), math.sin(a)
        d.line(P([(0, 0), (ux, uy)]), fill=f, width=int(6 * s))
        for t, l in ((0.45, 0.32), (0.72, 0.22)):
            bx, by = ux * t, uy * t
            for side in (-1, 1):
                b = a + side * math.pi / 3.2
                d.line(P([(bx, by), (bx + math.cos(b) * l, by + math.sin(b) * l)]), fill=f, width=int(4 * s))
    hexp = [(0.2 * math.cos(k * math.pi / 3), 0.2 * math.sin(k * math.pi / 3)) for k in range(6)]
    d.polygon(P(hexp), fill=f)


def sig_shock(d, f, s):      # a forked lightning bolt beneath a storm arc
    d.line(P(arc_pts(0, -0.55, 0.62, math.pi * 1.05, math.pi * 1.95)), fill=f, width=int(5 * s))
    bolt = [(0.12, -0.55), (-0.28, 0.05), (0.02, 0.05), (-0.22, 0.85), (0.38, -0.12), (0.06, -0.12), (0.3, -0.55)]
    d.polygon(P(bolt), fill=f)
    d.line(P([(0.02, 0.05), (0.42, 0.5)]), fill=f, width=int(4 * s))


def sig_thorn(d, f, s):      # a thorned bramble branch, the thorns hooked, a spike at its tip
    pts = [(-0.78 + 1.5 * t, 0.72 - 1.5 * t + 0.18 * math.sin(t * math.pi)) for t in np.linspace(0, 1, 40)]
    d.line(P(pts), fill=f, width=int(11 * s))
    pts2 = [(-0.7 + 1.3 * t, -0.62 + 1.25 * t - 0.15 * math.sin(t * math.pi)) for t in np.linspace(0, 1, 40)]
    d.line(P(pts2), fill=f, width=int(8 * s))
    for k in range(1, 7):
        t = k / 7.5; x, y = -0.78 + 1.5 * t, 0.72 - 1.5 * t + 0.18 * math.sin(t * math.pi)
        side = 1 if k % 2 else -1
        nx, ny = 0.7071 * side, 0.7071 * side
        d.polygon(P([(x - 0.09, y - 0.03), (x + 0.09, y + 0.03), (x + nx * 0.42 + 0.08, y + ny * 0.42 - 0.12)]), fill=f)
    d.polygon(P([(0.6, -0.68), (0.76, -0.84), (0.98, -1.0), (0.82, -0.6)]), fill=f)
    for (x, y, a) in ((-0.35, 0.05, -0.6), (0.12, -0.18, 2.4)):       # two leaves
        pts = [(x + 0.22 * math.cos(a) * t + 0.07 * math.sin(t * math.pi) * -math.sin(a), y + 0.22 * math.sin(a) * t + 0.07 * math.sin(t * math.pi) * math.cos(a)) for t in np.linspace(0, 1, 12)]
        pts += [(x + 0.22 * math.cos(a) * t - 0.07 * math.sin(t * math.pi) * -math.sin(a), y + 0.22 * math.sin(a) * t - 0.07 * math.sin(t * math.pi) * math.cos(a)) for t in np.linspace(1, 0, 12)]
        d.polygon(P(pts), fill=f)


def sig_blood(d, f, s):      # a blood drop with two fangs above it
    pts = []
    for t in np.linspace(0, 2 * math.pi, 60):
        x = 0.42 * math.sin(t); y = -0.62 * math.cos(t)
        if y < 0:
            x *= (1 + y / 0.62) ** 1.4 * 1.0
        pts.append((x, y + 0.25))
    d.polygon(P(pts), fill=f)
    for side in (-1, 1):
        d.polygon(P([(side * 0.12, -0.9), (side * 0.42, -0.9), (side * 0.26, -0.5)]), fill=f)
    d.line(P([(-0.55, -0.92), (0.55, -0.92)]), fill=f, width=int(5 * s))


def sig_wind(d, f, s):       # a spiral gust with three wind lines
    pts = [(0.08 * t * math.cos(t), 0.08 * t * math.sin(t)) for t in np.linspace(0.5, 4.2 * math.pi, 90)]
    pts = [(x * 0.78, y * 0.78) for x, y in pts]
    d.line(P(pts), fill=f, width=int(5 * s))
    for y0, l in ((-0.55, 0.9), (0.0, 1.1), (0.55, 0.8)):
        d.line(P([(-0.95, y0 + 0.08), (-0.95 + l * 0.7, y0)]), fill=f, width=int(4 * s))


def sig_soul(d, f, s):       # a soul wisp: a flame-like spirit with hollow eyes
    pts = []
    for t in np.linspace(0, 1, 40):
        a = math.pi * t
        pts.append((0.5 * math.cos(a), 0.15 + 0.5 * math.sin(a)))
    tail = [(0.5, 0.15), (0.38, -0.35), (0.2, -0.15), (0.05, -0.85), (-0.12, -0.25), (-0.3, -0.5), (-0.5, 0.15)]
    d.polygon(P(pts + tail[::-1][1:] if False else tail + pts[::-1]), fill=f)
    for ex in (-0.18, 0.18):
        d.ellipse(P([(ex - 0.09, 0.08), (ex + 0.09, 0.3)]), fill=(0, 0, 0, 255))


def sig_dawn(d, f, s):       # the sun: a disc and twelve rays, long and short
    d.ellipse(P([(-0.36, -0.36), (0.36, 0.36)]), fill=f)
    for k in range(12):
        a = k * math.pi / 6; l = 1.0 if k % 2 == 0 else 0.72
        ux, uy = math.cos(a), math.sin(a)
        d.polygon(P([(ux * 0.45 - uy * 0.07, uy * 0.45 + ux * 0.07), (ux * 0.45 + uy * 0.07, uy * 0.45 - ux * 0.07), (ux * l, uy * l)]), fill=f)


def sig_void(d, f, s):       # a vortex: arms spiralling into a black heart
    for k in range(4):
        off = k * math.pi / 2
        pts = [((0.16 + 0.07 * t) * math.cos(t + off), (0.16 + 0.07 * t) * math.sin(t + off)) for t in np.linspace(0, 3 * math.pi, 60)]
        d.line(P(pts), fill=f, width=int(5 * s))
    d.ellipse(P([(-0.26, -0.26), (0.26, 0.26)]), fill=(0, 0, 0, 255))
    d.ellipse(P([(-0.3, -0.3), (0.3, 0.3)]), outline=f, width=int(3 * s))


SIGILS = {'arcane': sig_arcane, 'fire': sig_fire, 'ice': sig_ice, 'shock': sig_shock, 'thorn': sig_thorn, 'blood': sig_blood,
          'wind': sig_wind, 'soul': sig_soul, 'dawn': sig_dawn, 'void': sig_void}


# the four wonder weapons (sheet: spellcraft, 'sigil'): their own emblems
def sig_skull(d, f, s):        # Gravecaller: a draugr's skull, its eyes burning, a barrow-iron crown of spikes over it
    skull = chaikin([(0, -0.62), (0.42, -0.5), (0.58, -0.1), (0.5, 0.25), (0.3, 0.38), (0.3, 0.62), (-0.3, 0.62), (-0.3, 0.38), (-0.5, 0.25), (-0.58, -0.1), (-0.42, -0.5)], 2)
    d.polygon(P(skull), fill=f)
    for sx in (-1, 1):
        d.polygon(P(chaikin([(sx * 0.08, -0.05), (sx * 0.36, -0.12), (sx * 0.38, 0.08), (sx * 0.12, 0.12)], 1)), fill=(0, 0, 0, 255))
    d.polygon(P([(0, 0.18), (0.08, 0.32), (-0.08, 0.32)]), fill=(0, 0, 0, 255))
    for x in (-0.18, -0.06, 0.06, 0.18):
        d.line(P([(x, 0.42), (x, 0.62)]), fill=(0, 0, 0, 255), width=int(3 * s))
    for k in range(5):                                         # the crown
        x = -0.4 + 0.2 * k
        d.polygon(P([(x - 0.07, -0.58), (x, -0.9 if k % 2 == 0 else -0.78), (x + 0.07, -0.58)]), fill=f)


def sig_gauntlet(d, f, s):     # Frostbinder: an armoured fist raised, rimed with ice, shards bursting from the knuckles
    fist = [(-0.38, -0.15), (0.38, -0.15), (0.42, 0.25), (0.25, 0.42), (0.22, 0.9), (-0.22, 0.9), (-0.25, 0.42), (-0.42, 0.25)]
    d.polygon(P(fist), fill=f)
    for k in range(4):                                         # the knuckle plates
        x0 = -0.38 + 0.19 * k
        d.polygon(P([(x0 + 0.01, -0.15), (x0 + 0.18, -0.15), (x0 + 0.16, -0.38), (x0 + 0.03, -0.38)]), fill=f)
        d.line(P([(x0 + 0.02, -0.14), (x0 + 0.17, -0.14)]), fill=(0, 0, 0, 255), width=int(2 * s))
    for y in (0.5, 0.66):
        d.line(P([(-0.22, y), (0.22, y)]), fill=(0, 0, 0, 255), width=int(2.5 * s))
    for a, L in ((-2.1, 0.5), (-1.57, 0.62), (-1.05, 0.5), (-2.6, 0.4), (-0.55, 0.4)):   # ice shards
        x, y = math.cos(a), math.sin(a)
        bx, by = x * 0.42, y * 0.42 - 0.2
        px, py = -y, x
        d.polygon(P([(bx + px * 0.05, by + py * 0.05), (bx + x * L, by + y * L), (bx - px * 0.05, by - py * 0.05)]), fill=f)


def sig_crown(d, f, s):        # Wolf Queen's Crown: a spiked crown over a howling wolf's head
    band = [(-0.62, -0.05), (0.62, -0.05), (0.56, -0.22), (-0.56, -0.22)]
    d.polygon(P(band), fill=f)
    for k in range(5):
        x = -0.5 + 0.25 * k
        h = 0.62 if k == 2 else 0.5 if k in (1, 3) else 0.4
        d.polygon(P([(x - 0.09, -0.22), (x, -0.22 - h), (x + 0.09, -0.22)]), fill=f)
        d.ellipse(P([(x - 0.04, -0.26 - h), (x + 0.04, -0.18 - h)]), fill=f)
    wolf = chaikin([(-0.38, 0.92), (-0.36, 0.5), (-0.26, 0.22), (-0.12, 0.12), (-0.08, -0.04), (0.02, 0.1), (0.2, 0.14), (0.56, 0.02), (0.6, 0.1), (0.36, 0.3), (0.22, 0.46), (0.18, 0.92)], 1)
    d.polygon(P(wolf), fill=f)                                 # a wolf's head, raised in a howl
    for ex in (-0.16, 0.0):                                    # its ears laid back
        d.polygon(P([(ex - 0.06, 0.16), (ex - 0.16, -0.12), (ex + 0.06, 0.12)]), fill=f)
    d.ellipse(P([(0.1, 0.18), (0.18, 0.25)]), fill=(0, 0, 0, 255))
    d.line(P([(0.3, 0.27), (0.5, 0.14)]), fill=(0, 0, 0, 255), width=int(2 * s))


def sig_horn(d, f, s):         # Horn of the Barrow-Jarls: a great curled war-horn bound in iron bands, its call rolling out
    pts_o, pts_i = [], []
    for k in range(25):
        t = k / 24
        a = math.pi * (0.95 + 1.15 * t)
        r = 0.82 - 0.4 * t
        cx, cy = 0.05 + r * math.cos(a), 0.18 + r * math.sin(a) * 0.9
        w = 0.04 + 0.2 * t ** 1.4
        nx, ny = math.cos(a), math.sin(a)
        pts_o.append((cx + nx * w, cy + ny * w)); pts_i.append((cx - nx * w, cy - ny * w))
    d.polygon(P(pts_o + pts_i[::-1]), fill=f)
    for k in (8, 14, 20):
        d.line(P([pts_o[k], pts_i[k]]), fill=(0, 0, 0, 255), width=int(3 * s))
    mx, my = (pts_o[-1][0] + pts_i[-1][0]) / 2, (pts_o[-1][1] + pts_i[-1][1]) / 2
    for i, r in enumerate((0.22, 0.36, 0.5)):                    # the call
        d.line(P(arc_pts(mx, my, r, -0.7, 0.7, 16)), fill=f, width=int((4 - i) * s))


SIGILS.update({'skull': sig_skull, 'gauntlet': sig_gauntlet, 'crown': sig_crown, 'horn': sig_horn})


# ------------------------------------------------------------------------------------------------- the medallion
def medallion(color, accent, style):
    c = rgb(color); acc = rgb(accent)
    img = canvas()
    # forged iron bezel: a radial gradient ring, notched with runes, studded with rivets
    yy, xx = np.mgrid[0:H * SS, 0:W * SS].astype(np.float32)
    rr = np.sqrt((xx - CX * SS) ** 2 + (yy - CY * SS) ** 2) / SS
    ang = np.arctan2(yy - CY * SS, xx - CX * SS)
    bez = (rr <= R) & (rr >= R - 19)
    shade = 0.55 + 0.45 * np.cos(ang + 2.2)                    # light from the upper left
    grain = np.asarray(Image.fromarray((np.random.default_rng(3).random((H, W)) * 255).astype(np.uint8)).resize((W * SS, H * SS), Image.BILINEAR), np.float32) / 255
    metal = np.dstack([(52 + 70 * shade + 18 * grain) * 1.0, (50 + 66 * shade + 18 * grain) * 0.98, (54 + 62 * shade + 18 * grain) * 1.05])
    edge = np.clip(1 - np.abs(rr - (R - 9.5)) / 9.5, 0, 1) ** 0.5
    layer = np.zeros((H * SS, W * SS, 4), np.float32)
    layer[..., :3] = metal * (0.65 + 0.35 * edge[..., None]); layer[..., 3] = bez * 255
    # the inner disc: deep and dark, the spell's colour welling up from the middle
    disc = rr < R - 19
    inner = np.clip(1 - rr / (R - 19), 0, 1)
    dc = np.array([10, 10, 13], np.float32) + np.array(c, np.float32) * (0.12 + 0.42 * inner ** 2)[..., None]
    layer[disc, :3] = dc[disc]; layer[disc, 3] = 255
    img.alpha_composite(Image.fromarray(np.clip(layer, 0, 255).astype(np.uint8), 'RGBA'))
    d = ImageDraw.Draw(img)
    d.ellipse([(CX - R) * SS, (CY - R) * SS, (CX + R) * SS, (CY + R) * SS], outline=(20, 18, 20, 255), width=2 * SS)
    d.ellipse([(CX - R + 19) * SS, (CY - R + 19) * SS, (CX + R - 19) * SS, (CY + R - 19) * SS], outline=(14, 12, 14, 255), width=2 * SS)
    for k in range(24):                                         # rune notches round the bezel
        a = k * math.pi / 12
        r0, r1 = R - 15, R - 5
        if k % 6 == 0:                                          # four rivets
            x, y = CX + (R - 9.5) * math.cos(a), CY + (R - 9.5) * math.sin(a)
            d.ellipse([(x - 5) * SS, (y - 5) * SS, (x + 5) * SS, (y + 5) * SS], fill=(150, 140, 125, 255), outline=(30, 26, 24, 255), width=SS)
            continue
        d.line([((CX + r0 * math.cos(a)) * SS, (CY + r0 * math.sin(a)) * SS), ((CX + r1 * math.cos(a)) * SS, (CY + r1 * math.sin(a)) * SS)],
               fill=(24, 22, 24, 255), width=2 * SS)
    # the sigil: a wide soft glow, the coloured body, a white-hot core
    s = SS * 1.25
    for layer_col, blur, width_k in ((c + (255,), 7, 1.6), (c + (255,), 2.5, 1.15), (tuple(min(255, int(v * 0.4 + 255 * 0.6)) for v in acc) + (255,), 0, 0.55)):
        sig = canvas(); sd = ImageDraw.Draw(sig)
        SIGILS[style](sd, layer_col, s * width_k)
        if blur:
            img.alpha_composite(glow(sig, blur, 1.4))
        else:
            img.alpha_composite(sig)
    return img.resize((W, H), Image.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = json.load(open(ROOT / 'sheets' / 'spellcraft.json'))['rows']
    for r in rows:
        medallion(r['color'], r['accent'], r.get('sigil', r['style'])).save(OUT / f"w_{r['id']}.png")
        print('w_' + r['id'])


if __name__ == '__main__':
    main()
