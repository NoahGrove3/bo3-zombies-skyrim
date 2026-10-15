#!/usr/bin/env python3
"""The main menu's shrine (Noah's picture, as the game's Shrine of the Dirge): a round rune floor on a dark misty
plain, a glowing blue crystal held over it in a ring of floating broken rock. The game animates it (src/Hud.cpp,
DrawMenu): the crystal turns (a sheet of frames) and pulses, the rocks float round it, the runes breathe.

    python3 tools/menushrine.py assets/ui

Writes (2560x1440 painting space; the C++ draws the moving parts with the numbers printed at the end):
  menu_shrine_ground.png   the lake's tint and the rune floor floating on it (static)
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
CX, CY = 1160.0, 1190.0          # the floor's centre on the painting
S = 3.4                          # painting pixels per game unit
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
    # nothing above the far shore (the mountains are their own layer, tools/menulake.py); below it the lake: a dark
    # see-through tint over the reflection the game draws (src/Hud.cpp DrawMenu), deeper toward us, with a faint sheen
    a = np.zeros((H, W), np.float32)
    lake = yy >= HORIZON
    rip = 0.5 + 0.5 * np.sin(yy * (0.25 + 0.9 * t) + 5 * fbm(W, H, 3, 6, seed=21))
    wcol = np.array([6, 10, 22], np.float32)[None, None] * (0.9 + 0.2 * rip[..., None])
    col = np.where(lake[..., None], wcol, col)
    a = np.where(lake, 0.10 + 0.40 * t ** 1.3, a)
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
    rgb = rgb * (1 - side[..., None]) + sidecol * side[..., None]
    rgb = rgb * (1 - topm[..., None]) + topcol * topm[..., None]
    al = np.maximum(al, np.maximum(side, topm))
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
    """the shrine's heart: a faceted orb (icosphere, 320 facets) crowned with a small star of light"""
    t = (1 + 5 ** 0.5) / 2
    V = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    V = [np.array(v, float) / np.linalg.norm(v) for v in V]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(2):
        cache = {}
        def mid(i, j):
            k = (min(i, j), max(i, j))
            if k not in cache:
                m = V[i] + V[j]
                V.append(m / np.linalg.norm(m))
                cache[k] = len(V) - 1
            return cache[k]
        F2 = []
        for a_, b_, c_ in F:
            ab, bc, ca = mid(a_, b_), mid(b_, c_), mid(c_, a_)
            F2 += [(a_, ab, ca), (b_, bc, ab), (c_, ca, bc), (ab, bc, ca)]
        F = F2
    R = 24.0
    tri = [[tuple(V[i] * R) for i in f] for f in F]
    for ang, hgt in ((0.0, 22), (2.1, 16), (4.2, 18)):
        bx, by = math.cos(ang) * 5, math.sin(ang) * 5
        base = [(bx + 3.5 * math.cos(2 * math.pi * j / 4), by + 3.5 * math.sin(2 * math.pi * j / 4), R - 3) for j in range(4)]
        tip = (bx * 1.4, by * 1.4, R - 3 + hgt)
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
        cen = view(t.mean(1))
        hue = np.arctan2(t.mean(1)[:, 1], t.mean(1)[:, 0]) + f / frames * 6.283  # a prismatic sheen that turns with it
        col = np.zeros((len(t), 4))
        col[:, 0] = 120 + 100 * facing ** 2 + 35 * np.sin(hue * 2) * (1 - facing)
        col[:, 1] = 170 + 80 * facing ** 2 + 25 * np.sin(hue * 2 + 2.1) * (1 - facing)
        col[:, 2] = 240 + 15 * lam
        col[:, 3] = 238
        out = np.zeros((len(t), 4))
        im = render(t, col, (fw, fh), 5.3, (fw / 2, fh * 0.56))  # (no facet lines: a gem's cut, not a net)
        # light inside it: a soft core
        a = np.asarray(im, np.float32)
        core = maskdraw(lambda d: d.ellipse([fw / 2 - 62, fh * 0.56 - 62, fw / 2 + 62, fh * 0.56 + 62], fill=255), fw, fh)
        core = blur(core, 34) * (a[..., 3] / 255)
        a[..., :3] = np.clip(a[..., :3] + core[..., None] * np.array([150, 140, 110]), 0, 255)
        im = Image.fromarray(a.astype(np.uint8), 'RGBA')
        sheet.paste(im, ((f % 6) * fw, (f // 6) * fh))
    sheet.save(OUT / 'menu_crystal.png', optimize=True)


def shard_mesh(L, seed):
    """the same rocks as the shrine in the game (tools/shrine.py rock): long flat broken shards, closed hulls"""
    rng = np.random.default_rng(seed)
    pts = []
    for _ in range(48):
        u = rng.uniform(-1, 1)
        th = rng.uniform(0, 2 * math.pi)
        taper = 1 - abs(u) ** 1.8 * 0.55
        pts.append((math.cos(th) * L * 0.2 * taper * rng.uniform(0.75, 1.1), math.sin(th) * L * 0.09 * taper * rng.uniform(0.7, 1.1), u * L * 0.5))
    pts = np.array(pts)
    hull = ConvexHull(pts)
    tris = pts[hull.simplices]
    c = pts.mean(0)
    n = normals(tris)
    flip = ((tris.mean(1) - c) * n).sum(1) < 0
    tris[flip] = tris[flip][:, ::-1]
    return tris


def render_tex(tris, uvs, shade, size, scale, center, diff, glow, ss=3):
    """a textured rock: tris (n,3,3) world, uvs (n,3,2), shade (n,) light per facet; a depth buffer, supersampled.
    Returns the rock (RGBA) and its runes' light alone (RGBA, for the game to pulse)"""
    w, h = size[0] * ss, size[1] * ss
    zb = np.full((h, w), np.inf, np.float32)
    out = np.zeros((h, w, 4), np.float32)
    lit = np.zeros((h, w), np.float32)
    v = view(tris)
    dh, dw = diff.shape[:2]
    for i in range(len(tris)):
        P = np.stack([(v[i, :, 0] * scale + center[0]) * ss, (v[i, :, 1] * scale + center[1]) * ss], -1)
        x0, x1 = int(max(0, np.floor(P[:, 0].min()))), int(min(w - 1, np.ceil(P[:, 0].max())))
        y0, y1 = int(max(0, np.floor(P[:, 1].min()))), int(min(h - 1, np.ceil(P[:, 1].max())))
        if x1 < x0 or y1 < y0:
            continue
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1].astype(np.float32) + 0.5
        (ax, ay), (bx, by), (cx, cy) = P
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-6:
            continue
        l0 = ((by - cy) * (xx - cx) + (cx - bx) * (yy - cy)) / den
        l1 = ((cy - ay) * (xx - cx) + (ax - cx) * (yy - cy)) / den
        l2 = 1 - l0 - l1
        m = (l0 >= -1e-4) & (l1 >= -1e-4) & (l2 >= -1e-4)
        z = l0 * v[i, 0, 2] + l1 * v[i, 1, 2] + l2 * v[i, 2, 2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m &= z < sub
        if not m.any():
            continue
        u = l0 * uvs[i, 0, 0] + l1 * uvs[i, 1, 0] + l2 * uvs[i, 2, 0]
        vv = l0 * uvs[i, 0, 1] + l1 * uvs[i, 1, 1] + l2 * uvs[i, 2, 1]
        px = np.clip((u * dw).astype(int), 0, dw - 1)
        py = ((vv % 1.0) * dh).astype(int) % dh
        sub[m] = z[m]
        o = out[y0:y1 + 1, x0:x1 + 1]
        o[m, :3] = diff[py[m], px[m]] * shade[i]
        o[m, 3] = 255
        lit[y0:y1 + 1, x0:x1 + 1][m] = glow[py[m], px[m]]
    rock = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), 'RGBA').resize(size, Image.LANCZOS)
    g = Image.fromarray((np.clip(lit, 0, 1) * 255).astype(np.uint8)).resize(size, Image.LANCZOS)
    g = np.asarray(g, np.float32) / 255
    halo = blur(g, 3) * 0.7
    a = np.clip(g + halo, 0, 1)
    runes = Image.fromarray(np.dstack([np.full_like(a, 120), np.full_like(a, 200), np.full_like(a, 255), a * 255]).astype(np.uint8), 'RGBA')
    return rock, runes


def shards():
    """the floating rocks: the same 2K slate as the game's (tools/rockart.py), mapped round each like a sleeve, with
    a bracelet of runes round each end; their light is a layer of its own (menu_shard<i>_r.png) the game pulses"""
    sys.path.insert(0, str(pathlib.Path(__file__).parent))
    import rockart
    d, _, g = rockart.rock_maps(2048)
    diff = np.asarray(d, np.float32)
    glow = np.asarray(g, np.float32)[..., 2] / 255
    specs = [(120, 0.5), (135, -0.4), (110, 0.9), (140, -0.8), (125, 0.25), (118, 1.1), (112, -0.2), (86, 0.6), (96, -1.0)]
    out = []
    for i, (L, tilt) in enumerate(specs):
        t = shard_mesh(L, 40 + i)
        uvs = np.array([rockart.fix_seam([rockart.rock_uv(p[2], p[0], p[1], L) for p in tri]) for tri in t])
        # lean it (the picture's rocks hang at angles), turn it a little toward us
        c, s_ = math.cos(tilt), math.sin(tilt)
        t = t @ np.array([[c, 0, s_], [0, 1, 0], [-s_, 0, c]]).T
        t = rotz(t, 0.6 * i)
        nv = view(normals(t))
        lam = np.clip(-(nv[:, 0] * LIGHT[0] + nv[:, 1] * LIGHT[1]) * 0.75 + 0.25 - nv[:, 2] * 0.35, 0, 1)
        shade = 0.42 + 1.30 * lam ** 1.1
        SH = 2.9  # the rocks' scale on the painting
        size = int(L * SH * 1.25) + 30
        rock, runes = render_tex(t, uvs, shade, (size, size), SH, (size / 2, size / 2), diff, glow)
        rock.save(OUT / f'menu_shard{i}.png', optimize=True)
        runes.save(OUT / f'menu_shard{i}_r.png', optimize=True)
        out.append(size)
    return out


def orb_sheet(frames=24, fw=448, fh=640, ro=108.0):
    """the shrine's heart as an orb of living light: smooth glass, energy veins swirling inside it as it turns (each
    frame turns it a little further), white at the core, a cold rim, a soft highlight - no facets, nothing on top"""
    rng = np.random.default_rng(7)
    dirs = rng.normal(size=(9, 3)); dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
    freqs = rng.uniform(3.0, 9.0, 9); phases = rng.uniform(0, 6.283, 9)
    cx, cy = fw / 2, fh * 0.56
    yy, xx = np.mgrid[0:fh, 0:fw].astype(np.float32)
    x = (xx - cx) / ro; y = (yy - cy) / ro
    r2 = x * x + y * y; r = np.sqrt(r2)
    inside = r < 1.0
    z = np.sqrt(np.clip(1 - r2, 0, 1))
    sheet = Image.new('RGBA', (fw * 6, fh * 4), (0, 0, 0, 0))
    for f in range(frames):
        th = f / frames * 2 * math.pi
        # the point on the sphere (turned about the vertical) and the swirl inside it
        px = x * math.cos(th) + z * math.sin(th); pz = -x * math.sin(th) + z * math.cos(th); py = -y
        e = np.zeros_like(x)
        for d, fr, ph in zip(dirs, freqs, phases):
            e += np.sin((px * d[0] + py * d[1] + pz * d[2]) * fr + ph + np.sin(px * fr * 0.5 + ph) * 1.4)
        e = e / 9.0 * 0.5 + 0.5
        veins = np.clip(1 - np.abs(e - 0.5) * 7.0, 0, 1) ** 1.6          # thin bright lines where the swirl folds
        core = np.clip(1 - r, 0, 1) ** 1.4
        rim = np.clip((r - 0.72) / 0.28, 0, 1) ** 2.2 * inside
        spec = np.exp(-(((x + 0.38) / 0.28) ** 2 + ((y + 0.42) / 0.22) ** 2))
        R_ = 40 + 120 * core + 120 * veins + 120 * rim + 200 * spec
        G_ = 95 + 130 * core + 120 * veins + 140 * rim + 200 * spec
        B_ = 210 + 45 * core + 40 * veins + 45 * rim + 200 * spec
        col = np.dstack([R_, G_, B_])
        edge = np.clip((1.0 - r) * ro / 1.5, 0, 1)                            # a clean, smooth edge
        a = edge * 0.97
        # the light around it (a soft halo)
        halo = np.clip(1 - (r - 1.0) / 1.1, 0, 1) ** 2.5 * (~inside)
        col = np.where(inside[..., None], col, np.dstack([110 + 0 * x, 180 + 0 * x, 255 + 0 * x]))
        a = np.where(inside, a, halo * 0.55)
        im = rgba(np.clip(col, 0, 255), np.clip(a, 0, 1))
        sheet.paste(im, ((f % 6) * fw, (f // 6) * fh))
    sheet.save(OUT / 'menu_crystal.png', optimize=True)


def softglow(n=256):
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    r = np.hypot(xx - n / 2 + 0.5, yy - n / 2 + 0.5) / (n / 2)
    a = np.clip(1 - r, 0, 1) ** 2.2
    rgba(np.dstack([np.full_like(a, 255)] * 3), a).save(OUT / 'menu_softglow.png', optimize=True)


if __name__ == '__main__':
    ground()
    orb_sheet()
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
