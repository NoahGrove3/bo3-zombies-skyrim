"""(Noah: the Alchemist) the Alchemist's thrown bomb flasks: the projectile (PROJ, Missile) models src/Classes.cpp ThrowFlask
lobs (tools/gen_esp.py UN_FlaskProj_<el>). Each a round-bellied glass flask modelled here from scratch (no one else's mesh):
a single-shell glass belly and neck (alpha blend, lit, glossy), the element's liquid glowing inside (glow map: it reads at
night), a cork stopper, and a cloth rag knotted round the neck (fire, poison, sun, blood) or a copper wire coil (frost,
shock). The fire flask's rag runs up through the cork as a wick and burns. The Philosopher's Flask, the class's wonder, is
larger (~26 units): a faceted crystal flask brimming with molten gold, a domed gold cap with a finial, a gold collar, a girdle of beads
and six gold ribs caging the belly.

About 18 units tall (the philosopher ~26), the neck on +Y (as Skyrim's projectiles fly, tools/throwdagger.py), the
flask's axis on Y and its height centred on the origin; it tumbles end over end about X, 2 turns a second, from load and
looping (tools/modelconv/spinwrap). No havok, no Prn, no skin. A small fizz rides the neck: the element's flipbook from
its bow's arrowhead (CurseOfAlduin\\bow\\<bid>_arrowfx.dds, tools/bowarrows.py) on camera-facing cards under the spinning
node (fxwrite parent=), plus a soft additive halo round the belly.

    python3 tools/flasks.py [--preview <png>]
      -> assets/fx/Meshes/CurseOfAlduin/alchemist/flask_{fire,frost,shock,poison,sun,blood,philosopher}.nif
         assets/fx/Textures/CurseOfAlduin/alchemist/*.dds

Pipeline: mesh -> OBJ -> modelconv (lit BSLightingShaderProperty shapes; glow maps on the glass and liquid) -> spinwrap
(FlaskSpin and its NiTransformController, BSX animated) -> fxwrite base= (the fizz and halo cards, parented to FlaskSpin).
build/flasks is emptied afterwards.

NIF layout:  BSFadeNode "AlchemistFlask" (+ BSXFlags 1: animated)
               NiNode "FlaskSpin"  <- NiTransformController (active|loop)
                 BSTriShape Glass / Liquid / Cork / Wrap (cloth or wire) [/ Tails / Wick / Gold]   lit
                 NiBillboardNode halo_bb, fizz<k>_bb -> BSTriShape (BSEffectShaderProperty, additive)"""
import math, pathlib, shutil, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from throwdagger import Mesh, unit, grid_faces, tileable, height_to_normal, nif_shapes, rot, tool

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx'
MESHDIR = OUT / 'Meshes' / 'CurseOfAlduin' / 'alchemist'
TEXDIR = OUT / 'Textures' / 'CurseOfAlduin' / 'alchemist'
TEXREL = 'CurseOfAlduin\\alchemist\\'
WORK = ROOT / 'build' / 'flasks'
TOOLS = pathlib.Path('/home/claude/tools')
HELD = '--held' in sys.argv   # (Noah: "hold it in my left or right hand") the held copies: still, flask_<el>_held.nif
SPIN_TPS = 0.0001 if HELD else 2.0  # (spinwrap needs a rate: one turn in hours)

# element: liquid colour, liquid pattern, neck wrap, fizz flipbook (its bow), fizz tint, fizz colour scale
EL = {
    'fire':        ((1.00, 0.42, 0.06), 'embers',  'cloth', 'emberstring', '1,0.75,0.45', 1.7),
    'frost':       ((0.42, 0.80, 1.00), 'veins',   'wire',  'rimehorn',    '0.7,0.9,1',   1.5),
    'shock':       ((0.66, 0.38, 1.00), 'veins',   'wire',  'stormreach',  '0.8,0.6,1',   1.6),
    'poison':      ((0.42, 1.00, 0.10), 'bubbles', 'cloth', 'rimehorn',    '0.35,1,0.12', 2.2),
    'sun':         ((1.00, 0.80, 0.26), 'swirl',   'cloth', 'dawnspire',   '1,0.85,0.45', 1.4),
    'blood':       ((0.90, 0.05, 0.08), 'bubbles', 'cloth', 'thirstbow',   '1,0.3,0.3',   1.8),
    'philosopher': ((1.00, 0.76, 0.20), 'molten',  'gold',  'dawnspire',   '1,0.82,0.35', 1.8),
}
rng = np.random.default_rng(4242)


# ------------------------------------------------------------------ geometry
def lathe(prof, nc=48, flat=False, mod=None):
    """a surface of revolution round Y from (y, r) profile points (bottom to top); normals from the profile's tangent
    (outward), or - with mod(i, theta) -> radial factor, or flat facets - from the surface itself. UV: u round, v down
    the image from the top (image coordinates)"""
    prof = np.asarray(prof, float)
    n = len(prof)
    th = np.linspace(0, 2 * np.pi, nc + 1)
    Y, R = prof[:, 0], prof[:, 1]
    dy, dr = np.gradient(Y), np.gradient(R)
    pn = unit(np.stack([dy, -dr], -1))                        # (radial, y) outward
    fac = np.ones((n, nc + 1)) if mod is None else np.array([[mod(i, t) for t in th] for i in range(n)])
    fac[:, -1] = fac[:, 0]
    P = np.stack([R[:, None] * fac * np.cos(th), np.repeat(Y[:, None], nc + 1, 1), R[:, None] * fac * np.sin(th)], -1)
    N = np.stack([pn[:, :1] * np.cos(th), np.repeat(pn[:, 1:2], nc + 1, 1), pn[:, :1] * np.sin(th)], -1)
    if mod is not None:                                       # (the modulated surface's own normals, kept outward)
        dr_ = np.gradient(P, axis=0); dc = np.roll(P[:, :-1], -1, 1) - np.roll(P[:, :-1], 1, 1)
        dc = np.concatenate([dc, dc[:, :1]], 1)
        Nn = unit(np.cross(dr_, dc)); Nn *= np.sign((Nn * N).sum(-1, keepdims=True) + 1e-9)
        ok = np.linalg.norm(np.cross(dr_, dc), axis=-1) > 1e-9
        N[ok] = Nn[ok]
    s = np.concatenate([[0], np.cumsum(np.hypot(np.diff(Y), np.diff(R)))]); s /= max(s[-1], 1e-9)
    UV = np.stack(np.meshgrid(th / (2 * np.pi), 1 - s), -1)
    V, N, UV, F = P.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), np.array(grid_faces(n, nc + 1))
    return facets(V, UV, F) if flat else (V, N, UV, F)


def facets(V, UV, F):
    """every triangle its own corners and face normal (the cut crystal's flat facets)"""
    V2 = V[F].reshape(-1, 3); UV2 = UV[F].reshape(-1, 2)
    a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    fn = unit(np.cross(b - a, c - a))
    cen = (a + b + c) / 3; out = cen.copy(); out[:, 1] = 0
    out[np.linalg.norm(out, axis=1) < 1e-6] = [0, 1, 0]
    fn *= np.sign((fn * out).sum(1, keepdims=True) + 1e-9)
    return V2, np.repeat(fn, 3, 0), UV2, np.arange(len(V2)).reshape(-1, 3)


def tube(C, rt, nr=10, closed=False, u_len=1.0):
    """a round tube of radius rt (scalar or per point) along the centreline C (parallel-transported frame)"""
    C = np.asarray(C, float); n = len(C)
    rt = np.broadcast_to(np.asarray(rt, float), (n,))
    T = unit(np.gradient(C, axis=0)) if not closed else unit(np.roll(C, -1, 0) - np.roll(C, 1, 0))
    a = np.array([0, 1.0, 0]) if abs(T[0, 1]) < 0.9 else np.array([1.0, 0, 0])
    Nf = [unit(np.cross(T[0], a))]
    for i in range(1, n):
        v = Nf[-1] - T[i] * (Nf[-1] @ T[i]); Nf.append(unit(v))
    Nf = np.array(Nf); Bf = np.cross(T, Nf)
    ph = np.linspace(0, 2 * np.pi, nr + 1)
    ring = np.cos(ph)[None, :, None] * Nf[:, None] + np.sin(ph)[None, :, None] * Bf[:, None]
    P = C[:, None] + rt[:, None, None] * ring
    s = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))])
    UV = np.stack(np.meshgrid(ph / (2 * np.pi), s / u_len, indexing='xy'), -1)
    F = grid_faces(n, nr + 1)
    if closed:   # (join the last ring back to the first)
        F += [(a_, c_, b_) for a_, b_, c_ in []]
        L = (n - 1) * (nr + 1)
        for j in range(nr):
            a_, b_ = L + j, L + j + 1
            F += [(a_, j, b_), (b_, j, j + 1)]
    return P.reshape(-1, 3), ring.reshape(-1, 3), UV.reshape(-1, 2), F


def sphere(c, rad, nu=16, nv=10):
    rad = np.broadcast_to(np.asarray(rad, float), (3,))
    th = np.linspace(0, 2 * np.pi, nu + 1); ph = np.linspace(-np.pi / 2, np.pi / 2, nv + 1)
    T, Pp = np.meshgrid(th, ph, indexing='ij')
    d = np.stack([np.cos(Pp) * np.cos(T), np.sin(Pp), np.cos(Pp) * np.sin(T)], -1)
    V = np.asarray(c) + d * rad; N = unit(d / rad)
    UV = np.stack([T / (2 * np.pi), 0.5 - Pp / np.pi], -1)
    return V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(nu + 1, nv + 1)


def ribbon(C, wdir, w, twist=0.0):
    """a flat cloth strip along C, w wide (per point) across wdir turned by twist (radians, along the strip)"""
    C = np.asarray(C, float); n = len(C)
    T = unit(np.gradient(C, axis=0))
    W0 = unit(wdir - T * (T @ wdir)[:, None])
    ang = np.linspace(0, twist, n)[:, None]
    B = np.cross(T, W0)
    Wv = W0 * np.cos(ang) + B * np.sin(ang)
    w = np.broadcast_to(np.asarray(w, float), (n,))[:, None]
    V = np.stack([C - Wv * w / 2, C + Wv * w / 2], 1)
    Nn = unit(np.cross(T, Wv))
    N = np.repeat(Nn[:, None], 2, 1)
    s = np.linspace(0, 1, n)
    UV = np.stack([np.tile([0.0, 1.0], (n, 1)), np.repeat(s[:, None], 2, 1)], -1)
    return V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(n, 2)


def belly_profile(c, R, a0, a1, n=30):
    a = np.radians(np.linspace(a0, a1, n))
    return [(c + R * math.sin(t), R * math.cos(t)) for t in a]


def flask_mesh(el):
    """the flask, base at y=0; returns the mesh and the fizz / halo anchors (before centring)"""
    phil = el == 'philosopher'
    S = 1.3 if phil else 1.0
    M = Mesh()
    c, R = 5.9 * S, 5.7 * S
    # --- glass: a slight push-up under the foot, the round belly, the shoulder, the neck, a rolled lip
    prof = [(0.62 * S, 0.0), (0.5 * S, 0.9 * S)] + belly_profile(c, R, -72, 60)
    prof += [(y * S, r * S) for y, r in ((11.55, 2.05), (12.05, 1.62), (12.45, 1.47), (13.5, 1.45), (14.6, 1.45),
                                         (15.4, 1.47), (15.65, 1.78), (15.95, 1.96), (16.3, 1.96), (16.5, 1.72), (16.56, 1.42))]
    M.add('Glass', 'crystal' if phil else 'glass', *lathe(prof, 14 if phil else 56, flat=phil))
    # --- the liquid: the belly's inside, filled to the level, a flat top (the cap its own surface: a hard edge)
    Ri = R - 0.42 * S
    fill = c + Ri * (0.86 if phil else 0.66)
    a1 = math.degrees(math.asin((fill - c) / Ri))
    lp = [(0.95 * S, 0.0)] + belly_profile(c, Ri, -74, a1, 28)
    M.add('Liquid', 'liquid', *lathe(lp, 40))
    M.add('Liquid', 'liquid', *lathe([(fill, Ri * math.cos(math.radians(a1))), (fill + 0.02, Ri * 0.5 * math.cos(math.radians(a1))), (fill + 0.03, 0.0)], 40))
    top = 16.56 * S
    anchors = {'belly': (0.0, c, 0.0), 'belly_r': R}
    if not phil:
        # --- the cork: down the neck, a fat plug above the lip, a rounded crown
        ck = [(14.4, 0.0), (14.4, 1.3), (16.4, 1.38), (16.52, 1.62), (17.9, 1.72), (18.15, 1.64), (18.3, 1.35), (18.34, 0.0)]
        M.add('Cork', 'cork', *lathe(ck, 32))
        top = 18.34
        anchors['fizz'] = (0.0, 18.9, 0.0)
        wrap = EL[el][2]
        if wrap == 'cloth':
            # a rag bound round the neck: lumpy folds, a knot at +X and two tails streaming out over the shoulder
            ph = rng.uniform(0, 6.28, 3)
            def folds(i, t, n=14):
                return 1 + 0.06 * math.sin(7 * t + ph[0]) + 0.035 * math.sin(13 * t + ph[1] + i * 0.4) + 0.03 * math.sin(3 * t + ph[2])
            yb = np.linspace(12.55, 14.75, 14)
            rb = 1.48 + 0.24 * np.sin(np.pi * (yb - yb[0]) / (yb[-1] - yb[0])) ** 0.6
            M.add('Wrap', 'cloth', *lathe(np.stack([yb, rb], 1), 48, mod=folds))
            M.add('Wrap', 'cloth', *sphere((1.86, 13.7, 0.0), (0.55, 0.62, 0.62), 14, 10))
            for side, tw in ((1, 0.9), (-1, -0.7)):
                t = np.linspace(0, 1, 14)[:, None]
                C = np.hstack([2.15 + 3.1 * t + 0.3 * t ** 2, 13.55 - 2.4 * t - 0.5 * t ** 2, side * (0.25 + 0.9 * t) + 0.2 * np.sin(5 * t)])
                M.add('Tails', 'cloth2', *ribbon(C, np.array([0, 0, 1.0]) + np.array([0, side * 0.6, 0]), 0.95 - 0.35 * t[:, 0], tw))
            if el == 'fire':
                # the wick: the rag's end stuffed through the cork, a frayed twist standing out of it, burning
                t = np.linspace(0, 1, 12)[:, None]
                C = np.hstack([0.25 * t ** 2, 17.6 + 2.0 * t, -0.15 * t])
                M.add('Wick', 'cloth', *tube(C, 0.36 - 0.12 * t[:, 0], 10))
                M.add('Wick', 'cloth', *sphere((0.25, 19.62, -0.15), (0.26, 0.2, 0.26), 10, 6))
                top = 19.7
                anchors['fizz'] = (0.25, 20.3, -0.15)
        else:
            # copper wire coiled up the neck, a twisted loop standing off it to hang it by
            t = np.linspace(0, 1, 260)
            turns = 4.5
            C = np.stack([1.6 * np.cos(2 * np.pi * turns * t), 12.55 + 2.55 * t, 1.6 * np.sin(2 * np.pi * turns * t)], 1)
            M.add('Wrap', 'copper', *tube(C, 0.14, 8, u_len=4.0))
            C = [(1.6 + 0.9 * math.sin(math.pi * k / 30) ** 0.8 + 0.0, 13.0 + 1.3 * k / 30, 0.25 * math.sin(2 * math.pi * k / 30)) for k in range(31)]
            M.add('Wrap', 'copper', *tube(C, 0.12, 8, u_len=4.0))
    else:
        # --- the Philosopher's gold: a domed cap over the lip with a beaded rim and a finial, a collar at the shoulder,
        # a girdle round the belly's widest with beads, six ribs caging the belly, a footring
        y0 = 15.1 * S
        cap = [(y0, 1.47 * S), (y0 + 0.05, 2.2 * S), (y0 + 0.8, 2.35 * S), (y0 + 1.0, 2.15 * S), (y0 + 2.2, 2.25 * S),
               (y0 + 2.45, 2.45 * S), (y0 + 2.7, 2.2 * S), (y0 + 3.4, 1.7 * S), (y0 + 3.9, 0.9 * S), (y0 + 4.15, 0.55 * S),
               (y0 + 4.3, 0.42 * S), (y0 + 4.45, 0.0)]
        M.add('Gold', 'gold', *lathe(cap, 40))
        fy = y0 + 4.4
        M.add('Gold', 'gold', *sphere((0, fy + 0.65, 0), 0.75, 16, 10))
        M.add('Gold', 'gold', *lathe([(fy + 1.25, 0.0), (fy + 1.3, 0.3), (fy + 1.75, 0.2), (fy + 2.35, 0.0)], 12))
        for k in range(10):   # the cap's bead rim
            a = 2 * np.pi * k / 10
            M.add('Gold', 'gold', *sphere((2.45 * S * math.cos(a), y0 + 2.45, 2.45 * S * math.sin(a)), 0.32, 8, 6))
        top = fy + 2.35
        anchors['fizz'] = (0.0, fy + 2.7, 0.0)
        # the shoulder collar
        M.add('Gold', 'gold', *lathe([(11.45 * S, 0.0), (11.4 * S, 2.5 * S), (11.75 * S, 2.6 * S), (12.25 * S, 1.75 * S), (12.6 * S, 1.6 * S), (12.7 * S, 0.0)], 40))
        # the girdle and its beads
        th = np.linspace(0, 2 * np.pi, 97)
        M.add('Gold', 'gold', *tube(np.stack([(R + 0.12) * np.cos(th), np.full_like(th, c), (R + 0.12) * np.sin(th)], 1), 0.32, 10, u_len=6))
        for k in range(14):
            a = 2 * np.pi * (k + 0.5) / 14
            M.add('Gold', 'gold', *sphere(((R + 0.35) * math.cos(a), c, (R + 0.35) * math.sin(a)), 0.42, 10, 8))
        # six ribs from the collar down over the belly to the foot
        for k in range(6):
            a = 2 * np.pi * k / 6 + np.pi / 6
            ang = np.radians(np.linspace(-70, 57, 40))
            rr = R + 0.18
            C = np.stack([rr * np.cos(ang) * math.cos(a), c + rr * np.sin(ang), rr * np.cos(ang) * math.sin(a)], 1)
            M.add('Gold', 'gold', *tube(C, 0.2, 8, u_len=6))
        M.add('Gold', 'gold', *lathe([(0.2 * S, 0.0), (0.0, 1.0 * S), (0.0, 2.25 * S), (0.45 * S, 2.45 * S), (0.95 * S, 2.25 * S), (1.0 * S, 1.6 * S), (0.9, 0.0)], 40))
    return M, anchors, top


# ------------------------------------------------------------------ textures
def dds(arr, name, alpha, size=None):
    TEXDIR.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    a = np.clip(arr, 0, 255).astype(np.uint8)
    tmp = WORK / (name + '.png'); Image.fromarray(a, 'RGBA' if alpha else 'RGB').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', f'dds:compression={"dxt5" if alpha else "dxt1"}', '-define', 'dds:mipmaps=9',
                    str(TEXDIR / name)], check=True)
    return TEXREL + name


def grid(H, W):
    v, u = np.mgrid[0:H, 0:W].astype(np.float32)
    return (u + 0.5) / W, (v + 0.5) / H


def flat_normal(H, W, spec):
    return np.dstack([np.full((H, W), 128), np.full((H, W), 128), np.full((H, W), 255), np.full((H, W), spec)])


def glass_textures(crystal=False):
    """the glass: near clear, a touch thicker (more opaque) at the foot and the lip, faint draw streaks and a few seed
    bubbles; its glow (tinted by each flask's emit colour) bright over the belly where the liquid lights it"""
    H = W = 256
    u, v = grid(H, W)
    s = 1 - v                                                 # 0 at the foot, 1 at the lip
    streak = tileable(H, W, 1.4, 31 + crystal, aniso=(1.0, 0.08))
    fine = tileable(H, W, 0.7, 33 + crystal)
    a = (0.2 if crystal else 0.27) + 0.06 * (streak - 0.5) + 0.35 * np.exp(-(s / 0.06) ** 2) + 0.25 * np.exp(-((1 - s) / 0.05) ** 2)
    tint = np.array([250, 238, 205]) if crystal else np.array([224, 238, 232])
    col = np.ones((H, W, 3)) * tint * (0.92 + 0.12 * fine[..., None])
    im = Image.new('L', (W, H), 0); d = ImageDraw.Draw(im)
    r = np.random.default_rng(7 + crystal)
    for _ in range(26):
        x, y, rr = r.uniform(0, W), r.uniform(H * 0.15, H * 0.95), r.uniform(1.0, 2.6)
        d.ellipse([x - rr, y - rr, x + rr, y + rr], outline=255)
    bub = np.asarray(im, np.float32) / 255
    a = np.clip(a + 0.4 * bub, 0, 1); col = col + 30 * bub[..., None]
    D = dds(np.dstack([col, a * 255]), f'{"crystal" if crystal else "glass"}_d.dds', True)
    nrm = height_to_normal(streak * 0.5 + fine * 0.1, 3.0)
    N = dds(np.dstack([nrm, np.full((H, W), 255)]), f'{"crystal" if crystal else "glass"}_n.dds', True)
    lit = np.clip((0.72 - s) / 0.2, 0, 1) * 0.75 + 0.25 + 0.15 * (fine - 0.5)
    G = dds(np.repeat(lit[..., None] * 255, 3, -1), f'{"crystal" if crystal else "glass"}_g.dds', False)
    return D, N, G


def liquid_textures(el):
    """the liquid: its colour churned by a warped noise; the glow the same, its hot streaks running toward white.
    embers (sparks), veins (bright cracks: ice, lightning), bubbles, swirl / molten (slow bright folds)"""
    col, pat = np.array(EL[el][0]), EL[el][1]
    H = W = 256
    u, v = grid(H, W)
    seed = sum(map(ord, el))
    n1 = tileable(H, W, 1.25, seed); n2 = tileable(H, W, 1.25, seed + 1); n3 = tileable(H, W, 0.9, seed + 2)
    iy = (np.arange(H)[:, None] + (n1 - 0.5) * 60).astype(int) % H
    ix = (np.arange(W)[None, :] + (n2 - 0.5) * 60).astype(int) % W
    sw = n3[iy, ix]                                           # the warped swirl, 0..1
    lum = 0.35 + 0.65 * sw
    hot = np.clip((sw - 0.62) * 3.0, 0, 1)
    if pat == 'veins':
        vein = np.exp(-((np.abs(np.sin(sw * 9.0 * np.pi))) / 0.08) ** 2)
        hot = np.maximum(hot, vein * 0.9)
    elif pat in ('bubbles', 'embers'):
        im = Image.new('L', (W, H), 0); d = ImageDraw.Draw(im)
        r = np.random.default_rng(seed)
        for _ in range(70 if pat == 'bubbles' else 120):
            x, y, rr = r.uniform(0, W), r.uniform(0, H), r.uniform(1.5, 5.5) if pat == 'bubbles' else r.uniform(0.6, 1.8)
            if pat == 'bubbles': d.ellipse([x - rr, y - rr, x + rr, y + rr], outline=255, width=1)
            else: d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=255)
        dots = np.asarray(im.filter(ImageFilter.GaussianBlur(0.7)), np.float32) / 255
        hot = np.maximum(hot, dots)
    elif pat == 'molten':
        hot = np.clip((sw - 0.45) * 2.2, 0, 1) ** 0.8
        lum = 0.6 + 0.4 * sw
    whiteish = col * 0.35 + 0.65
    g = col[None, None] * lum[..., None] * (1 - hot[..., None]) + whiteish[None, None] * hot[..., None]
    g *= 0.95 if el != 'blood' else 1.0
    D = dds(col[None, None] * (0.45 + 0.4 * lum[..., None]) * 255 + 60 * hot[..., None], f'liquid_{el}_d.dds', False)
    G = dds(g * 255, f'liquid_{el}_g.dds', False)
    return D, G


def liquid_normal():
    H = W = 128
    n = tileable(H, W, 1.6, 77)
    return dds(np.dstack([height_to_normal(n, 2.0), np.full((H, W), 200)]), 'liquid_n.dds', True)


def cork_textures():
    H = W = 256
    u, v = grid(H, W)
    n = tileable(H, W, 0.8, 51); m = tileable(H, W, 1.3, 52)
    pores = np.clip((0.3 - n) * 4, 0, 1)
    lum = (0.85 + 0.3 * m) * (1 - 0.55 * pores)
    col = np.stack([168 * lum, 124 * lum, 78 * lum], -1)
    char = np.exp(-(v / 0.18) ** 2)[..., None] * 0.35         # (the crown a little darkened by handling)
    D = dds(col * (1 - char), 'cork_d.dds', False)
    N = dds(np.dstack([height_to_normal(-pores * 1.2 + m * 0.3, 4.0), 30 + 20 * m]), 'cork_n.dds', True)
    return D, N


def cloth_textures():
    """coarse linen: a plain weave, frayed tones, oil and soot stains"""
    H = W = 256
    u, v = grid(H, W)
    f = 40
    weave = 0.5 + 0.25 * np.sin(2 * np.pi * u * f) * np.sign(np.sin(np.pi * v * f)) + 0.25 * np.sin(2 * np.pi * v * f) * np.sign(np.sin(np.pi * u * f))
    stain = tileable(H, W, 1.5, 61); grain = tileable(H, W, 0.6, 62)
    lum = (0.72 + 0.28 * weave) * (0.85 + 0.25 * grain)
    col = np.stack([200 * lum, 184 * lum, 150 * lum], -1)
    st = np.clip((stain - 0.55) * 3, 0, 1)[..., None]
    col = col * (1 - 0.45 * st) + np.array([70, 50, 30]) * 0.45 * st
    D = dds(col, 'cloth_d.dds', False)
    N = dds(np.dstack([height_to_normal(weave * 1.0 + grain * 0.3, 3.5), 25 + 20 * weave]), 'cloth_n.dds', True)
    return D, N


def metal_textures(name, base, engrave):
    H = W = 256
    u, v = grid(H, W)
    br = tileable(H, W, 1.3, 71 + engrave, aniso=(1.0, 0.06)); fine = tileable(H, W, 0.6, 73)
    h = br * 0.4 + fine * 0.1
    lum = 0.88 + 0.22 * (br - 0.5) * 2 + 0.06 * (fine - 0.5)
    if engrave:   # running scrollwork: an S-curve vine engraved round and round
        sc = np.sin(2 * np.pi * (u * 6 + 0.12 * np.sin(2 * np.pi * v * 3)))
        line = np.exp(-((np.abs(np.sin(np.pi * (v * 8 + 0.25 * sc)))) / 0.1) ** 2)
        lum = lum * (1 - 0.45 * line); h = h - line * 1.2
    col = np.array(base)[None, None] * lum[..., None]
    D = dds(col, f'{name}_d.dds', False)
    N = dds(np.dstack([height_to_normal(h, 5.0), 200 + 40 * fine]), f'{name}_n.dds', True)
    return D, N


def halo_texture():
    H = W = 128
    u, v = grid(H, W)
    r = np.hypot(u - 0.5, v - 0.5) * 2
    a = np.clip(np.exp(-(r / 0.45) ** 2) - 0.02, 0, 1) * (r < 1)
    return dds(np.dstack([np.full((H, W, 3), 255), a * 255]), 'halo.dds', True)


# ------------------------------------------------------------------ build
def rootname_exe():
    exe = WORK / 'rootname'
    if not exe.exists():
        src = WORK / 'rootname.cpp'
        src.write_text('#include "NifFile.hpp"\nusing namespace nifly;\nint main(int c,char**v){NifFile n;if(n.Load(v[1]))return 1;'
                       'n.GetRootNode()->name.get()=v[2];return n.Save(v[1]);}\n')
        subprocess.run(['g++', '-std=c++20', '-O1', f'-I{TOOLS}/nifly/include', f'-I{TOOLS}/nifly/external', str(src),
                        str(TOOLS / 'nifly' / 'build' / 'src' / 'libnifly.a'), '-o', str(exe)], check=True)
    return str(exe)


def fx_cards(el, anchors):
    """(name, texture, tint, scale, half-size, position, flipbook?, phase) for the halo and the neck's fizz"""
    col, _, _, bid, tint, sc = EL[el]
    phil = el == 'philosopher'
    fxtex = f'CurseOfAlduin\\bow\\{bid}_arrowfx.dds'
    halo = ','.join(f'{min(1.0, c):.2f}' for c in col)
    cards = [('halo', TEXREL + 'halo.dds', halo, 0.9 if phil else 0.6, anchors['belly_r'] * 1.9, anchors['belly'], False, 0.0)]
    fz = anchors['fizz']
    size = 6.5 if phil else (4.4 if el == 'fire' else 3.6)    # (the sprite fills about a third of its cell)
    cards.append(('fizz0', fxtex, '1,1,1' if el == 'fire' else tint, sc, size, fz, True, 0.0))
    cards.append(('fizz1', fxtex, tint, sc * 0.8, size * 1.3, (fz[0], fz[1] + 0.4, fz[2]), True, 1.3))
    return cards


def build(el, tex):
    phil = el == 'philosopher'
    M, anchors, top = flask_mesh(el)
    lo, hi = M.bounds()
    yc = (lo[1] + hi[1]) / 2                                  # the height centred; the axis stays on Y
    for g in M.groups: g[2][:, 1] -= yc
    anchors = {k: ((v[0], v[1] - yc, v[2]) if isinstance(v, tuple) else v) for k, v in anchors.items()}
    obj = WORK / f'flask_{el}.obj'; M.write_obj(obj)
    col = EL[el][0]
    ec = ','.join(f'{c:.2f}' for c in col)
    gD, gN, gG = tex['crystal'] if phil else tex['glass']
    lD, lG = tex['liquid'][el]
    job = [f'in {obj}', f'out {WORK / "static.nif"}', 'scale 1', 'origin 1', 'offset 0 0 0',
           f'mat {"crystal" if phil else "glass"} diffuse={gD} normal={gN} glow={gG} emit={ec} emitmult={0.7 if phil else 0.5} spec=2.4 gloss=200 alpha=blend doublesided=0',
           f'mat liquid diffuse={lD} normal={tex["liquid_n"]} glow={lG} emit=1,1,1 emitmult={2.6 if phil else 2.2} spec=1.2 gloss=80 alpha=0 doublesided=0',
           f'mat cork diffuse={tex["cork"][0]} normal={tex["cork"][1]} spec=0.2 gloss=12 alpha=0 doublesided=0',
           f'mat cloth diffuse={tex["cloth"][0]} normal={tex["cloth"][1]} spec=0.15 gloss=10 alpha=0 doublesided=0',
           f'mat cloth2 diffuse={tex["cloth"][0]} normal={tex["cloth"][1]} spec=0.15 gloss=10 alpha=0 doublesided=1',
           f'mat copper diffuse={tex["copper"][0]} normal={tex["copper"][1]} spec=1.4 gloss=70 alpha=0 doublesided=0',
           f'mat gold diffuse={tex["gold"][0]} normal={tex["gold"][1]} spec=2.0 gloss=110 alpha=0 doublesided=0']
    if el == 'fire':   # (the wick's tip charred and glowing: the cloth's own glow, a dim ember)
        pass
    jf = WORK / 'flask.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([str(TOOLS / 'modelconv'), str(jf)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('modelconv failed')
    spun = WORK / 'spun.nif'
    r = subprocess.run([tool('spinwrap'), str(WORK / 'static.nif'), str(spun), 'FlaskSpin', '1', '0', '0', str(SPIN_TPS), '24'], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('spinwrap failed')
    nif = MESHDIR / f'flask_{el}{"_held" if HELD else ""}.nif'
    MESHDIR.mkdir(parents=True, exist_ok=True)
    L = [f'base {spun}', f'out {nif}']
    cards = fx_cards(el, anchors)
    for name, t, tint, sc, hs, at, flip, phase in cards:
        extra = f' flip=8x8 fps=24 phase={phase:.2f}' if flip else ''
        L.append(f'shape {name} tex={t} color={tint},1 scale={sc} blend=add billboard=2 parent=FlaskSpin at={at[0]:.2f},{at[1]:.2f},{at[2]:.2f}{extra}')
        cu = 1 / 8 if flip else 1
        for X, Y, uu, vv in ((-hs, hs, 0, 0), (hs, hs, cu, 0), (hs, -hs, cu, cu), (-hs, -hs, 0, cu)):
            L.append(f'v {X:.2f} {Y:.2f} 0 {uu} {vv} 1 1 1 1')
        L += ['t 0 1 2', 't 0 2 3']
    (WORK / 'fx.job').write_text('\n'.join(L) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / 'fx.job')], capture_output=True, text=True)
    if r.returncode or 'no node' in r.stderr: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed')
    subprocess.run([rootname_exe(), str(nif), 'AlchemistFlask'], check=True)
    if HELD:  # (held, it is a weapon: without a Prn - its sheath node - Skyrim never hangs it in the hand; tools/modelconv/addprn.cpp)
        subprocess.run(['/home/claude/tools/addprn', str(nif), str(nif), 'WeaponDagger'], check=True)
    lo, hi = M.bounds()
    print(f'{nif.name}: {hi[1] - lo[1]:.1f} tall (Y), {hi[0] - lo[0]:.1f} x {hi[2] - lo[2]:.1f}; Y {lo[1]:.1f}..{hi[1]:.1f}')
    return cards


def textures():
    tex = {'glass': glass_textures(False), 'crystal': glass_textures(True), 'liquid': {el: liquid_textures(el) for el in EL},
           'liquid_n': liquid_normal(), 'cork': cork_textures(), 'cloth': cloth_textures(),
           'copper': metal_textures('copper', (190, 108, 62), False), 'gold': metal_textures('gold', (226, 176, 72), True)}
    halo_texture()
    return tex


# ------------------------------------------------------------------ preview
MATS = {   # shape name prefix -> (emit colour or None, emit mult, specular, glossy exponent, alpha blend)
    'Glass': ('el', 0.5, 0.9, 60, True), 'Liquid': ((1, 1, 1), 2.2, 0.3, 30, False), 'Cork': (None, 0, 0.05, 8, False),
    'Wrap': (None, 0, 0.1, 8, False), 'Tails': (None, 0, 0.05, 8, False), 'Wick': (None, 0, 0.05, 8, False),
    'Gold': (None, 0, 0.7, 40, False)}


def load_tex(rel, rgba=False):
    if not rel: return None
    rel = rel.replace('\\', '/')
    if rel.lower().startswith('textures/'): rel = rel[9:]
    p = OUT / 'Textures' / rel
    if not p.exists(): return None
    return np.asarray(Image.open(p).convert('RGBA' if rgba else 'RGB'), np.float32) / 255


def render(el, sh, cards, R, night, W=230, H=330, span=31.0):
    sc = H / span
    zb = np.full((H, W), -1e9); img = np.zeros((H, W, 3), np.float32)
    gz = np.full((H, W), -1e9); gcol = np.zeros((H, W, 3), np.float32); ga = np.zeros((H, W), np.float32)
    L = unit(np.array([0.35, 0.55, 0.75])); Hh = unit(L + np.array([0, 0, 1.0]))
    amb, dif_k = (0.06, 0.12) if night else (0.3, 0.7)
    rr = np.random.default_rng(2)
    elc = np.array(EL[el][0])
    for meta, V, F in sh:
        kind = meta.get('name', '').split(':')[0]
        if kind not in MATS: continue
        emit, emult, spk, gl, blend = MATS[kind]
        if kind == 'Liquid' and el == 'philosopher': emult = 2.6
        P = V[:, :3] @ R.T
        T = P[F]; UV = V[F][:, :, 3:5]
        nrm = unit(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]))
        d = load_tex(meta.get('tex0'), rgba=blend); g = load_tex(meta.get('tex2'))
        e1 = T[:, 1, :2] - T[:, 0, :2]; e2 = T[:, 2, :2] - T[:, 0, :2]
        area = np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0]) * sc * sc / 2
        n = np.clip(area * 4, 3, 4000).astype(int)
        rep = np.repeat(np.arange(len(F)), n)
        r = rr.random((len(rep), 2)); m = r.sum(1) > 1; r[m] = 1 - r[m]
        Tp = T[rep]; Up = UV[rep]
        pts = Tp[:, 0] + r[:, :1] * (Tp[:, 1] - Tp[:, 0]) + r[:, 1:] * (Tp[:, 2] - Tp[:, 0])
        uv = Up[:, 0] + r[:, :1] * (Up[:, 1] - Up[:, 0]) + r[:, 1:] * (Up[:, 2] - Up[:, 0])
        px = (W / 2 + pts[:, 0] * sc).astype(int); py = (H / 2 - pts[:, 1] * sc).astype(int)
        ok = (px >= 0) & (px < W) & (py >= 0) & (py < H)
        px, py, pz, uv, rep = px[ok], py[ok], pts[ok, 2], uv[ok], rep[ok]
        def samp(t):
            hh, ww = t.shape[:2]
            return t[(np.mod(uv[:, 1], 1) * (hh - 1)).astype(int), (np.mod(uv[:, 0], 1) * (ww - 1)).astype(int)]
        tc = samp(d) if d is not None else np.full((len(px), 4 if blend else 3), 0.6)
        nn = nrm[rep] * np.sign(nrm[rep][:, 2:3] + 1e-9)
        lit = amb + dif_k * np.clip(nn @ L, 0, 1)
        spec = np.clip(nn @ Hh, 0, 1) ** gl * spk * (0.35 if night else 1.0)
        col = tc[:, :3] * lit[:, None] + spec[:, None]
        if g is not None and emit is not None:
            ec = elc if emit == 'el' else np.array(emit)
            col = col + samp(g) * ec * emult * (0.55 if not night else 0.75)
        o = np.argsort(pz); px, py, pz, col = px[o], py[o], pz[o], col[o]
        if blend:
            a = tc[o, 3]
            upd = pz > gz[py, px]
            gz[py[upd], px[upd]] = pz[upd]; gcol[py[upd], px[upd]] = col[upd]; ga[py[upd], px[upd]] = a[upd]
            continue
        upd = pz > zb[py, px]
        zb[py[upd], px[upd]] = pz[upd]; img[py[upd], px[upd]] = col[upd]
    bg = np.array([0.02, 0.02, 0.035]) if night else np.array([0.11, 0.11, 0.13])
    img[zb < -1e8] = bg
    # the additive cards (each facing the camera, hidden where something opaque stands in front)
    for name, t, tint, csc, hs, at, flip, phase in cards:
        c = np.asarray(at) @ R.T
        tx = load_tex(t, rgba=True)
        if tx is None: continue
        if flip:
            k = int(phase * 24 + 5) % 64; cw = tx.shape[1] // 8
            tx = tx[(k // 8) * cw:(k // 8 + 1) * cw, (k % 8) * cw:(k % 8 + 1) * cw]
        s = max(2, int(2 * hs * sc))
        spr = np.asarray(Image.fromarray((tx * 255).astype(np.uint8), 'RGBA').resize((s, s), Image.BILINEAR), np.float32) / 255
        add = spr[..., :3] * spr[..., 3:] * np.array([float(x) for x in tint.split(',')]) * csc * (0.8 if night else 0.45)
        x0 = int(W / 2 + c[0] * sc - s / 2); y0 = int(H / 2 - c[1] * sc - s / 2)
        xa, ya, xb, yb = max(0, x0), max(0, y0), min(W, x0 + s), min(H, y0 + s)
        if xa >= xb or ya >= yb: continue
        vis = (zb[ya:yb, xa:xb] < c[2])[..., None]
        img[ya:yb, xa:xb] += add[ya - y0:yb - y0, xa - x0:xb - x0] * vis
    # the glass over everything behind it
    m = gz > zb
    a = ga[..., None] * m[..., None]
    img = img * (1 - a) + gcol * a
    return np.clip(img, 0, 1)


def preview(path, cards_by_el):
    W, H = 230, 330
    els = list(EL)
    out = Image.new('RGB', (W * len(els), 2 * H + 46), (20, 20, 24))
    dr = ImageDraw.Draw(out)
    for i, el in enumerate(els):
        nif = MESHDIR / f'flask_{el}.nif'
        sh = nif_shapes(nif)
        V = np.vstack([v[:, :3] for m, v, f in sh if m.get('name', '').split(':')[0] in MATS])
        lo, hi = V.min(0), V.max(0)
        day = render(el, sh, cards_by_el[el], rot('x', 12) @ rot('y', -30), False, W, H)
        night = render(el, sh, cards_by_el[el], rot('y', -30) @ rot('x', 65), True, W, H)
        out.paste(Image.fromarray((day * 255).astype(np.uint8)), (i * W, 0))
        out.paste(Image.fromarray((night * 255).astype(np.uint8)), (i * W, H + 16))
        dr.text((i * W + 6, 6), f'flask_{el}', fill=(235, 235, 235))
        dr.text((i * W + 6, 20), f'{hi[1] - lo[1]:.1f} tall  Y {lo[1]:.1f}..{hi[1]:.1f}', fill=(170, 170, 170))
        dr.text((i * W + 6, H + 20), 'night, tumbled 65deg about X', fill=(150, 150, 150))
    dr.text((8, 2 * H + 24), f'top row: day, upright (+Y = flight / neck, up the page)   bottom: night (emissive), mid-tumble   '
            f'spin {SPIN_TPS:g} turns/s about X', fill=(200, 200, 200))
    out.save(path)
    print('preview', path)


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    tex = textures()
    cards = {el: build(el, tex) for el in EL}
    if '--preview' in sys.argv:
        preview(sys.argv[sys.argv.index('--preview') + 1], cards)
    shutil.rmtree(WORK, ignore_errors=True)   # (disk is tight: the OBJs, PNGs, jobs and the static NIFs go)
