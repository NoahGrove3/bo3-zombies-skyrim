#!/usr/bin/env python3
"""The main menu's shrine (Noah's picture, as the game's Shrine of the Dirge): a round rune floor on a dark misty
plain, a glowing blue crystal held over it in a ring of floating broken rock. The game animates it (src/Hud.cpp,
DrawMenu): the crystal turns (a sheet of frames) and pulses, the rocks float round it, the runes breathe.

    python3 tools/menushrine.py assets/ui

Writes (2560x1440 painting space; the C++ draws the moving parts with the numbers printed at the end):
  menu_shrine_ground.png   the plain, the standing stones, the rune floor (static)
  menu_shrine_glow.png     the floor's light alone (runes, rings, pool) - drawn over it, breathing
  menu_crystal.png         the crystal turning, 24 frames (6 x 4 sheet of 320x480)
  menu_shard0..8.png       the floating rocks
  menu_softglow.png        a round soft light
One projection for all of it: x = CX + X*S, y = CY - (Z*cos(E) - Y*sin(E))*S  (Y away from the viewer).
"""
import math, pathlib, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.spatial import ConvexHull

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'menushrine')
OUT.mkdir(parents=True, exist_ok=True)
W, H = 2560, 1440
CX, CY = 1060.0, 1185.0          # the floor's centre on the painting
S = 3.9                          # painting pixels per game unit
E = math.radians(17)             # how far we look down
HORIZON = 800
CRYSTAL_Z = 130.0


def proj(X, Y, Z):
    return CX + X * S, CY - (Z * math.cos(E) - Y * math.sin(E)) * S


def fbm(w, h, oct=6, base=4, seed=0, pers=0.55):
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(oct):
        n = base * 2 ** o
        g = rng.random((n + 1, int(n * w / h) + 2)).astype(np.float32)
        im = Image.fromarray((g * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
        out += np.asarray(im, np.float32) / 255 * amp
        tot += amp
        amp *= pers
    return out / tot


def rgba(rgb, a):
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), np.clip(a, 0, 1) * 255]).astype(np.uint8), 'RGBA')


def blur(m, r):
    return np.asarray(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)), np.float32) / 255


def maskdraw(fn, w=W, h=H):
    im = Image.new('L', (w, h), 0)
    fn(ImageDraw.Draw(im))
    return np.asarray(im, np.float32) / 255


# ---------------------------------------------------------------- the ground
def ground():
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    n = fbm(W, H, 7, 3, seed=3)
    n2 = fbm(W, H, 5, 12, seed=9)
    # the plain: dark slate going into mist at the horizon
    t = np.clip((yy - HORIZON) / (H - HORIZON), 0, 1)
    base = np.array([20, 22, 32], np.float32)
    far = np.array([52, 58, 82], np.float32)
    col = far[None, None] * (1 - t[..., None]) ** 2.2 + base[None, None] * (1 - (1 - t[..., None]) ** 2.2)
    col *= (0.75 + 0.45 * n[..., None]) * (0.9 + 0.2 * n2[..., None])
    # a soft ridge line on the horizon (far hills), barely there
    ridge = HORIZON - 40 - 60 * fbm(W, 1, 5, 3, seed=5)[0] * (1 + 0.6 * np.sin(np.linspace(0, 7, W)))
    a = (yy >= ridge[None, :]).astype(np.float32)
    hills = (yy < HORIZON + 10) & (yy >= ridge[None, :])
    col[hills] = col[hills] * 0.55 + np.array([60, 66, 92]) * 0.45
    # standing stones on the plain, broken, in silhouette
    stones = maskdraw(lambda d: [d.polygon(p, fill=255) for p in [
        [(330, 830), (352, 690), (388, 676), (398, 832)], [(470, 826), (486, 742), (512, 730), (520, 828)],
        [(1720, 818), (1738, 712), (1770, 700), (1782, 820)], [(1880, 822), (1890, 760), (1920, 752), (1928, 824)],
        [(2240, 830), (2262, 650), (2300, 640), (2316, 834)], [(140, 838), (150, 780), (176, 774), (182, 840)]]])
    col = col * (1 - stones[..., None]) + np.array([24, 26, 36]) * stones[..., None] * (0.8 + 0.4 * n[..., None])
    a = np.maximum(a, stones)
    # the floor's shadow on the plain
    sh = maskdraw(lambda d: d.ellipse([CX - 820, CY - 170, CX + 820, CY + 300], fill=255))
    col *= (1 - 0.45 * blur(sh, 60))[..., None]
    img = rgba(col, a)

    # ---- the rune floor: a stone drum, its top carved in rings
    R, HGT = 162.0, 26.0
    top = [proj(R * math.cos(t), R * math.sin(t), 0) for t in np.linspace(0, 2 * math.pi, 240)]
    bot = [proj(R * math.cos(t), R * math.sin(t), -HGT) for t in np.linspace(0, 2 * math.pi, 240)]
    side = maskdraw(lambda d: d.polygon([proj(R * math.cos(t), R * math.sin(t), -HGT) for t in np.linspace(0, math.pi, 120)] +
                                        [proj(R * math.cos(t), R * math.sin(t), 0) for t in np.linspace(math.pi, 0, 120)], fill=255))
    topm = maskdraw(lambda d: d.polygon(top, fill=255))
    stone = fbm(W, H, 7, 20, seed=11)
    cracks = fbm(W, H, 4, 60, seed=12)
    sidecol = np.array([58, 60, 74], np.float32)[None, None] * (0.6 + 0.6 * stone[..., None])
    # blocks in the drum's side
    blocks = np.zeros((H, W), np.float32)
    for k in range(24):
        t = k / 24 * 2 * math.pi
        x0, y0 = proj(R * math.cos(t), R * math.sin(t), 0)
        x1, y1 = proj(R * math.cos(t), R * math.sin(t), -HGT)
        if math.sin(t) <= 0.02:
            blocks += maskdraw(lambda d: d.line([(x0, y0), (x1, y1)], fill=255, width=4))
    sidecol *= (1 - 0.55 * np.clip(blocks, 0, 1))[..., None]
    # the top: pale worn stone, darker toward the rim
    yy2, xx2 = np.mgrid[0:H, 0:W].astype(np.float32)
    ux = (xx2 - CX) / S
    uy = (yy2 - CY) / (S * math.sin(E))
    rr = np.sqrt(ux ** 2 + uy ** 2)
    ang = np.arctan2(uy, ux)
    topcol = np.array([96, 100, 118], np.float32)[None, None] * (0.62 + 0.5 * stone[..., None]) * (1.05 - 0.25 * np.clip(rr / R, 0, 1)[..., None])
    groove = np.zeros_like(rr)
    for r0, wdt in ((150, 3.2), (122, 2.6), (84, 2.4), (52, 2.0)):
        groove += np.exp(-((rr - r0) / wdt) ** 2)
    # spokes between the rings
    for k in range(12):
        a0 = k / 12 * 2 * math.pi
        d = np.abs(np.angle(np.exp(1j * (ang - a0))))
        groove += np.exp(-(d * rr / 1.8) ** 2) * ((rr > 52) & (rr < 122))
    groove = np.clip(groove, 0, 1)
    # the rune band (between 122 and 150): glyphs cut into the stone
    rng = random.Random(4)
    gl = Image.new('L', (W, H), 0)
    gd = ImageDraw.Draw(gl)
    strokes = [[(0, -1, 0, 1), (0, -0.3, 0.6, -0.9)], [(-0.5, 1, 0, -1), (0.5, 1, 0, -1), (-0.3, 0.3, 0.3, 0.3)], [(0, -1, 0, 1), (-0.6, 0, 0.6, 0)],
               [(-0.6, -1, 0.6, 1), (0.6, -1, -0.6, 1)], [(0, -1, 0, 1), (0, 0.1, 0.6, 0.9)], [(-0.6, -0.8, 0.6, -0.8), (0, -0.8, 0, 1)],
               [(-0.5, -1, -0.5, 1), (-0.5, 0, 0.5, -0.6), (-0.5, 0, 0.5, 0.6)], [(0, -1, 0.6, 0), (0.6, 0, 0, 1), (0, 1, -0.6, 0), (-0.6, 0, 0, -1)]]
    NG = 28
    for k in range(NG):
        a0 = k / NG * 2 * math.pi
        cr = 136.0
        tang = (-math.sin(a0), math.cos(a0))
        rad = (math.cos(a0), math.sin(a0))
        for x0, y0, x1, y1 in strokes[rng.randrange(len(strokes))]:
            p0 = (cr * rad[0] + x0 * 6 * tang[0] - y0 * 9 * rad[0], cr * rad[1] + x0 * 6 * tang[1] - y0 * 9 * rad[1])
            p1 = (cr * rad[0] + x1 * 6 * tang[0] - y1 * 9 * rad[0], cr * rad[1] + x1 * 6 * tang[1] - y1 * 9 * rad[1])
            gd.line([proj(p0[0], p0[1], 0), proj(p1[0], p1[1], 0)], fill=255, width=7)
    glyph = np.asarray(gl, np.float32) / 255
    # the pool at the heart
    pool = np.clip(1 - rr / 46, 0, 1)
    topcol *= (1 - 0.6 * groove)[..., None]
    topcol *= (1 - 0.75 * glyph)[..., None]
    topcol = topcol * (1 - (pool > 0)[..., None] * 0.7) + np.array([30, 70, 120]) * (pool > 0)[..., None] * 0.7
    # rubble round the foot of the drum
    rub = np.zeros((H, W), np.float32)
    rr2 = random.Random(8)
    for k in range(40):
        t = rr2.random() * 2 * math.pi
        d = R + 8 + rr2.random() * 60
        x, y = proj(d * math.cos(t), d * math.sin(t), -HGT)
        s_ = 6 + rr2.random() * 16
        pts = [(x + s_ * math.cos(q) * (0.7 + 0.5 * rr2.random()), y + s_ * 0.6 * math.sin(q) * (0.7 + 0.5 * rr2.random()) - s_ * 0.3) for q in np.linspace(0, 2 * math.pi, 7)[:-1]]
        rub = np.maximum(rub, maskdraw(lambda dd: dd.polygon(pts, fill=255)))
    arr = np.asarray(img, np.float32)
    rgb, al = arr[..., :3], arr[..., 3] / 255
    rubcol = np.array([50, 52, 64], np.float32)[None, None] * (0.6 + 0.6 * stone[..., None])
    rgb = rgb * (1 - rub[..., None]) + rubcol * rub[..., None]
    rgb = rgb * (1 - side[..., None]) + sidecol * side[..., None]
    rgb = rgb * (1 - topm[..., None]) + topcol * topm[..., None]
    al = np.maximum(al, np.maximum(side, np.maximum(topm, rub)))
    # cold light from the crystal on the floor and the plain round it
    cx, cy = proj(0, 0, CRYSTAL_Z)
    lit = np.exp(-(((xx2 - CX) / 900) ** 2 + ((yy2 - CY) / 360) ** 2))
    rgb += np.array([30, 60, 110]) * (lit * 0.55)[..., None] * (al > 0)[..., None]
    rgba(rgb, al).save(OUT / 'menu_shrine_ground.png', optimize=True)

    # ---- the floor's own light: runes, rings, pool (drawn breathing over the ground)
    glow = np.clip(glyph * 1.0 + groove * 0.35 * (rr > 110) + pool * 1.2, 0, 1) * topm
    halo = blur(glow, 10) * 0.9 + blur(glow, 30) * 0.6
    g = np.clip(glow + halo, 0, 1)
    lcol = np.dstack([90 + 120 * glow, 170 + 70 * glow, 255 * np.ones_like(glow)])
    rgba(lcol, g).save(OUT / 'menu_shrine_glow.png', optimize=True)


# ---------------------------------------------------------------- a tiny flat-shaded renderer
LIGHT = np.array([-0.45, -0.55, 0.70]); LIGHT /= np.linalg.norm(LIGHT)


def view(v):
    """world (X right, Y away, Z up) -> view (x right, y down, depth away)"""
    X, Y, Z = v[..., 0], v[..., 1], v[..., 2]
    x = X
    y = -(Z * math.cos(E) - Y * math.sin(E))
    dep = Y * math.cos(E) + Z * math.sin(E)
    return np.stack([x, y, dep], -1)


def render(tris, cols, size, scale, center, ss=3, outline=None):
    """tris: (n,3,3) world; cols: (n,4) rgba; painter's algorithm, supersampled"""
    w, h = size
    img = Image.new('RGBA', (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    v = view(tris)
    order = np.argsort(-v[..., 2].mean(1))
    for i in order:
        pts = [((p[0] * scale + center[0]) * ss, (p[1] * scale + center[1]) * ss) for p in v[i]]
        c = tuple(int(x) for x in np.clip(cols[i], 0, 255))
        d.polygon(pts, fill=c)
        if outline is not None:
            oc = tuple(int(x) for x in np.clip(outline[i], 0, 255))
            d.line(pts + [pts[0]], fill=oc, width=max(1, ss))
    return img.resize((w, h), Image.LANCZOS)


def normals(tris):
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    return n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-9)


def crystal_mesh():
    tri = []
    h_top, h_bot, r = 44, 38, 21
    mid = [(r * math.cos(2 * math.pi * k / 8 + 0.2), r * math.sin(2 * math.pi * k / 8 + 0.2), 0) for k in range(8)]
    for k in range(8):
        a, b = mid[k], mid[(k + 1) % 8]
        tri.append([a, b, (0, 0, h_top)])
        tri.append([b, a, (0, 0, -h_bot)])
    for ang, hgt in ((0.0, 30), (2.1, 24), (4.2, 27)):
        bx, by = math.cos(ang) * 9, math.sin(ang) * 9
        base = [(bx + 5 * math.cos(2 * math.pi * j / 4), by + 5 * math.sin(2 * math.pi * j / 4), h_top * 0.55) for j in range(4)]
        tip = (bx * 1.6, by * 1.6, h_top * 0.55 + hgt)
        for j in range(4):
            tri.append([base[j], base[(j + 1) % 4], tip])
    return np.array(tri, np.float64)


def rotz(t, a):
    c, s = math.cos(a), math.sin(a)
    m = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    return t @ m.T


def crystal_sheet(frames=24, fw=448, fh=640):
    base = crystal_mesh()
    sheet = Image.new('RGBA', (fw * 6, fh * 4), (0, 0, 0, 0))
    for f in range(frames):
        t = rotz(base, f / frames * 2 * math.pi)
        n = normals(t)
        nv = view(n)
        lam = np.clip(-(nv[:, 0] * LIGHT[0] + nv[:, 1] * LIGHT[1]) * 0.7 + 0.3 - nv[:, 2] * 0.4, 0, 1)
        facing = np.clip(-nv[:, 2], 0, 1)
        col = np.zeros((len(t), 4))
        col[:, 0] = 70 + 150 * lam ** 1.5 + 40 * facing
        col[:, 1] = 150 + 95 * lam + 10 * facing
        col[:, 2] = 235 + 20 * lam
        col[:, 3] = 228
        out = np.zeros((len(t), 4))
        out[:, :3] = np.clip(col[:, :3] + 60, 0, 255)
        out[:, 3] = 255
        im = render(t, col, (fw, fh), 6.0, (fw / 2, fh * 0.56), outline=out)
        # light inside it: a soft core
        a = np.asarray(im, np.float32)
        core = maskdraw(lambda d: d.ellipse([fw / 2 - 44, fh * 0.56 - 100, fw / 2 + 44, fh * 0.56 + 66], fill=255), fw, fh)
        core = blur(core, 18) * (a[..., 3] / 255)
        a[..., :3] = np.clip(a[..., :3] + core[..., None] * np.array([120, 120, 80]), 0, 255)
        im = Image.fromarray(a.astype(np.uint8), 'RGBA')
        sheet.paste(im, ((f % 6) * fw, (f // 6) * fh))
    sheet.save(OUT / 'menu_crystal.png', optimize=True)


def shard_mesh(L, seed):
    rng = np.random.default_rng(seed)
    pts = []
    for _ in range(26):
        u = rng.uniform(-1, 1)
        th = rng.uniform(0, 2 * math.pi)
        taper = 1 - abs(u) ** 1.6
        pts.append((math.cos(th) * L * 0.2 * (0.4 + taper) * rng.uniform(0.7, 1.1), math.sin(th) * L * 0.12 * (0.4 + taper) * rng.uniform(0.7, 1.1), u * L * 0.5))
    pts = np.array(pts)
    hull = ConvexHull(pts)
    tris = pts[hull.simplices]
    # outward winding
    c = pts.mean(0)
    n = normals(tris)
    flip = ((tris.mean(1) - c) * n).sum(1) < 0
    tris[flip] = tris[flip][:, ::-1]
    return tris


def shards():
    specs = [(120, 0.5), (135, -0.4), (110, 0.9), (140, -0.8), (125, 0.25), (118, 1.1), (112, -0.2), (86, 0.6), (96, -1.0)]
    out = []
    for i, (L, tilt) in enumerate(specs):
        t = shard_mesh(L, 40 + i)
        # lean it (the picture's rocks hang at angles), turn it a little toward us
        c, s = math.cos(tilt), math.sin(tilt)
        t = t @ np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]]).T
        t = rotz(t, 0.6 * i)
        n = normals(t)
        nv = view(n)
        lam = np.clip(-(nv[:, 0] * LIGHT[0] + nv[:, 1] * LIGHT[1]) * 0.75 + 0.25 - nv[:, 2] * 0.35, 0, 1)
        rng = np.random.default_rng(70 + i)
        jit = rng.uniform(0.85, 1.12, len(t))
        col = np.zeros((len(t), 4))
        col[:, 0] = (30 + 92 * lam ** 1.3) * jit
        col[:, 1] = (28 + 84 * lam ** 1.3) * jit
        col[:, 2] = (40 + 100 * lam ** 1.3) * jit
        col[:, 3] = 255
        out_ = np.clip(col * 1.12, 0, 255)
        SH = 2.6  # the rocks' scale on the painting
        size = int(L * SH * 1.25) + 30
        im = render(t, col, (size, size), SH, (size / 2, size / 2), outline=out_)
        a = np.asarray(im, np.float32)
        m = a[..., 3] / 255
        # blue veins of the crystal's light running through the rock
        vm = Image.new('L', (size, size), 0)
        vd = ImageDraw.Draw(vm)
        for v in range(3):
            x, y = size / 2 + rng.uniform(-size * 0.15, size * 0.15), size / 2 + rng.uniform(-size * 0.3, size * 0.3)
            ang = rng.uniform(0, 2 * math.pi)
            pts = [(x, y)]
            for _ in range(7):
                ang += rng.uniform(-0.8, 0.8)
                x += math.cos(ang) * size * 0.06
                y += math.sin(ang) * size * 0.06
                pts.append((x, y))
            vd.line(pts, fill=255, width=2)
        vein = np.asarray(vm, np.float32) / 255 * (blur(m, 3) > 0.6)
        vglow = blur(vein, 4)
        a[..., :3] = np.clip(a[..., :3] * (1 - vein[..., None] * 0.8) + vein[..., None] * np.array([150, 220, 255]) + vglow[..., None] * np.array([40, 90, 160]), 0, 255)
        # the crystal side of each rock lit cold blue (rim light)
        rim = np.clip(m - blur(m, 6) * 0.98, 0, 1)
        a[..., :3] = np.clip(a[..., :3] + rim[..., None] * np.array([60, 110, 200]), 0, 255)
        Image.fromarray(a.astype(np.uint8), 'RGBA').save(OUT / f'menu_shard{i}.png', optimize=True)
        out.append(size)
    return out


def softglow(n=256):
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    r = np.hypot(xx - n / 2 + 0.5, yy - n / 2 + 0.5) / (n / 2)
    a = np.clip(1 - r, 0, 1) ** 2.2
    rgba(np.dstack([np.full_like(a, 255)] * 3), a).save(OUT / 'menu_softglow.png', optimize=True)


if __name__ == '__main__':
    ground()
    crystal_sheet()
    sizes = shards()
    softglow()
    cx, cy = proj(0, 0, CRYSTAL_Z)
    print(f'crystal centre {cx:.0f},{cy:.0f}; floor centre {CX:.0f},{CY:.0f}; S {S}; E {math.degrees(E):.0f}; shard sizes {sizes}')
    # a still to look at
    comp = Image.new('RGBA', (W, H), (14, 16, 30, 255))
    comp.alpha_composite(Image.open(OUT / 'menu_shrine_ground.png'))
    comp.alpha_composite(Image.open(OUT / 'menu_shrine_glow.png'))
    sh = Image.open(OUT / 'menu_crystal.png').crop((0, 0, 448, 640))
    comp.alpha_composite(sh, (int(cx - 224), int(cy - 640 * 0.56)))
    for i in range(9):
        a = i / 9 * 2 * math.pi
        r = 112
        x, y = proj(math.cos(a) * r, math.sin(a) * r, CRYSTAL_Z + (i % 3 - 1) * 30)
        im = Image.open(OUT / f'menu_shard{i}.png')
        comp.alpha_composite(im, (int(x - im.width / 2), int(y - im.height / 2)))
    comp.convert('RGB').resize((1280, 720)).save(OUT / 'shrine_preview.jpg', quality=88)
