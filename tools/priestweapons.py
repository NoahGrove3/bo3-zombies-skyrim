"""(Noah: the Dragon Priests' weapons - docs/DRAGON_PRIEST_WEAPONS.md - "Our own models with 2k textures and hyper realistic
detail and effects. I want each weapon to feel and play as good as they look.") The ten weapons, modelled here from scratch
(no one else's mesh), each with its own 2K textures and its own effects living on it.

As Skyrim's weapons are: the grip at the origin, the head (blade, ball, hammer) up +Y; Prn by kind (WeaponSword,
WeaponBack, WeaponAxe/Mace, WeaponDagger, WeaponStaff). Built along Z (the head up) and turned at the end.

    python3 tools/priestweapons.py [names...] [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/priests/<name>.nif
         assets/fx/Textures/CurseOfAlduin/priests/<name>_*.dds"""
import math, pathlib, shutil, subprocess, sys
import numpy as np
from scipy.ndimage import gaussian_filter
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gauntlet as g
import mutagenstaff as ms
from throwdagger import Mesh, unit, grid_faces, tileable, height_to_normal, tool, rename_root

ROOT = g.ROOT
MESHDIR = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'priests'
g.TEXDIR = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'priests'
g.TEXREL = 'CurseOfAlduin\\priests\\'
WORK = ROOT / 'build' / 'priests'
g.WORK = WORK
TOOLS = g.TOOLS
FX = {k: 'CurseOfAlduin\\alchemist\\gauntlet_fx_' + k + '.dds' for k in ('core', 'glow', 'swirl', 'motes', 'pulse')}
rng = np.random.default_rng(777)


# ================================================================== shapes
def blade(M, mat, y0, length, width, thick, tip=0.22, fuller=0.0, jag=0.0, seed=0, uv=(0, 1, 0, 1), name='Blade', curve=0.0, ns=120, nx=17, runes=None, single=0.0):
    """a blade along +Z from z=y0: its half width w(s) (s 0 at the guard, 1 at the point), a lens cross-section thinning to
    sharp edges, an optional fuller groove down its middle (0..1 of its length), optional crystal jags along the edges.
    UV: u across (0..1 left to right), v along (0 at the guard), into the given window of the sheet. single (0..1): a
    single-edged blade instead - a thick spine at -x, a flat slab thinning toward +x, then a ground bevel (this fraction
    of the width) down to a keen edge, and a sharp point"""
    if runes: nx = max(nx, 29)
    if single: nx = max(nx, 41)
    r = np.random.default_rng(seed)
    S = np.linspace(0, 1, ns)
    jagged = np.zeros(ns)
    if jag:
        k = 0
        while k < ns:
            run = r.integers(5, 12)
            amp = jag * (0.4 + 0.6 * r.random())
            for q in range(min(run, ns - k)):
                jagged[k + q] = amp * (q / run)   # (a sawtooth: each crystal widening to a step)
            k += run
    def half(s):
        body = width * 0.5 * (1.0 - 0.18 * s)
        pt = np.clip((1.0 - s) / tip, 0, 1) if tip > 0 else 1.0
        if single: return body * np.clip(pt, 0, 1) ** 0.72   # (a keen point, not a rounded one)
        return body * np.sqrt(np.clip(pt * (2 - pt), 0, 1))
    X = np.linspace(-1, 1, nx)
    V = np.zeros((2, ns, nx, 3)); N = np.zeros_like(V); UV = np.zeros((2, ns, nx, 2))
    for i, s in enumerate(S):
        hw = half(s) + jagged[i] * (1 - s) ** 0.3
        t = thick * (1.0 - 0.45 * s) * 0.5
        for j, x in enumerate(X):
            prof = (1 - abs(x) ** 2.2) ** 0.75
            if single:   # (spine -> slab -> the bevel's shoulder -> the edge)
                tt = (x + 1) / 2; b = single
                prof = (1 - 0.3 * tt / (1 - b)) if tt < 1 - b else 0.7 * ((1 - tt) / b) ** 1.15
                prof = prof * math.sqrt(min(1.0, tt / 0.05 + 0.25)) + 0.02 * (1 - tt)
            fz = 0.0
            if fuller and s < fuller:
                fw = runes[1] * 1.15 if runes else 0.18   # (with script: a broad flat channel the letters are cut into)
                fz = (0.3 if runes else 0.38) * t * math.exp(-(x / fw) ** (6 if runes else 2)) * min(1.0, (fuller - s) * 8) * min(1.0, s * 12)
            z = t * prof - fz
            cx = curve * s ** 2 * length
            for side, sg in ((0, 1), (1, -1)):
                V[side, i, j] = (x * hw + cx, sg * z, y0 + s * length)
                UV[side, i, j] = (uv[0] + (uv[1] - uv[0]) * (x * 0.5 + 0.5), uv[2] + (uv[3] - uv[2]) * s)
    for side in range(2):
        dx = np.gradient(V[side], axis=1); ds = np.gradient(V[side], axis=0)
        n = unit(np.cross(dx, ds))
        if side == 0: n = n * np.where((n[..., 1:2] < 0), -1, 1)
        else: n = n * np.where((n[..., 1:2] > 0), -1, 1)
        N[side] = n
    if runes:   # (Noah: "etched into the blade") the script's strip: the same surface, its middle third, lifted a hair
        rmat, rw, s0, s1, cell = runes[:5]   # (material, its half width as a fraction of the blade's, along s0..s1, a glyph's length in units)
        rc = runes[5] if len(runes) > 5 else 0.0   # (its centre across the blade)
        js = [j for j, x in enumerate(X) if abs(x - rc) <= rw + 1e-6]
        i0 = int(s0 * (ns - 1)); i1 = int(s1 * (ns - 1))
        for side in range(2):
            Vs = V[side, i0:i1 + 1][:, js] + N[side, i0:i1 + 1][:, js] * 0.012
            Ns = N[side, i0:i1 + 1][:, js]
            uu = (X[js] - X[js][0]) / (X[js][-1] - X[js][0])
            if side == 1: uu = 1 - uu
            vv = (S[i0:i1 + 1] - S[i0]) * length / cell
            UVs = np.stack(np.meshgrid(uu, vv), -1)
            F = np.array(grid_faces(Vs.shape[0], Vs.shape[1]))
            P = Vs.reshape(-1, 3); Nn = Ns.reshape(-1, 3)
            fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
            flip = (fn * Nn[F].mean(1)).sum(1) < 0
            F[flip] = F[flip][:, [0, 2, 1]]
            M.add('Runes', rmat, P, Nn, UVs.reshape(-1, 2), F)
    # (Noah: "I can also see in between the 2 blade faces ... it should look and feel like a real dagger") the two faces
    # closed where they meet: a strip down each edge (the spine of a single-edged blade is a real back), over the point and
    # across the base
    ring = [(i, 0) for i in range(ns)] + [(ns - 1, j) for j in range(1, nx)] + [(i, nx - 1) for i in range(ns - 2, -1, -1)] + [(0, j) for j in range(nx - 2, 0, -1)] + [(0, 0)]
    P0 = np.array([V[0, i, j] for i, j in ring]); P1 = np.array([V[1, i, j] for i, j in ring])
    U0 = np.array([UV[0, i, j] for i, j in ring])
    Pe = np.concatenate([P0, P1]); Ue = np.concatenate([U0, U0])
    out = Pe.copy(); out[:, 1] = 0
    mid = np.array([[np.interp(p[2], V[0, :, nx // 2, 2], V[0, :, nx // 2, 0]), 0, p[2]] for p in Pe])
    Ne = unit(out - mid + 1e-6)
    m = len(ring); Fe = []
    for k in range(m - 1):
        a0, b0, c0, d0 = k, k + 1, m + k, m + k + 1
        Fe += [(a0, b0, c0), (b0, d0, c0)]
    Fe = np.array(Fe)
    fn = np.cross(Pe[Fe[:, 1]] - Pe[Fe[:, 0]], Pe[Fe[:, 2]] - Pe[Fe[:, 0]])
    flip = (fn * Ne[Fe].mean(1)).sum(1) < 0
    Fe[flip] = Fe[flip][:, [0, 2, 1]]
    M.add(name, mat, Pe, Ne, Ue, Fe)
    for side in range(2):
        F = np.array(grid_faces(ns, nx))
        if side == 1: F = F[:, [0, 2, 1]]
        P = V[side].reshape(-1, 3); Nn = N[side].reshape(-1, 3); U = UV[side].reshape(-1, 2)
        # (the two faces wound so each faces out)
        cen = P[F].mean(1); fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
        flip = (fn * Nn[F].mean(1)).sum(1) < 0
        F[flip] = F[flip][:, [0, 2, 1]]
        M.add(name, mat, P, Nn, U, F)


def lathe(prof, nc=32, cx=0.0, cy=0.0, uvs=None):
    """a turned part about Z; uvs: (u scale, v scale, u offset, v offset) - default the metal sheet's true scale"""
    prof = np.asarray(prof, float)
    P, N, UV, F = g.lathe_z(prof, cx, cy, nc, 0, 1, 0, 1)
    if uvs is None:
        circ = 2 * np.pi * max(0.2, float(prof[:, 1].max()))
        L = np.concatenate([[0], np.cumsum(np.hypot(np.diff(prof[:, 0]), np.diff(prof[:, 1])))])
        UV = UV * [circ / TILE, max(L[-1], 0.1) / TILE] + rng.random(2)
    else:
        UV = UV * [uvs[0], uvs[1]] + [uvs[2], uvs[3]]
    rad = P[:, :2] - [cx, cy]
    if ((N[:, :2] * rad).sum(1)).mean() < 0:   # (a profile drawn downward: its normals turned inward - turned out)
        N = -N; F = np.asarray(F)[:, [0, 2, 1]]
    return P, N, UV, F


TILE = 6.0


def rod(path, r, nseg=10):
    P, N, UV, F = g.tube(np.asarray(path, float), r, nseg)
    Ln = np.linalg.norm(np.diff(np.asarray(path, float), axis=0), axis=1).sum()
    rr = float(np.max(r)) if np.ndim(r) else float(r)
    UV = UV * [2 * np.pi * rr / TILE, Ln / TILE] + rng.random(2)
    return P, N, UV, F


def grip_wrap(M, mat, z0, z1, r, pitch=1.0, uv=(0.0, 1.0, 0.0, 1.0)):
    """a leather strap wound up a handle in a helix, its edges raised; UV into the window (the leather band's)"""
    nr, nc = max(40, int((z1 - z0) / pitch * 10)), 28
    zz = np.linspace(z0, z1, nr)
    th = np.linspace(0, 2 * np.pi, nc + 1)
    Z, T = np.meshgrid(zz, th, indexing='ij')
    ph = T / (2 * np.pi) + (Z - z0) / pitch
    ridge = np.abs(np.sin(np.pi * ph)) ** 0.35
    rad = r + 0.06 * ridge
    V = np.stack([rad * np.cos(T), rad * np.sin(T), Z], -1)
    N = np.stack([np.cos(T), np.sin(T), np.zeros_like(T)], -1)
    UV = np.stack([uv[0] + (uv[1] - uv[0]) * T / (2 * np.pi), uv[2] + (uv[3] - uv[2]) * ((Z - z0) / pitch % 1.0)], -1)
    # (v: each turn of the strap its own pass down the band - a strap seen as one)
    UV[..., 1] = uv[2] + (uv[3] - uv[2]) * ph % 1.0 * 0 + uv[2] + (uv[3] - uv[2]) * (Z - z0) / (z1 - z0)
    M.add('Grip', mat, V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(nr, nc + 1))


def box(c, ex, ey, ez, ax=(1, 0, 0), ay=(0, 1, 0), az=(0, 0, 1)):
    ax, ay, az = (np.array(v, float) for v in (ax, ay, az))
    V, N, UV, F = [], [], [], []
    for axis, e, o1, e1, o2, e2 in ((ax, ex, ay, ey, az, ez), (ay, ey, az, ez, ax, ex), (az, ez, ax, ex, ay, ey)):
        for s in (1, -1):
            base = len(V); n = axis * s
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                V.append(c + n * e + o1 * e1 * a + o2 * e2 * b); N.append(n)
                UV.append(((a + 1) * e1 / TILE, (b + 1) * e2 / TILE))
            F += [(base, base + 1, base + 2), (base, base + 2, base + 3)] if s > 0 else [(base, base + 2, base + 1), (base, base + 3, base + 2)]
    UV = np.array(UV) + rng.random(2)
    return np.array(V), np.array(N), UV, F


def torus(c, R, r, axis, nu=24, nv=10, stretch=1.0, long=None):
    """a ring: centre c, round axis; stretch > 1 makes it a chain link's oval, long along `long`"""
    axis = unit(np.asarray(axis, float))
    if long is not None:
        a = unit(np.asarray(long, float) - axis * (np.asarray(long, float) @ axis))
    else:
        a = unit(np.cross(axis, [0.3, 0.2, 0.93]))
    b = np.cross(axis, a)
    V, N = [], []
    for i in range(nu + 1):
        th = 2 * np.pi * i / nu
        cen = c + (a * math.cos(th) * stretch * R + b * math.sin(th) * R)
        out = unit(a * math.cos(th) * stretch + b * math.sin(th) / 1.0)
        for j in range(nv + 1):
            ph = 2 * np.pi * j / nv
            n = out * math.cos(ph) + axis * math.sin(ph)
            V.append(cen + n * r); N.append(n)
    UV = np.stack(np.meshgrid(np.linspace(0, 2 * np.pi * R * stretch / TILE, nv + 1), np.linspace(0, 1, nu + 1)), -1).reshape(-1, 2) + rng.random(2)
    return np.array(V), np.array(N), UV, grid_faces(nu + 1, nv + 1)


def spikes(M, mat, c, R, n, length, base_r, seed=0):
    """spikes on a ball of radius R at c, spread evenly (a Fibonacci sphere), each a cone"""
    k = np.arange(n) + 0.5
    phi = np.arccos(1 - 2 * k / n); th = np.pi * (1 + 5 ** 0.5) * k
    for i in range(n):
        d = np.array([math.sin(phi[i]) * math.cos(th[i]), math.sin(phi[i]) * math.sin(th[i]), math.cos(phi[i])])
        L = length * (0.85 + 0.3 * np.random.default_rng(seed + i).random())
        P, Nn, UV, F = g.axis_lathe([(R * 0.85, base_r), (R + L * 0.3, base_r * 0.75), (R + L * 0.8, base_r * 0.25), (R + L, 0.0)], c, d, 10)
        M.add('Spike', mat, P, Nn, UV * [base_r * 6 / TILE, L / TILE] + rng.random(2), F)


# ================================================================== textures
def save(arr, name, alpha=False):
    return g.save(arr, name, alpha)


def metal_tex(name, base_rgb, rust=0.0, dark=0.0, etch=None, seed=0):
    """2048, tiling: worked metal - hammer marks, brushing, scratches, pitting; rust (0..1) blooming in the pits; dark (0..1)
    blackened oxide in the low places; optional etched pattern (an array in 0..1, tiling)"""
    S = 2048
    d1, d2 = g.cells(512, 700, 300 + seed)
    hammer = np.tile(np.clip(d1 / 16.0, 0, 1), (4, 4))
    brushed = tileable(S, S, 1.1, 301 + seed, aniso=(1.0, 0.05))
    fine = tileable(S, S, 0.5, 302 + seed)
    big = tileable(S, S, 2.3, 303 + seed)
    mid = tileable(S, S, 1.5, 304 + seed)
    sc = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(sc)
    rr = np.random.default_rng(305 + seed)
    for _ in range(2200):
        x, y = rr.random(2) * S; a = rr.random() * math.pi; l = 6 + rr.random() ** 2 * 110
        for ox in (-S, 0, S):
            for oy in (-S, 0, S):
                dr.line([(x + ox, y + oy), (x + ox + math.cos(a) * l, y + oy + math.sin(a) * l)], fill=int(50 + 150 * rr.random()), width=1)
    scr = np.asarray(sc, np.float32) / 255
    pits = np.clip((tileable(S, S, 0.35, 306 + seed) - 0.78) * 6, 0, 1)
    low = np.clip((0.45 - (0.5 * hammer + 0.5 * big)) * 3, 0, 1)
    dents = np.tile(np.clip(g.cells(256, 60, 307 + seed)[0] / 26.0, 0, 1), (8, 8))   # (big old dents and wear)
    grit = tileable(S, S, 0.3, 308 + seed)
    h = 0.45 * hammer + 0.5 * dents + 0.15 * brushed + 0.3 * fine + 0.25 * grit - 0.35 * scr - 0.8 * pits
    lum = 0.8 + 0.2 * (brushed - 0.5) + 0.25 * (big - 0.5) + 0.15 * hammer + 0.2 * scr
    col = np.array(base_rgb, np.float32) * lum[..., None]
    if dark:
        col = col * (1 - dark * 0.7 * low[..., None]) + np.array([20, 18, 18]) * dark * 0.7 * low[..., None]
    rmask = np.clip(rust * (pits * 1.4 + np.clip((mid - 0.6) * 3, 0, 1) * low), 0, 1)
    col = col * (1 - rmask[..., None]) + np.array([96, 50, 28]) * (0.7 + 0.5 * fine[..., None]) * rmask[..., None]
    if etch is not None:
        col = col * (1 - 0.55 * etch[..., None]) + np.array([30, 28, 26]) * 0.55 * etch[..., None]
        h = h - 0.9 * etch
    grime = np.clip(low * 0.8 + (1 - dents) * 0.3 + pits, 0, 1) * (0.6 + 0.4 * mid)
    col = col * (1 - 0.45 * grime[..., None]) + np.array([26, 22, 18]) * 0.45 * grime[..., None]
    col = col * (0.92 + 0.16 * (grit[..., None] - 0.5))
    D = save(col, name + '_d.dds')
    nrm = height_to_normal(gaussian_filter(h, 0.7), 11.0)
    # (Noah: "too smooth and shiny") a dull, worn sheen: only the rubbed high places and fresh scratches catch the light
    spec = np.clip(70 + 45 * (hammer - 0.5) + 70 * scr * (1 - grime) - 60 * grime - 60 * rmask - (50 * etch if etch is not None else 0), 0, 255)
    N = save(np.dstack([nrm, spec]), name + '_n.dds', True)
    return D, N


def leather_tex(name, base_rgb=(58, 34, 24), seed=0):
    """2048 tiling: worn leather strap - pebbled grain, creases, burnished where held, pale stitch holes"""
    S = 2048
    d1, d2 = g.cells(512, 2400, 400 + seed)
    peb = np.tile(np.clip((d2 - d1) / 6.0, 0, 1), (4, 4))
    fine = tileable(S, S, 0.5, 401 + seed)
    big = tileable(S, S, 2.0, 402 + seed)
    crease = tileable(S, S, 1.0, 403 + seed, aniso=(1.0, 0.2))
    cr = np.exp(-((crease - 0.5) / 0.012) ** 2)
    lum = 0.8 + 0.3 * (big - 0.5) + 0.12 * (fine - 0.5) + 0.12 * peb - 0.35 * cr
    col = np.array(base_rgb, np.float32) * lum[..., None]
    D = save(col, name + '_d.dds')
    nrm = height_to_normal(gaussian_filter(0.6 * peb + 0.35 * fine - 1.0 * cr, 0.5), 9.0)
    spec = np.clip(28 + 30 * peb + 25 * big - 25 * cr, 0, 255)
    N = save(np.dstack([nrm, spec]), name + '_n.dds', True)
    return D, N


# (Noah: "The dragon symbols ... need to be higher quality and etched into the blade") the Dragon tongue's letters as its
# cut stones show them: claw strokes - each a wedge, broad where the claw went in, tapering where it left - and dots,
# set in a box a little taller than wide. Each letter: strokes (x0, y0, x1, y1, broad end's width) and dots (x, y, r)
DOV = [
    ([(0.2, 0.05, 0.2, 1.15, 0.16), (0.2, 0.55, 0.85, 0.15, 0.13)], []),
    ([(0.15, 0.1, 0.85, 1.1, 0.15), (0.85, 0.1, 0.15, 1.1, 0.15)], [(0.5, 1.28, 0.08)]),
    ([(0.5, 0.05, 0.5, 1.15, 0.16), (0.15, 0.4, 0.5, 0.75, 0.11), (0.85, 0.4, 0.5, 0.75, 0.11)], []),
    ([(0.2, 0.1, 0.8, 0.1, 0.13), (0.8, 0.1, 0.2, 1.1, 0.15)], [(0.75, 0.75, 0.08)]),
    ([(0.25, 0.05, 0.25, 1.15, 0.15), (0.75, 0.05, 0.75, 1.15, 0.15)], [(0.5, 0.6, 0.08)]),
    ([(0.15, 0.15, 0.55, 1.1, 0.15), (0.55, 1.1, 0.9, 0.3, 0.12)], []),
    ([(0.5, 0.05, 0.15, 0.6, 0.14), (0.15, 0.6, 0.5, 1.15, 0.12), (0.6, 0.3, 0.9, 0.9, 0.11)], []),
    ([(0.2, 0.05, 0.2, 1.15, 0.15), (0.2, 0.1, 0.85, 0.4, 0.12), (0.2, 0.7, 0.85, 1.0, 0.12)], []),
    ([(0.15, 1.1, 0.5, 0.05, 0.15), (0.5, 0.05, 0.85, 1.1, 0.15)], [(0.5, 0.78, 0.08)]),
    ([(0.15, 0.05, 0.5, 0.55, 0.13), (0.85, 0.05, 0.5, 0.55, 0.13), (0.5, 0.55, 0.5, 1.15, 0.15)], []),
    ([(0.3, 0.05, 0.3, 1.15, 0.15), (0.3, 0.6, 0.85, 0.6, 0.12)], [(0.75, 0.25, 0.08), (0.75, 0.95, 0.08)]),
    ([(0.15, 0.2, 0.85, 0.2, 0.12), (0.5, 0.2, 0.5, 1.15, 0.15), (0.15, 0.75, 0.85, 0.75, 0.11)], []),
    ([(0.85, 0.05, 0.15, 0.6, 0.14), (0.15, 0.6, 0.85, 1.15, 0.14)], [(0.75, 0.6, 0.08)]),
    ([(0.15, 0.05, 0.15, 1.15, 0.14), (0.5, 0.05, 0.5, 1.15, 0.14), (0.85, 0.05, 0.85, 1.15, 0.14)], []),
    ([(0.2, 0.15, 0.8, 0.95, 0.15)], [(0.25, 0.95, 0.09), (0.75, 0.2, 0.09)]),
    ([(0.15, 0.1, 0.85, 0.5, 0.13), (0.85, 0.5, 0.15, 0.9, 0.13), (0.15, 0.9, 0.85, 1.15, 0.11)], []),
]


def glyph_strip(name, steel_rgb, glow_rgb=None, n=16, seed=0, W=256, cell_h=256, lit=None):
    """a strip of the Dragon tongue's letters (W wide, n letters, one under another), cut into steel: V-grooves with
    bright polished lips and dark grime in their beds, for blade(runes=...): its colour (with alpha: only the letters),
    its normal (the grooves' walls, spec in alpha) and, with glow_rgb, the letters' light"""
    from scipy.ndimage import distance_transform_edt
    H = cell_h * n
    SS = 4
    im = Image.new('L', (W * SS, H * SS), 0); dr = ImageDraw.Draw(im)
    r = np.random.default_rng(1300 + seed)
    order = r.permutation(len(DOV))
    for k in range(n):
        strokes, dots = DOV[order[k % len(DOV)]]
        flipx = r.random() < 0.3
        ox, oy, sc = W * SS * 0.13, (k * cell_h + cell_h * 0.06) * SS, W * SS * 0.74
        def P(x, y): return (ox + (1 - x if flipx else x) * sc, oy + y * sc * 0.62)
        for (x0, y0, x1, y1, w) in strokes:
            a = np.array(P(x0, y0)); b = np.array(P(x1, y1))
            d = b - a; L = np.linalg.norm(d) + 1e-6; d = d / L; nrm = np.array([-d[1], d[0]])
            w0 = w * sc * 0.8; w1 = w0 * 0.42   # (bold wedge strokes, chisel-cut: wide at the blow, narrowing)
            poly = [a + nrm * w0 - d * w0 * 0.3, b + nrm * w1, b + d * w0 * 0.5, b - nrm * w1, a - nrm * w0 - d * w0 * 0.3]
            dr.polygon([tuple(p) for p in poly], fill=255)
        for (x, y, rr) in dots:
            c = np.array(P(x, y)); R = rr * sc * 0.55
            dr.ellipse([c[0] - R, c[1] - R, c[0] + R, c[1] + R], fill=255)
    big = np.asarray(im, np.float32) / 255
    inside = big > 0.5
    depth = np.clip(distance_transform_edt(inside) / (SS * 9.0), 0, 1)        # (V-groove: deepest down the stroke's middle)
    out = np.clip(1 - distance_transform_edt(~inside) / (SS * 2.5), 0, 1)       # (the polished lip just outside)
    def down(a): return a.reshape(H, SS, W, SS).mean(axis=(1, 3))
    m, dep, lip = down(big), down(depth), down(out) * (1 - down(big))
    steel = np.array(steel_rgb, np.float32)
    col = steel[None, None, :] * (1 - 0.75 * dep[..., None]) + np.array([20, 18, 16]) * 0.0
    col = col * (1 - 0.4 * m[..., None]) + np.array([30, 26, 22]) * 0.4 * m[..., None]
    col = col + np.array([60, 60, 62]) * lip[..., None]
    if lit is not None:   # (letters filled with light: their own bright colour, not dark grime - seen even where the glow map is dim)
        col = col * (1 - m[..., None]) + np.array(lit, np.float32)[None, None, :] * m[..., None]
    alpha = np.clip(m * 1.05 + lip * 0.9, 0, 1) * 255
    D = save(np.dstack([col, alpha]), name + '_d.dds', True)
    h = -dep * 1.6 + lip * 0.25
    nrm = height_to_normal(gaussian_filter(h, 0.7), 9.0)
    spec = np.clip(60 + 160 * lip - 30 * m, 0, 255)
    Nn = save(np.dstack([nrm, spec]), name + '_n.dds', True)
    G = None
    if glow_rgb is not None:
        glow = np.clip(dep * 1.2 + gaussian_filter(m, 3.0) * 0.5, 0, 1)
        G = save(np.array(glow_rgb, np.float32) * glow[..., None], name + '_g.dds')
    return D, Nn, G


def script_mask(S, rows, seed, density=0.6, size=28, width=4):
    """dragon-script: rows of wedge-cut glyphs (three strokes each, the Dragon tongue's claw marks), 0..1"""
    im = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(im)
    r = np.random.default_rng(seed)
    for (u0, u1, v0, v1) in rows:
        x = u0 * S
        while x < u1 * S - size:
            if r.random() < density:
                cx, cy = x + size * 0.5, (v0 + v1) * 0.5 * S
                for k in range(r.integers(2, 4)):
                    a = r.choice([-1, 0, 1]) * 0.5 + r.random() * 0.2
                    ox = (k - 1) * size * 0.28
                    l = size * (0.5 + 0.35 * r.random())
                    dr.line([(cx + ox - math.sin(a) * l * 0.5, cy - math.cos(a) * l * 0.5), (cx + ox + math.sin(a) * l * 0.5, cy + math.cos(a) * l * 0.5)], fill=255, width=width)
                if r.random() < 0.4:
                    dr.ellipse([cx - 3, cy + size * 0.35, cx + 3, cy + size * 0.35 + 6], fill=255)
            x += size * 1.1
    return np.asarray(im.filter(ImageFilter.GaussianBlur(1.0)), np.float32) / 255


def blade_tex(name, steel_rgb, edge_rgb=None, glow_rgb=None, script=True, pattern='folded', seed=0, ice=False, mask=None, mask_rgb=None):
    """2048 blade sheet: u across (the edges at 0 and 1), v along (the guard at 0): the steel's own grain (folded layers
    or plain), a polished bevel at each edge, the fuller darker, a line of dragon-script etched down the middle; for ice:
    pale blue depths with cracks and frosted edges. Optional glow (the script and cracks lit)"""
    S = 2048
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    warp = tileable(S, S, 1.8, 500 + seed)
    fine = tileable(S, S, 0.5, 501 + seed)
    streak = tileable(S, S, 1.0, 502 + seed, aniso=(0.06, 1.0))
    edge = np.clip(1 - np.minimum(u, 1 - u) / 0.12, 0, 1) ** 1.5
    centre = np.exp(-((u - 0.5) / 0.07) ** 2)
    if ice:
        cracks = np.zeros((S, S), np.float32)
        d1, d2 = g.cells(S // 2, 220, 503 + seed)
        cell = np.kron(np.clip((d2 - d1) / 3.0, 0, 1), np.ones((2, 2)))
        cracks = np.clip(1 - cell * 2.5, 0, 1)
        depth = 0.55 + 0.35 * centre + 0.2 * (warp - 0.5)
        col = np.array([120, 175, 220], np.float32) * depth[..., None]
        col = col + np.array([200, 230, 255]) * (0.45 * edge + 0.5 * cracks)[..., None]
        frost = np.clip((fine - 0.45) * 3, 0, 1) * edge
        col = col * (1 - 0.4 * frost[..., None]) + np.array([235, 245, 255]) * 0.4 * frost[..., None]
        h = 0.3 * warp + 0.2 * fine - 0.5 * cracks + 0.5 * frost
        spec = np.clip(230 - 120 * frost, 0, 255)
        glow = np.clip(cracks * 0.8 + centre * 0.35 * (0.6 + 0.4 * warp), 0, 1)
    else:
        if pattern == 'mist':   # (fog in the steel: soft cloudy layers drifting along the blade)
            layers = np.clip(0.5 + 0.9 * (tileable(S, S, 1.6, 520 + seed, aniso=(0.35, 1.0)) - 0.5) + 0.4 * (warp - 0.5), 0, 1)
        elif pattern == 'folded':
            layers = np.sin((u * 14 + 1.1 * warp + 0.5 * streak + v * 3) * 2 * np.pi * 2) * 0.5 + 0.5
        else:
            layers = streak
        lum = 0.78 + (0.22 if pattern == 'mist' else 0.07) * (layers - 0.5) + 0.06 * (fine - 0.5) + 0.3 * edge - 0.14 * centre
        col = np.array(steel_rgb, np.float32) * lum[..., None]
        if edge_rgb is not None:
            col = col * (1 - 0.5 * edge[..., None]) + np.array(edge_rgb) * 0.5 * edge[..., None]
        h = 0.2 * layers + 0.15 * fine + 0.4 * edge
        spec = np.clip(125 + 80 * edge - 40 * centre + 20 * layers - 35 * np.clip((fine - 0.55) * 4, 0, 1), 0, 255)
        glow = np.zeros((S, S), np.float32)
    if script:
        m = script_mask(S, [(0.43, 0.57, 0.08 + 0.1 * k, 0.16 + 0.1 * k) for k in range(0)] , seed)
        # (down the blade's middle: glyphs set one under another along v)
        im = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(im)
        r = np.random.default_rng(510 + seed)
        y = S * 0.1
        while y < S * 0.7:   # (each glyph about a finger's width on the blade: three claw strokes and a dot)
            cx = S * 0.5
            n = r.integers(2, 4)
            for k in range(n):
                a = r.choice([-1, 0, 1]) * 0.5 + (r.random() - 0.5) * 0.25
                ox = (k - (n - 1) / 2) * 70
                l = 150 + 70 * r.random()
                dr.line([(cx + ox - math.sin(a) * l * 0.5, y - math.cos(a) * l * 0.5), (cx + ox + math.sin(a) * l * 0.5, y + math.cos(a) * l * 0.5)], fill=255, width=20)
            if r.random() < 0.5:
                dr.ellipse([cx - 14, y + 115, cx + 14, y + 143], fill=255)
            y += 260
        m = np.asarray(im.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
        col = col * (1 - 0.65 * m[..., None]) + np.array([24, 22, 22]) * 0.65 * m[..., None]
        h = h - 1.0 * m
        spec = spec - 100 * m
        if glow_rgb is not None: glow = np.maximum(glow, m)
    if mask is not None:   # (channels cut into the blade, filled with its light: poison, tears)
        col = col * (1 - 0.8 * mask[..., None]) + np.array(mask_rgb, np.float32) * 0.8 * mask[..., None]
        h = h - 0.8 * mask
        spec = spec - 60 * mask
        glow = np.maximum(glow, mask)
    D = save(col, name + '_d.dds')
    nrm = height_to_normal(gaussian_filter(h, 0.8), 6.0)
    N = save(np.dstack([nrm, np.clip(spec, 0, 255)]), name + '_n.dds', True)
    G = None
    if glow_rgb is not None:
        G = save(np.array(glow_rgb, np.float32) * gaussian_filter(glow, 1.5)[..., None] * 1.2, name + '_g.dds')
    return D, N, G


# ================================================================== the build
ROUGH = {'iron': 0.045, 'bronze': 0.04, 'leather': 0.03, 'steel': 0.04, 'silver': 0.03, 'oak': 0.05, 'wood': 0.05, 'bone': 0.035, 'stone': 0.07}


def roughen(M, seed=0):
    """(Noah: "their surfaces are too smooth") the metal and leather pushed in and out along their normals by a little
    smooth noise - forged and cast, not turned on a lathe"""
    r = np.random.default_rng(900 + seed)
    waves = [(unit(r.normal(size=3)) * f, r.random() * 6.28, a) for f, a in ((0.9, 1.0), (1.9, 0.55), (4.3, 0.3), (8.7, 0.15))]
    out = []
    for gname, m, V, N, UV, F in M.groups:
        amp = ROUGH.get(m, 0.0)
        if amp:
            V = np.asarray(V, float); N = np.asarray(N, float)
            n = sum(a * np.sin(V @ k + ph) for k, ph, a in waves) / 2.0
            V = V + N * (amp * n)[:, None]
        out.append((gname, m, V, N, UV, F))
    M.groups = out


# (Noah: "most all of the priest weapons sit awkward in the players hand either clipping into the hilt or the weapon being
# too small") each weapon sized and set in the hand as Skyrim's are (measured from Kv's crystal swords and the Light
# Greatswords: the grip top - the guard's underside - 4 units above the origin, grips about 1.2 thick): per weapon the
# top of its grip as built, and its scale
# (Noah: "the glacier greatsword's hilt still clips into my players right hand") its guard raised 3 units clear of the upper hand
FIT = {'zahkriisos_greatsword': (1.2, 1.3), 'nahkriin_mistblade': (1.5, 1.4), 'volsung_windcutter': (1.7, 1.6),
       'otar_stormblade': (8.0, 1.4), 'rahgot_dagger': (0.6, 1.75), 'krosis_griefblade': (1.5, 1.5),
       'hevnoraak_sealbreaker': (12.0, 1.15), 'morokei_maul': (12.0, 1.15)}


def fit_lines(L, top, sc, root):
    """the effects' lines moved as the model is: points in the weapon's own space (top-shifted and scaled), points in a
    node's or a card's own space only scaled"""
    out = []; own = False; card = False
    def full(x, y, z): return x * sc, y * sc, (z - top) * sc + 4.0
    for l in L:
        p = l.split(' ')
        if p[0] == 'shape':
            card = any(w.startswith('billboard=') and w != 'billboard=0' for w in p)
            own = any(w.startswith('parent=') for w in p)
            for i, w in enumerate(p):
                if w.startswith('at='):
                    x, y, z = full(*(float(q) for q in w[3:].split(',')))
                    p[i] = f'at={x:.3f},{y:.3f},{z:.3f}'
            out.append(' '.join(p)); continue
        if p[0] == 'node':
            x, y, z = float(p[3]), float(p[4]), float(p[5])
            x, y, z = full(x, y, z) if p[2] == root else (x * sc, y * sc, z * sc)
            p[3], p[4], p[5] = f'{x:.4f}', f'{y:.4f}', f'{z:.4f}'
            out.append(' '.join(p)); continue
        if p[0] == 'v' and len(p) >= 4:
            x, y, z = float(p[1]), float(p[2]), float(p[3])
            x, y, z = (x * sc, y * sc, z * sc) if (card or own) else full(x, y, z)
            p[1], p[2], p[3] = f'{x:.4f}', f'{y:.4f}', f'{z:.4f}'
            out.append(' '.join(p)); continue
        out.append(l)
    return out


def finish(M, name, mats, fx_lines=None, prn='WeaponSword', root=None):
    """join each material's parts, turn Z up to +Y, write it (modelconv), its effects (fxwrite), its Prn"""
    roughen(M, len(name))
    rootname = root or ''.join(w.capitalize() for w in name.split('_'))
    if name in FIT:
        top, sc = FIT[name]
        M.groups = [(gn, m, (np.asarray(V, float) - [0, 0, top]) * sc + [0, 0, 4.0], N, UV, F) for gn, m, V, N, UV, F in M.groups]
        if fx_lines: fx_lines = fit_lines(fx_lines, top, sc, rootname)
    merged = {}
    for gname, m, V, N, UV, F in M.groups:
        if m not in merged: merged[m] = [gname, m, [], [], [], [], 0]
        e = merged[m]; e[2].append(V); e[3].append(N); e[4].append(UV); e[5].append(np.asarray(F) + e[6]); e[6] += len(V)
    R = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)
    M.groups = [(m.capitalize(), m, np.vstack(e[2]) @ R.T, np.vstack(e[3]) @ R.T, np.vstack(e[4]), np.vstack(e[5])) for m, e in merged.items()]
    WORK.mkdir(parents=True, exist_ok=True)
    obj = WORK / f'{name}.obj'; M.write_obj(obj)
    static = WORK / f'{name}_static.nif'
    job = [f'in {obj}', f'out {static}', 'scale 1', 'ground 0', 'origin 1', 'offset 0 0 0'] + [f'mat {k} {v}' for k, v in mats.items()]
    (WORK / f'{name}.job').write_text('\n'.join(job) + '\n')
    r = subprocess.run([str(TOOLS / 'modelconv'), str(WORK / f'{name}.job')], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('modelconv failed')
    import throwdagger; throwdagger.WORK.mkdir(parents=True, exist_ok=True)
    rename_root(static, rootname)
    MESHDIR.mkdir(parents=True, exist_ok=True)
    nif = MESHDIR / f'{name}.nif'
    if fx_lines:
        L = [f'base {static}', f'out {nif}'] + ms.turn_y(fx_lines)
        (WORK / f'{name}_fx.job').write_text('\n'.join(g.fx_fix(L)) + '\n')
        r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / f'{name}_fx.job')], capture_output=True, text=True)
        if r.returncode or 'no node' in r.stderr: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed')
    else:
        shutil.copy(static, nif)
    subprocess.run([str(TOOLS / 'addprn'), str(nif), str(nif), prn], check=True)
    lo, hi = M.bounds()
    print(f'{nif.name}: x {lo[0]:.1f}..{hi[0]:.1f}  y {lo[1]:.1f}..{hi[1]:.1f}  z {lo[2]:.1f}..{hi[2]:.1f}; {sum(len(gg[5]) for gg in M.groups)} triangles')
    return obj


class Fx:
    """effect shapes (fxwrite lines), built along Z like the model"""
    def __init__(self): self.L = []
    def card(self, name, t, at, hs, col, sc, flip=None):
        extra = f' flip={flip[0]}x{flip[1]} fps={flip[2]} phase={flip[3]}' if flip else ''
        self.L.append(f'shape {name} tex={t} color={col} scale={sc} blend=add billboard=1 at={at[0]:.3f},{at[1]:.3f},{at[2]:.3f}{extra}')
        cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
        for X, Y, uu, vv in ((-hs, hs, 0, 0), (hs, hs, cu, 0), (hs, -hs, cu, cv), (-hs, -hs, 0, cv)):
            self.L.append(f'v {X:.3f} {Y:.3f} 0 {uu} {vv} 1 1 1 1')
        self.L.extend(['t 0 1 2', 't 0 2 3'])
    def mesh(self, opts, V, N, UV, F, alpha=None):
        self.L.append(f'shape {opts}')
        for i in range(len(V)):
            a = 1.0 if alpha is None else alpha[i]
            self.L.append(f'v {V[i][0]:.4f} {V[i][1]:.4f} {V[i][2]:.4f} {UV[i][0]:.4f} {UV[i][1]:.4f} 1 1 1 {a:.3f}')
            self.L.append(f'n {N[i][0]:.4f} {N[i][1]:.4f} {N[i][2]:.4f}')
        for a, b, c in F:
            self.L.append(f't {a} {b} {c}')
    def sheath(self, name, opts, z0, z1, r0, r1, nu=20, nv=12, uvz=1.0, fade=True):
        """an open sleeve along Z (a mist round a blade, a wind round a hammer), fading at both ends"""
        V, N, UV, A = [], [], [], []
        for i in range(nv + 1):
            s = i / nv; z = z0 + (z1 - z0) * s; r = r0 + (r1 - r0) * s
            for j in range(nu + 1):
                th = 2 * math.pi * j / nu
                V.append((r * math.cos(th), r * math.sin(th), z)); N.append((math.cos(th), math.sin(th), 0))
                UV.append((j / nu * 2, s * uvz)); A.append(math.sin(math.pi * s) ** 1.2 if fade else 1.0)
        F = []
        for i in range(nv):
            for j in range(nu):
                a = i * (nu + 1) + j; b = a + 1; c = a + nu + 1; d = c + 1
                F += [(a, c, b), (b, c, d)]
        self.mesh(f'{name} {opts}', V, N, UV, F, A)


# ================================================================== the weapons
def zahkriisos(texonly=False):
    """(Zahkriisos the Frost-Priest - "Glacier greatsword") a greatsword of glacier ice grown over an iron core: a broad
    blade with crystal jags along its edges, its depths glowing pale blue through the cracks; a guard of ice spreading
    like antlers; a long grip of dark leather bound with iron rings; an ice crystal for a pommel. Its effects: frost mist
    flowing up the blade, motes of frost circling it, a cold glow at the guard"""
    bD, bN, bG = blade_tex('zahk_blade', None, glow_rgb=(110, 190, 255), script=False, ice=True, seed=1)
    rD, rN, rG = glyph_strip('zahk_runes', (150, 196, 230), glow_rgb=(120, 200, 255), seed=1)
    iD, iN = metal_tex('zahk_iron', (92, 96, 104), rust=0.15, dark=0.6, seed=2)
    lD, lN = leather_tex('zahk_leather', (40, 36, 40), seed=3)
    M = Mesh()
    blade(M, 'ice', 6.5, 62.0, 7.6, 1.5, tip=0.16, fuller=0.0, jag=0.9, seed=11, runes=('runes', 0.26, 0.08, 0.72, 31.0))
    # the guard: ice crystals flaring out and up from a block, like antlers
    M.add('Guard', 'icesolid', *box(np.array([0, 0, 5.4]), 5.0, 1.3, 1.25))
    for side in (-1, 1):   # (crystal shards jutting from the block's faces)
        for k in range(4):
            c = np.array([(k - 1.5) * 2.2, side * 1.2, 5.4 + 0.3 * (k % 2)])
            P, N, UV, F = g.axis_lathe([(-0.3, 0.6), (1.1, 0.42), (2.6 + 0.6 * (k % 2), 0.0)], c, unit(np.array([0.25 * (k - 1.5), side * 0.55, 1.0])), 6)
            M.add('Guard', 'icesolid', P, N, UV * [0.3, 0.3] + [0.35, 0.5], F)
    for side in (-1, 1):
        for k, (ang, L, r) in enumerate(((0.35, 8.5, 0.75), (0.95, 6.0, 0.6), (-0.15, 5.0, 0.55))):
            d = np.array([side * math.cos(ang), 0.0, math.sin(ang) + 0.25])
            base = np.array([side * 4.2, 0.0, 5.6])
            P, N, UV, F = g.axis_lathe([(0, r), (L * 0.35, r * 0.8), (L * 0.8, r * 0.35), (L, 0.0)], base, unit(d), 6)
            M.add('Guard', 'icesolid', P, N, UV * [0.3, 0.3] + [0.35, 0.5], F)
    # the grip: dark leather, iron rings, an iron collar under the guard
    grip_wrap(M, 'leather', -11.0, 3.6, 1.12, 1.2)
    for z in (-11.4, -4.0, 3.9):
        M.add('Iron', 'iron', *lathe([(z - 0.4, 1.16), (z - 0.3, 1.36), (z + 0.3, 1.36), (z + 0.4, 1.16)], 24))
    M.add('Iron', 'iron', *lathe([(3.9, 1.25), (4.2, 1.7), (4.4, 1.8), (4.4, 0.0)], 24))
    # the pommel: a cluster of ice crystals in an iron cup
    M.add('Iron', 'iron', *lathe([(-11.4, 0.0), (-11.4, 1.2), (-12.4, 1.55), (-12.9, 1.4), (-12.9, 0.0)], 24))
    for k in range(5):
        a = 2 * math.pi * k / 5
        d = unit(np.array([math.cos(a) * 0.45, math.sin(a) * 0.45, -1.0]))
        P, N, UV, F = g.axis_lathe([(0, 0.55), (1.6, 0.45), (3.0 + 0.6 * (k % 2), 0.0)], np.array([0, 0, -12.6]), d, 6)
        M.add('Guard', 'icesolid', P, N, UV * [0.3, 0.3] + [0.35, 0.5], F)
    fx = Fx()
    fx.sheath('FrostMist', f'tex={FX["swirl"]} color=0.75,0.9,1,0.5 scale=1.3 blend=add falloff=1 uscroll=7 vscroll=3.5', 6.0, 70.0, 4.5, 2.5, 20, 14, 3.0)
    fx.sheath('FrostMotes', f'tex={FX["motes"]} color=0.7,0.9,1,0.8 scale=1.6 blend=add uscroll=5 vscroll=-9', 4.0, 66.0, 6.2, 4.0, 24, 10, 4.0)
    fx.card('GuardGlow', FX['glow'], (0, 0, 5.6), 7.0, '0.5,0.8,1,0.5', 1.0)
    fx.card('GuardCore', FX['core'], (0, 0, 5.6), 3.2, '0.6,0.85,1,1', 1.4, (4, 4, 6, 0))
    mats = {'ice': f'diffuse={bD} normal={bN} glow={bG} emit=0.6,0.85,1 emitmult=1.6 spec=2.6 gloss=160 alpha=0 doublesided=0',
            'runes': f'diffuse={rD} normal={rN} glow={rG} emit=0.6,0.85,1 emitmult=2.2 spec=1.5 gloss=80 alpha=blend doublesided=0',
            'icesolid': f'diffuse={bD} normal={bN} glow={bG} emit=0.6,0.85,1 emitmult=1.2 spec=2.4 gloss=150 alpha=0 doublesided=0',
            'iron': f'diffuse={iD} normal={iN} spec=0.55 gloss=24 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12 alpha=0 doublesided=0'}
    return finish(M, 'zahkriisos_greatsword', mats, fx.L, 'WeaponBack')


def place(M, src, R=np.eye(3), t=(0, 0, 0)):
    """another mesh's parts turned by R and moved by t, added to M"""
    t = np.asarray(t, float)
    for gname, m, V, N, UV, F in src.groups:
        M.add(gname, m, np.asarray(V) @ R.T + t, np.asarray(N) @ R.T, UV, F)


def rotm(axis, ang):
    axis = unit(np.asarray(axis, float)); x, y, z = axis; c, s_ = math.cos(ang), math.sin(ang); C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s_, x * z * C + y * s_], [y * x * C + z * s_, c + y * y * C, y * z * C - x * s_], [z * x * C - y * s_, z * y * C + x * s_, c + z * z * C]])




def mask_plate(c, fwd, up, w, h, depth, n=22):
    """a Dragon Priest's mask in relief: an oval plate bulged forward, a brow ridge across it, two eye slits sunk into it,
    a crest rising from the brow. Returns (V, N, UV, F) facing fwd"""
    fwd = unit(np.asarray(fwd, float)); up = unit(np.asarray(up, float) - fwd * (np.asarray(up, float) @ fwd)); rt = np.cross(up, fwd)
    V = np.zeros((n + 1, n + 1, 3)); UV = np.zeros((n + 1, n + 1, 2))
    for i in range(n + 1):
        for j in range(n + 1):
            x = -1 + 2 * j / n; y = -1 + 2 * i / n
            rr = x * x + y * y
            if rr > 1: x, y = x / math.sqrt(rr), y / math.sqrt(rr); rr = 1
            bulge = (1 - rr) ** 0.5
            brow = 0.45 * math.exp(-((y - 0.22 - 0.25 * abs(x)) / 0.1) ** 2) * (1 - x * x)
            eyes = -0.7 * math.exp(-(((abs(x) - 0.4) / 0.2) ** 2 + ((y - 0.02 - 0.12 * abs(x)) / 0.05) ** 2))
            ridge = 0.3 * math.exp(-((x / 0.07) ** 2)) * (y > 0.25)
            cheek = -0.15 * math.exp(-(((abs(x) - 0.45) / 0.12) ** 2 + ((y + 0.4) / 0.25) ** 2))
            z = depth * (bulge + brow + eyes + ridge + cheek)
            V[i, j] = c + rt * x * w + up * y * h + fwd * z
            UV[i, j] = (0.5 + 0.5 * x, 0.5 + 0.5 * y)
    dx = np.gradient(V, axis=1); dy = np.gradient(V, axis=0)
    N = unit(np.cross(dx, dy))
    N = N * np.where((N @ fwd)[..., None] < 0, -1, 1)
    F = np.array(grid_faces(n + 1, n + 1))
    fn = np.cross(V.reshape(-1, 3)[F[:, 1]] - V.reshape(-1, 3)[F[:, 0]], V.reshape(-1, 3)[F[:, 2]] - V.reshape(-1, 3)[F[:, 0]])
    flip = (fn @ fwd) < 0
    F[flip] = F[flip][:, [0, 2, 1]]
    return V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2) * 0.35 + rng.random(2), F


def knotwork(S, cells=8, band=0.11, seed=0):
    """a tiling Nordic knotwork: two families of diagonal bands woven over and under each other, their edges cut deep
    (0..1 the grooves)"""
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    a = (u + v) * cells; b = (u - v) * cells
    da = np.abs((a % 1.0) - 0.5); db = np.abs((b % 1.0) - 0.5)
    onA = da < band * 2; onB = db < band * 2
    over = ((np.floor(a) + np.floor(b)) % 2) == 0           # (which band passes over at each crossing)
    edgeA = np.exp(-((da - band * 2) / 0.012) ** 2); edgeB = np.exp(-((db - band * 2) / 0.012) ** 2)
    groove = np.where(onA & onB, np.where(over, edgeA, edgeB), np.maximum(edgeA * ~onB, edgeB * ~onA))
    field = (~onA & ~onB).astype(np.float32) * 0.55          # (the ground between the bands cut back)
    return np.clip(groove + field, 0, 1)


def talon(M, mat, base, d1, d2, length, r0, seg=14):
    """a claw: from base out along d1, curling toward d2, tapering to a point"""
    d1 = unit(np.asarray(d1, float)); d2 = unit(np.asarray(d2, float))
    pts = []
    p = np.asarray(base, float)
    for k in range(seg):
        s = k / (seg - 1)
        d = unit(d1 * (1 - s) + d2 * s)
        pts.append(p.copy()); p = p + d * length / (seg - 1)
    M.add('Talon', mat, *rod(np.array(pts), r0 * (1 - np.linspace(0, 1, seg)) ** 0.8 + 0.02, 10))


def nahkriin(texonly=False):
    """(Nahkriin, Warden of Skuldafn - "Mistblade sword"; Noah: "redo the whole blade design" - "Ethereal ghost blade",
    "Dragon Priest motif", "Pale blue-white") a blade of solid mist and pale light: no steel at all - a shape of fog
    held as a blade, brightest down its heart, see-through toward its edges, mist flowing up through it and pouring
    off it, a line of dragon-script hanging lit inside it. Its hilt is the only thing of it that is solid: ancient dark
    bronze carved all over in knotwork, a guard whose ends curl into claws, four talons rising from its collar to grip
    the blade's root, a grip of black leather bound in bronze, and a pommel of three talons clutching a pale stone."""
    etch = knotwork(2048, 5, 0.1, 61)
    bD, bN = metal_tex('nahk_bronze', (104, 84, 58), rust=0.0, dark=0.85, etch=etch, seed=62)
    lD, lN = leather_tex('nahk_leather', (34, 30, 30), seed=63)
    M = Mesh()
    # the guard: a heavy bar, thick at its middle, its two ends sweeping out and curling up into claws
    for side in (-1, 1):
        pts = [np.array([side * 6.2 * s, 0.0, 3.0 + 1.4 * s ** 2.5]) for s in np.linspace(0, 1, 16)]
        M.add('Bronze', 'bronze', *rod(np.array(pts), np.linspace(1.05, 0.55, 16), 14))
        end = pts[-1]
        for k, (dy, curl) in enumerate(((-0.45, 0.0), (0.0, 0.0), (0.45, 0.0))):
            talon(M, 'bronze', end + np.array([0, dy, 0]), (side * 0.6, dy * 0.8, 1.0), (-side * 0.9, dy * 0.3, 0.4), 3.4 - 0.5 * abs(k - 1), 0.32)
    M.add('Bronze', 'bronze', *lathe([(1.6, 1.15), (2.0, 1.75), (2.6, 2.0), (3.6, 2.0), (4.0, 1.9), (4.3, 1.4), (4.4, 0.0)], 32))
    # (Noah: "make sure the blade itself connects to the handle cleanly") the socket's mouth: a bronze lip shaped round the
    # blade's footprint - a stadium as wide and deep as the blade's root - the blade standing up out of it
    a = np.linspace(0, 2 * np.pi, 97)
    hw, hd = 3.05, 0.95
    lip = np.stack([np.clip(np.cos(a) * 1.25, -1, 1) * hw, np.sin(a) * hd, np.full_like(a, 4.75)], -1)
    M.add('Bronze', 'bronze', *rod(lip, 0.42, 12))
    plate = np.stack([np.clip(np.cos(a) * 1.25, -1, 1) * hw, np.sin(a) * hd], -1)
    for zz, sh in ((4.35, 1.0), (4.0, 0.82)):
        ring = np.column_stack([plate * sh, np.full(len(a), zz)])
        M.add('Bronze', 'bronze', *rod(ring, 0.38, 10))
    # four talons from the collar gripping the blade's root
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        d = np.array([math.cos(a), math.sin(a) * 0.55, 0.0])
        talon(M, 'bronze', np.array([d[0] * 2.6, d[1] * 1.4, 4.8]), d * 0.4 + np.array([0, 0, 1.0]), -d * 0.7 + np.array([0, 0, 1.0]), 3.4, 0.36)
    # the grip: black leather, three bronze bands
    grip_wrap(M, 'leather', -7.6, 1.5, 0.86, 0.85)
    for z in (-7.4, -3.0, 1.4):
        M.add('Bronze', 'bronze', *lathe([(z - 0.45, 0.9), (z - 0.3, 1.12), (z + 0.3, 1.12), (z + 0.45, 0.9)], 24))
    # the pommel: a knotted bronze cup and three talons clutching a pale stone
    M.add('Bronze', 'bronze', *lathe([(-7.8, 0.0), (-7.8, 0.95), (-8.4, 1.35), (-9.0, 1.2), (-9.2, 0.0)], 28))
    sc = np.array([0, 0, -10.3])
    stone = [(sc[2] - 1.05 * math.cos(a), 1.05 * math.sin(a)) for a in np.linspace(0, math.pi, 16)]
    M.add('Stone', 'stone', *lathe(stone, 22, uvs=(1, 1, 0, 0)))
    for k in range(3):
        a = 2 * math.pi * k / 3
        d = np.array([math.cos(a), math.sin(a), 0.0])
        talon(M, 'bronze', np.array([d[0] * 0.9, d[1] * 0.9, -9.0]), d * 0.8 + np.array([0, 0, -0.6]), -d + np.array([0, 0, -0.9]), 3.2, 0.3)
    # ---- the blade: all effect, no steel
    fx = Fx()
    L0, LEN = 3.4, 50.8   # (the blade rises from inside the socket: no gap where it meets the hilt)
    def blade_fx(name, opts, width, thick, scale_v, nx=11, ns=60, z0=L0, length=LEN):
        W = Mesh()
        blade(W, 'x', z0, length, width, thick, tip=0.26, seed=5, uv=(0, 1, 0, scale_v), ns=ns, nx=nx)
        for gname, m, V, N, UV, F in W.groups:
            fx.mesh(f'{name} {opts}', V, N, UV, F)
    mist = 'CurseOfAlduin\\priests\\nahk_mist.dds'
    blade_fx('BladeBody', f'tex={mist} color=0.82,0.9,1,0.75 scale=1.4 blend=alpha falloff=1 vscroll=2.2', 5.6, 1.4, 4.0)
    blade_fx('BladeHeart', f'tex={FX["swirl"]} color=0.75,0.88,1,1 scale=2.2 blend=add falloff=1 uscroll=2.6 vscroll=3.5', 2.8, 1.0, 3.0, z0=L0 + 0.5, length=LEN - 2.0)
    blade_fx('BladeHalo', f'tex={FX["glow"]} color=0.6,0.8,1,0.5 scale=1.0 blend=add rim=1', 7.4, 2.2, 1.0, nx=9, ns=30)
    # the script: a strip of glyphs lit inside the blade, seen from both faces
    runes = 'CurseOfAlduin\\priests\\nahk_runes.dds'
    for fy, nm in ((1, 'RunesA'), (-1, 'RunesB')):
        V, N, UV, F = [], [], [], []
        z0r, z1r = L0 + 2.0, L0 + LEN * 0.78
        for i2, z in enumerate((z0r, z1r)):
            for x in (-0.75, 0.75):
                V.append((x, 0.02 * fy, z)); N.append((0, fy, 0)); UV.append((0.5 + x / 1.5, i2 * (z1r - z0r) / (1.5 * 16)))
        F = [(0, 2, 1), (1, 2, 3)] if fy > 0 else [(0, 1, 2), (1, 3, 2)]
        fx.mesh(f'{nm} tex={runes} color=0.7,0.88,1,1 scale=2.4 blend=add', V, N, UV, F)
    fx.sheath('MistA', f'tex={FX["swirl"]} color=0.82,0.9,1,0.5 scale=1.3 blend=add falloff=1 uscroll=3 vscroll=4.5', 4.0, 56.0, 3.6, 2.4, 18, 12, 3.0)
    fx.sheath('MistB', f'tex={FX["swirl"]} color=0.78,0.88,1,0.32 scale=1.0 blend=add falloff=1 uscroll=-5 vscroll=6', 6.0, 60.0, 5.6, 4.4, 18, 12, 2.0)
    fx.card('RootGlow', FX['glow'], (0, 0, 5.2), 5.5, '0.65,0.85,1,0.65', 1.3)
    fx.card('RootCore', FX['core'], (0, 0, 5.0), 2.4, '0.75,0.9,1,1', 1.8, (4, 4, 6, 0))
    fx.card('StoneGlow', FX['glow'], tuple(sc), 3.0, '0.7,0.88,1,0.55', 1.0)
    # its own textures: the mist (soft wisps streaming along v, tiling) and the glyph strip
    S = 1024
    w1 = tileable(S, S, 1.4, 71, aniso=(0.25, 1.0)); w2 = tileable(S, S, 0.8, 72, aniso=(0.4, 1.0))
    mistv = np.clip(0.45 + 0.8 * (w1 - 0.5) + 0.35 * (w2 - 0.5), 0.05, 1.0)
    save(np.dstack([np.full((S, S, 3), 255.0) * (0.85 + 0.15 * mistv[..., None]), mistv * 255]), 'nahk_mist.dds', True)
    _d, _n, _g = glyph_strip('nahk_glyphs', (255, 255, 255), glow_rgb=None, seed=73)
    gm = np.asarray(Image.open(WORK / 'nahk_glyphs_d.dds.png').convert('RGBA'), np.float32)[..., 3] / 255
    for f in ('nahk_glyphs_d.dds', 'nahk_glyphs_n.dds'):
        (g.TEXDIR / f).unlink(missing_ok=True)
    rn = gaussian_filter(gm, 1.0) * 255; glow = gaussian_filter(gm, 6.0) * 255
    save(np.dstack([np.full(rn.shape + (3,), 255.0), np.clip(rn + glow * 0.7, 0, 255)]), 'nahk_runes.dds', True)
    stD = save(np.dstack([np.full((256, 256), 205.0), np.full((256, 256), 222.0), np.full((256, 256), 240.0)]) * (0.85 + 0.15 * tileable(256, 256, 1.0, 66)[..., None]), 'nahk_stone_d.dds')
    mats = {'bronze': f'diffuse={bD} normal={bN} spec=0.65 gloss=26 alpha=0 doublesided=0',
            'stone': f'diffuse={stD} normal={bN} glow={stD} emit=0.8,0.9,1 emitmult=1.2 spec=2.5 gloss=200 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12 alpha=0 doublesided=0'}
    return finish(M, 'nahkriin_mistblade', mats, fx.L, 'WeaponSword')


def grow(M, since, centre, k, lift=0.0):
    """the parts added since index `since` scaled by k about centre (and raised by lift): a head made bigger whole"""
    c = np.asarray(centre, float)
    out = []
    for i, (gname, m, V, N, UV, F) in enumerate(M.groups):
        if i >= since:
            V = (np.asarray(V) - c) * k + c + np.array([0, 0, lift])
            UV = np.asarray(UV) * k
        out.append((gname, m, V, N, UV, F))
    M.groups = out


def superbox(c, ex, ey, ez, p=6.0, nu=40, nv=20, R=np.eye(3)):
    """a block with softened edges (a superellipsoid): half sizes ex, ey, ez, its roundness p (bigger: squarer)"""
    V, N, UV = [], [], []
    def sgnpow(x, e): return np.sign(x) * np.abs(x) ** e
    e = 2.0 / p
    for i in range(nv + 1):
        ph = -math.pi / 2 + math.pi * i / nv
        for j in range(nu + 1):
            th = -math.pi + 2 * math.pi * j / nu
            x = sgnpow(math.cos(ph), e) * sgnpow(math.cos(th), e)
            y = sgnpow(math.cos(ph), e) * sgnpow(math.sin(th), e)
            z = sgnpow(math.sin(ph), e)
            V.append(np.array([x * ex, y * ey, z * ez]))
            N.append(np.array([sgnpow(math.cos(ph), 2 - e) * sgnpow(math.cos(th), 2 - e) / ex, sgnpow(math.cos(ph), 2 - e) * sgnpow(math.sin(th), 2 - e) / ey, sgnpow(math.sin(ph), 2 - e) / ez]))
            UV.append((j / nu * 2 * (ex + ey) * 2 / TILE, i / nv * 2 * ez * 2 / TILE))
    V = np.array(V) @ R.T + c; N = unit(np.array(N) @ R.T)
    F = np.array(grid_faces(nv + 1, nu + 1))
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    flip = (fn * N[F].mean(1)).sum(1) < 0
    F[flip] = F[flip][:, [0, 2, 1]]
    return V, N, np.array(UV) + rng.random(2), F


def sheet_runes(S, n, seed, size=150, width=16, glow=True):
    """a tiling sheet of scattered dragon-script glyphs (0..1), for runes cut over a hammer's faces"""
    im = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(im); r = np.random.default_rng(seed)
    cols = int(S / (size * 1.6)); rows = int(S / (size * 1.6))
    for a in range(cols):
        for b in range(rows):
            if r.random() > n: continue
            cx = (a + 0.5) * S / cols; cy = (b + 0.5) * S / rows
            k3 = r.integers(2, 4)
            for k in range(k3):
                ang = r.choice([-1, 0, 1]) * 0.5 + (r.random() - 0.5) * 0.25
                ox = (k - (k3 - 1) / 2) * size * 0.32
                l = size * (0.6 + 0.3 * r.random())
                dr.line([(cx + ox - math.sin(ang) * l * 0.5, cy - math.cos(ang) * l * 0.5), (cx + ox + math.sin(ang) * l * 0.5, cy + math.cos(ang) * l * 0.5)], fill=255, width=width)
    return np.asarray(im.filter(ImageFilter.GaussianBlur(1.5)), np.float32) / 255


def glow_tex(name, rgb, mask, blur=3.0):
    return save(np.array(rgb, np.float32) * np.clip(gaussian_filter(mask, blur) * 1.4 + mask * 0.6, 0, 1)[..., None], name)


def skinned_lash(fx, name, opts, pts, radius, bones, parent, barbs=0, seed=0):
    """a tube along pts skinned to bones (nodes at points along it, under parent): each ring to its nearest two bones"""
    pts = np.asarray(pts, float)
    P, N, UV, F = g.tube(pts, radius, 8, vrange=(0, len(pts) / 6.0))
    nb = len(bones)
    L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))])
    tpos = L / L[-1]
    fx.L.append(f'shape {name} {opts} parent={parent} skin={",".join(bones)}')
    ring = len(P) // len(pts)
    for i in range(len(P)):
        t = tpos[i // ring] * (nb - 1)
        b0 = int(min(nb - 2, math.floor(t))); w1 = t - b0
        fx.L.append(f'v {P[i][0]:.4f} {P[i][1]:.4f} {P[i][2]:.4f} {UV[i][0]:.4f} {UV[i][1]:.4f} 1 1 1 1')
        fx.L.append(f'n {N[i][0]:.4f} {N[i][1]:.4f} {N[i][2]:.4f}')
        fx.L.append(f'w {b0} {1 - w1:.4f} {b0 + 1} {w1:.4f}')
    for a, b, c in F:
        fx.L.append(f't {a} {b} {c}')


def volsung(texonly=False):
    """(Volsung of Volskygge - "Windcutter swords", twin light swords) a slender curved sabre - wielded one in each hand -
    of pale blue-grey steel, a long fuller swept along its back like a gust, script cut into it glowing faint sky-blue;
    a bronze guard of three swept-back feathered curls; a grip of blue-grey leather; a pommel curling into a spiral like
    a turning wind. Gusts swirl round the blade and streak off its edge."""
    bD, bN, bG = blade_tex('vols_blade', (170, 182, 196), edge_rgb=(225, 232, 242), glow_rgb=None, script=False, pattern='mist', seed=81)
    rD, rN, rG = glyph_strip('vols_runes', (150, 160, 172), glow_rgb=(150, 205, 255), seed=81)
    etch = knotwork(2048, 6, 0.09, 82)
    zD, zN = metal_tex('vols_bronze', (112, 92, 62), dark=0.8, etch=etch, seed=83)
    lD, lN = leather_tex('vols_leather', (58, 64, 74), seed=84)
    M = Mesh()
    blade(M, 'blade', 2.8, 40.0, 3.3, 0.8, tip=0.28, fuller=0.8, seed=85, curve=0.06, runes=('runes', 0.42, 0.06, 0.7, 21.0))
    # the guard: a small bronze block and three feathered curls swept back each side
    M.add('Bronze', 'bronze', *superbox(np.array([0, 0, 2.3]), 1.4, 0.8, 0.75, 4.0, 24, 12))
    for side in (-1, 1):
        for k, (L, up) in enumerate(((6.8, 0.8), (5.4, 1.8), (4.0, 2.6))):
            pts = [np.array([side * (1.2 + L * s), 0.0, 2.3 - 1.4 * s * s + up * s * s * 0.5]) for s in np.linspace(0, 1, 14)]
            pts = [p + np.array([0, 0, -k * 0.35]) for p in pts]
            curl = [pts[-1] + np.array([side * 0.5 * math.cos(a), 0, 0.5 * math.sin(a) + 0.5]) for a in np.linspace(-math.pi / 2, math.pi * 0.9, 10)]
            M.add('Bronze', 'bronze', *rod(np.array(pts + curl[1:]), np.linspace(0.42 - 0.07 * k, 0.1, len(pts) + len(curl) - 1), 10))
    grip_wrap(M, 'leather', -5.6, 1.7, 0.72, 0.8)
    M.add('Bronze', 'bronze', *lathe([(1.6, 0.78), (1.8, 0.98), (2.0, 0.9)], 20))
    M.add('Bronze', 'bronze', *lathe([(-5.6, 0.78), (-5.8, 0.98), (-6.1, 0.82), (-6.3, 0.0)], 20))
    spiral = [np.array([1.3 * (1 - t / 9) * math.cos(t) * 0.9, 0.0, -7.2 + 1.3 * (1 - t / 9) * math.sin(t)]) for t in np.linspace(0, 9, 40)]
    M.add('Bronze', 'bronze', *rod(np.array([np.array([0, 0, -6.2])] + spiral), np.linspace(0.42, 0.14, 41), 10))
    fx = Fx()
    fx.sheath('Gust', f'tex={FX["swirl"]} color=0.85,0.92,1,0.45 scale=1.3 blend=add falloff=1 uscroll=12 vscroll=2.5', 2.5, 44.0, 2.8, 1.8, 18, 12, 2.0)
    fx.sheath('GustStreaks', f'tex={FX["motes"]} color=0.9,0.95,1,0.6 scale=1.5 blend=add uscroll=16 vscroll=-4', 3.0, 44.0, 3.8, 2.6, 22, 10, 3.0)
    mats = {'blade': f'diffuse={bD} normal={bN} spec=1.8 gloss=110 alpha=0 doublesided=0',
            'runes': f'diffuse={rD} normal={rN} glow={rG} emit=0.6,0.8,1 emitmult=1.4 spec=1.2 gloss=70 alpha=blend doublesided=0',
            'bronze': f'diffuse={zD} normal={zN} spec=0.65 gloss=26 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12 alpha=0 doublesided=0'}
    return finish(M, 'volsung_windcutter', mats, fx.L, 'WeaponSword')




def otar(texonly=False):
    """(Otar the Mad - Noah: "the whip looks terrible but the handle isn't bad", then "let's just make it a sword made
    out of animated lightning", then "Steel + lightning edges") a storm blade: a grip of blackened iron bound in black
    leather, a pommel of three hooks, a crossguard of black iron, and a blade of dark storm-steel with lightning crackling
    along its edges, down its fuller and along the guard, sparks crawling up it"""
    iD, iN = metal_tex('otar_iron', (70, 68, 70), rust=0.35, dark=0.9, seed=91)
    lD, lN = leather_tex('otar_leather', (26, 22, 24), seed=92)
    M = Mesh()
    M.add('Iron', 'iron', *lathe([(-6.0, 0.0), (-6.0, 0.95), (-5.6, 1.1), (-5.3, 0.85)] + [(z, 0.8) for z in np.linspace(-5.2, 7.5, 5)] + [(7.6, 1.0), (8.4, 1.2), (8.8, 0.7), (9.6, 0.45)], 24))
    grip_wrap(M, 'leather', -5.0, 8.0, 0.85, 0.75)
    for k in range(3):
        a = 2 * math.pi * k / 3
        d = np.array([math.cos(a), math.sin(a), 0.0])
        talon(M, 'iron', np.array([d[0] * 0.6, d[1] * 0.6, -6.0]), d + np.array([0, 0, -0.6]), -d * 0.5 + np.array([0, 0, 0.6]), 2.6, 0.3)
    # (Noah: "the stormblade needs another model redesign" - "Steel + lightning edges") a real blade of dark storm-steel,
    # a fuller down its middle, out of the iron grip's mouth under a crossguard of black iron; lightning crackles along
    # both its edges to the point, down the fuller and along the guard - ribbons flickering frame by frame (flipbooks)
    fx = Fx()
    Z0, L, HW = 9.2, 32.0, 1.75
    bD, bN, _bg = blade_tex('otar_blade', (62, 64, 74), edge_rgb=(196, 200, 214), script=False, pattern='folded', seed=94)
    blade(M, 'blade', Z0, L, HW * 2, 0.78, tip=0.18, fuller=0.78, seed=95)
    def bladeshape(s_):   # (blade()'s own half width, for the bolts)
        body = HW * (1.0 - 0.18 * s_)
        pt = np.clip((1.0 - s_) / 0.18, 0, 1)
        return body * np.sqrt(np.clip(pt * (2 - pt), 0, 1))
    # the crossguard: a bar of black iron, its ends swept up into points, a boss over the blade's root
    M.add('Iron', 'iron', *superbox(np.array([0, 0, 9.0]), 4.6, 0.75, 0.55, 5.0, 28, 10))
    M.add('Iron', 'iron', *superbox(np.array([0, 0, 9.2]), 1.5, 1.05, 0.95, 3.0, 18, 10))
    for sd in (-1, 1):
        talon(M, 'iron', np.array([sd * 4.3, 0, 9.1]), (sd, 0, 0.5), (sd * 0.2, 0, 1), 2.6, 0.45)
    def ribbon(name, pts, width, tex, col, fps, ph, sc=2.8):
        """a bolt along pts: two crossed strips (one flat in the blade's plane, one across it), the flipbook's v along"""
        pts = np.asarray(pts, float); n = len(pts)
        tg = np.gradient(pts, axis=0); tg /= np.linalg.norm(tg, axis=1, keepdims=True)
        V, N, UV, F = [], [], [], []
        for plane in ((0, 1, 0), (1, 0, 0)):
            b0 = len(V)
            for i2, (p_, t_) in enumerate(zip(pts, tg)):
                side = np.cross(t_, plane)
                if np.linalg.norm(side) < 1e-3: side = np.cross(t_, (0, 0, 1))
                side /= np.linalg.norm(side)
                w_ = width[i2] if np.ndim(width) else width
                for sd in (-1, 1):
                    V.append(tuple(p_ + side * w_ * sd)); N.append(plane); UV.append(((sd + 1) / 2, 1 - i2 / (n - 1)))
            for i2 in range(n - 1):
                a0 = b0 + 2 * i2; F += [(a0, a0 + 1, a0 + 2), (a0 + 1, a0 + 3, a0 + 2), (a0, a0 + 2, a0 + 1), (a0 + 1, a0 + 2, a0 + 3)]
        fx.mesh(f'{name} tex=CurseOfAlduin\\priests\\{tex}.dds color={col} scale={sc} blend=add flip=4x2 fps={fps} phase={ph}', V, N, UV, F)
    S_ = np.linspace(0.02, 1, 40)
    for k, sd in enumerate((-1, 1)):   # (along both edges, meeting at the point)
        pts = [(sd * (bladeshape(s_) + 0.12), 0.0, Z0 + L * s_) for s_ in S_] + [(0.0, 0.0, Z0 + L + 0.5)]
        ribbon(f'EdgeBolt{k}', pts, 0.75, 'otar_edge', '0.85,0.8,1,1', 15 + 3 * k, 0.21 * k)
        ribbon(f'EdgeHaze{k}', pts, 1.3, 'otar_bolts', '0.5,0.4,1,0.5', 9 + k, 0.5 + 0.2 * k, 2.0)
    ribbon('RidgeBolt', [(0.0, 0.0, Z0 + L * s_ * 0.76) for s_ in S_], np.linspace(0.7, 0.35, 40), 'otar_edge', '0.8,0.75,1,0.9', 13, 0.37)
    for k, sd in enumerate((-1, 1)):   # (along the guard)
        gp_ = [(sd * (1.4 + 3.4 * t), 0.0, 9.0 + 0.9 * t ** 3) for t in np.linspace(0, 1, 16)]
        ribbon(f'GuardBolt{k}', gp_, 0.6, 'otar_edge', '0.85,0.8,1,1', 16 + 2 * k, 0.6 + 0.15 * k)
    fx.sheath('Sparks', f'tex={FX["motes"]} color=0.7,0.6,1,0.9 scale=2.0 blend=add uscroll=7 vscroll=-12', Z0, Z0 + L, 2.4, 1.0, 16, 10, 3.0)
    fx.card('GuardHalo', FX['glow'], (0, 0, 9.4), 3.5, '0.6,0.5,1,0.4', 1.0)
    fx.card('TipSpark', FX['core'], (0, 0, Z0 + L), 1.2, '0.85,0.8,1,1', 2.2, (4, 4, 22, 0.4))
    # the bolts' flipbook: 8 frames (4x2) of a forked bolt running the blade's length, white on black (add blend)
    CW, CH = 256, 1024
    atlas = np.zeros((CH * 2, CW * 4), np.float32)
    r = np.random.default_rng(93)
    for fr in range(8):
        im = Image.new('L', (CW, CH), 0); dr = ImageDraw.Draw(im)
        def jag(x0, y0, x1, y1, n, amp):
            pts = [(x0, y0)]
            for t in np.linspace(0, 1, n)[1:-1]:
                pts.append((x0 + (x1 - x0) * t + r.normal() * amp, y0 + (y1 - y0) * t + r.normal() * amp * 0.3))
            pts.append((x1, y1)); return pts
        main = jag(CW / 2 + r.normal() * 10, 0, CW / 2 + r.normal() * 10, CH, 34, 22)
        dr.line(main, fill=255, width=5)
        for _ in range(r.integers(3, 6)):   # (forks off the main bolt)
            k0 = r.integers(2, len(main) - 6); x0, y0 = main[k0]
            x1 = np.clip(x0 + r.choice([-1, 1]) * r.uniform(40, 110), 6, CW - 6); y1 = y0 + r.uniform(60, 200)
            dr.line(jag(x0, y0, x1, y1, 9, 10), fill=190, width=2)
        a = np.asarray(im, np.float32) / 255
        a = np.clip(a + gaussian_filter(a, 4) * 1.6 + gaussian_filter(a, 14) * 1.2, 0, 1)
        a = a * np.clip(np.minimum(np.arange(CW) + 0.5, CW - np.arange(CW) - 0.5) / 30, 0, 1)[None, :]
        cy, cx = divmod(fr, 4)
        atlas[cy * CH:(cy + 1) * CH, cx * CW:(cx + 1) * CW] = a
    save(np.dstack([atlas * 255] * 3), 'otar_bolts.dds')
    atlas = np.zeros((CH * 2, CW * 4), np.float32)
    for fr in range(8):
        im = Image.new('L', (CW, CH), 0); dr = ImageDraw.Draw(im)
        pts = [(CW / 2, 0)]
        for t in np.linspace(0, 1, 60)[1:-1]:
            pts.append((CW / 2 + r.normal() * 16, CH * t + r.normal() * 3))
        pts.append((CW / 2, CH)); dr.line(pts, fill=255, width=6)
        for _ in range(r.integers(2, 5)):
            k0 = r.integers(2, len(pts) - 4); x0, y0 = pts[k0]
            dr.line([(x0, y0), (x0 + r.choice([-1, 1]) * r.uniform(30, 70), y0 + r.uniform(20, 60))], fill=160, width=2)
        a_ = np.asarray(im, np.float32) / 255
        a_ = np.clip(a_ + gaussian_filter(a_, 3) * 1.4 + gaussian_filter(a_, 10) * 0.8, 0, 1)
        a_ = a_ * np.clip(np.minimum(np.arange(CW) + 0.5, CW - np.arange(CW) - 0.5) / 24, 0, 1)[None, :]
        cy, cx = divmod(fr, 4)
        atlas[cy * CH:(cy + 1) * CH, cx * CW:(cx + 1) * CW] = a_
    save(np.dstack([atlas * 255] * 3), 'otar_edge.dds')
    mats = {'iron': f'diffuse={iD} normal={iN} spec=0.5 gloss=22 alpha=0 doublesided=0',
            'blade': f'diffuse={bD} normal={bN} spec=1.6 gloss=90 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12 alpha=0 doublesided=0'}
    return finish(M, 'otar_stormblade', mats, fx.L, 'WeaponSword', root='OtarStormblade')


def rahgot(texonly=False):
    """(Rahgot of Forelhost - "Martyr's dagger"; Noah: "completely redone" - "Ritual sacrifice knife": "A broad, curved
    obsidian-black blade, a bone and gold hilt, poison runes etched along its spine") a sacrificial knife: a broad
    single-edged blade of black volcanic glass, curving like a crescent toward its point, its edge knapped into
    scalloped facets that catch the light, the Dragon tongue's letters cut along its thick spine and glowing poison
    green; a hilt of yellowed bone between collars of worn gold, a gold guard swept forward like two fangs, and a
    pommel of gold clutching a small green stone. A green haze clings to it and poison beads at its point."""
    S = 2048
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    # obsidian: near-black glass, conchoidal ripples, the knapped scallops along the edge
    rip = tileable(S, S, 1.0, 151, aniso=(1.0, 0.35)); fine = tileable(S, S, 0.5, 152)
    # the bevel (u past 0.68): ground and polished, fine grind lines along it, a bright line at its shoulder and a
    # glassy, faintly translucent keen edge
    grind = tileable(S, S, 0.3, 153, aniso=(0.02, 1.0))
    bev = np.clip((u - 0.68) / 0.02, 0, 1)
    shoulder = np.exp(-((u - 0.68) / 0.006) ** 2)
    keen = np.clip((u - 0.955) / 0.045, 0, 1) ** 1.5
    col = np.array([22, 22, 26], np.float32) * (0.8 + 0.5 * (rip[..., None] - 0.5))
    col = col * (1 - bev[..., None]) + (np.array([40, 42, 48]) * (0.85 + 0.3 * (grind[..., None] - 0.5))) * bev[..., None]
    col = col + np.array([120, 125, 135]) * shoulder[..., None] * 0.8 + np.array([70, 96, 84]) * keen[..., None]
    h = (0.5 * rip + 0.2 * fine) * (1 - bev) + 0.12 * grind * bev
    bD = save(col, 'rahg_obsidian_d.dds')
    bN = save(np.dstack([height_to_normal(gaussian_filter(h, 0.7), 6.0), np.clip(170 + 60 * bev + 25 * shoulder, 0, 255)]), 'rahg_obsidian_n.dds', True)
    # (Noah: "the dragon text on the blade should be glowing green, right now I can barely see the text") the letters lit bright poison green
    rD, rN, rG = glyph_strip('rahg_runes', (40, 44, 40), glow_rgb=(120, 255, 90), seed=153, lit=(120, 255, 90))
    gD, gN = metal_tex('rahg_gold', (190, 152, 66), dark=0.5, seed=154)
    # bone: yellowed ivory, its grain running along the hilt, pores, hairline age-cracks, browned in the low places
    S2 = 2048
    grain = tileable(S2, S2, 0.6, 158, aniso=(0.04, 1.0)); por = tileable(S2, S2, 0.25, 159); blot = tileable(S2, S2, 1.6, 160)
    d1, d2 = g.cells(1024, 160, 161); age = np.kron(np.clip(1 - (d2 - d1) / 1.5, 0, 1), np.ones((2, 2))) * (blot > 0.45)
    pores = np.clip((por - 0.7) * 5, 0, 1)
    bc = np.array([222, 208, 172], np.float32) * (0.82 + 0.18 * (grain[..., None] - 0.5) + 0.12 * (blot[..., None] - 0.5))
    bc = bc * (1 - 0.35 * np.clip((0.5 - blot) * 2, 0, 1)[..., None]) + np.array([120, 92, 56]) * 0.35 * np.clip((0.5 - blot) * 2, 0, 1)[..., None]
    bc = bc * (1 - 0.5 * (age + pores)[..., None].clip(0, 1)) + np.array([70, 56, 40]) * 0.5 * (age + pores)[..., None].clip(0, 1)
    boneD = save(bc, 'rahg_bone_d.dds')
    boneN = save(np.dstack([height_to_normal(gaussian_filter(0.4 * grain - 0.6 * age - 0.5 * pores + 0.3 * blot, 0.7), 6.0), np.clip(110 + 40 * grain - 80 * pores, 0, 255)]), 'rahg_bone_n.dds', True)
    M = Mesh()
    # the blade: broad, curving toward the point, the edge on the outside of the curve; built as a blade, then bent
    W = Mesh()
    # (Noah: "should look sharper like it has a blade edge", then "the blade edge is on the wrong side of the blade and
    # the blade is too wide for the hilt") single-edged: a thick spine carrying the script on the outside of the curve,
    # a ground bevel down to a keen glassy edge on its inside, a needle point; no wider than the guard
    blade(W, 'obsidian', 2.0, 17.0, 2.9, 0.85, tip=0.42, seed=156, ns=90, curve=-0.2, single=0.32, runes=('runes', 0.24, 0.06, 0.78, 15.0, -0.38))
    for gname, m, V, N, UV, F in W.groups:   # (Noah: "the blade is facing the wrong way") mirrored: its edge to the front
        V = np.asarray(V, float) * [-1, 1, 1]; N = np.asarray(N, float) * [-1, 1, 1]; F = np.asarray(F)[:, [0, 2, 1]]
        UV = np.asarray(UV, float).copy()
        if m == 'runes': UV[:, 0] = 1 - UV[:, 0]   # (the letters read the right way round)
        M.add(gname, m, V, N, UV, F)
    # the guard: a gold bar whose ends sweep forward like fangs
    M.add('Gold', 'gold', *superbox(np.array([0, 0, 1.6]), 1.5, 0.75, 0.55, 4.0, 22, 10))
    for side in (-1, 1):
        pts = [np.array([side * (1.2 + 1.8 * s_), 0.0, 1.6 + 2.0 * s_ ** 2]) for s_ in np.linspace(0, 1, 12)]
        M.add('Gold', 'gold', *rod(np.array(pts), np.linspace(0.42, 0.05, 12), 10))
    # the hilt: bone between gold collars, a knuckle ridge carved into the bone
    M.add('Gold', 'gold', *lathe([(1.2, 0.7), (1.0, 0.85), (0.6, 0.8)], 22))
    M.add('Bone', 'bone', *lathe([(0.6, 0.62)] + [(z, 0.6 + 0.07 * (math.sin(z * 4.0) > 0.6)) for z in np.linspace(0.4, -4.6, 26)] + [(-4.8, 0.62)], 24))
    M.add('Gold', 'gold', *lathe([(-4.8, 0.65), (-5.0, 0.84), (-5.4, 0.9), (-5.6, 0.7)], 22))
    # the pommel: gold claws round a green stone
    sc = np.array([0, 0, -6.4])
    M.add('Stone', 'stone', *lathe([(sc[2] - 0.75 * math.cos(a), 0.75 * math.sin(a)) for a in np.linspace(0, math.pi, 14)], 18, uvs=(1, 1, 0, 0)))
    for k in range(3):
        a = 2 * math.pi * k / 3
        d = np.array([math.cos(a), math.sin(a), 0.0])
        talon(M, 'gold', np.array([d[0] * 0.55, d[1] * 0.55, -5.6]), d * 0.6 + np.array([0, 0, -0.8]), -d * 0.9 + np.array([0, 0, -0.6]), 1.9, 0.2)
    fx = Fx()
    fx.sheath('PoisonHaze', f'tex={FX["swirl"]} color=0.4,0.9,0.25,0.35 scale=1.2 blend=add falloff=1 uscroll=2 vscroll=3', 1.5, 19.0, 2.3, 1.6, 16, 10, 2.0)
    fx.card('TipBead', FX['core'], (0.2 * 17.0, 0, 19.0), 0.7, '0.45,1,0.25,1', 2.0, (4, 4, 10, 0))
    fx.card('StoneGlow', FX['glow'], tuple(sc), 1.8, '0.45,1,0.3,0.5', 1.0)
    stD = save(np.dstack([np.full((256, 256), 70.0), np.full((256, 256), 190.0), np.full((256, 256), 60.0)]) * (0.8 + 0.2 * tileable(256, 256, 1.0, 157)[..., None]), 'rahg_stone_d.dds')
    mats = {'obsidian': f'diffuse={bD} normal={bN} spec=2.4 gloss=170 alpha=0 doublesided=0',
            'runes': f'diffuse={rD} normal={rN} glow={rG} emit=0.4,1,0.25 emitmult=5.0 spec=1.0 gloss=80 alpha=blend doublesided=0',
            'gold': f'diffuse={gD} normal={gN} spec=0.9 gloss=34 alpha=0 doublesided=0',
            'bone': f'diffuse={boneD} normal={boneN} spec=0.3 gloss=16 alpha=0 doublesided=0',
            'stone': f'diffuse={stD} normal={gN} glow={stD} emit=0.45,1,0.3 emitmult=1.0 spec=2.5 gloss=200 alpha=0 doublesided=0'}
    return finish(M, 'rahgot_dagger', mats, fx.L, 'WeaponDagger')


def krosis(texonly=False):
    """(Krosis of Shearpoint - "Grief blade", a sword that weeps light) a mourner's sword of dark, smoke-grey steel; down
    each face a line of tear-shaped hollows inlaid with silver, light welling in them and running down the blade; a
    guard of silver branches drooping like a willow's; a grip of black leather bound with silver wire; a pommel of a
    pale crystal teardrop. Drops of light fall from it."""
    S = 2048
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    tears = np.zeros((S, S), np.float32)
    for k in range(9):
        vc = 0.1 + 0.075 * k
        for uc in (0.36, 0.64):
            du = (u - uc) / 0.03; dv = (v - vc) / 0.04
            r = np.sqrt(du ** 2 + np.where(dv > 0, dv * 0.6, dv * 1.2) ** 2)
            tears = np.maximum(tears, np.clip(1.2 - r, 0, 1) ** 0.5)
    trail = np.exp(-((np.minimum(np.abs(u - 0.36), np.abs(u - 0.64))) / 0.006) ** 2) * (v < 0.78) * (v > 0.08) * 0.6
    mask = np.clip(tears + trail, 0, 1)
    bD, bN, bG = blade_tex('kros_blade', (92, 96, 104), edge_rgb=(170, 176, 186), glow_rgb=(255, 236, 190), script=False, pattern='folded', seed=111, mask=mask, mask_rgb=(240, 225, 190))
    sD, sN = metal_tex('kros_silver', (176, 176, 180), dark=0.65, seed=112)
    lD, lN = leather_tex('kros_leather', (30, 28, 30), seed=113)
    M = Mesh()
    blade(M, 'blade', 2.6, 44.0, 4.0, 0.95, tip=0.22, fuller=0.0, seed=114)
    M.add('Silver', 'silver', *superbox(np.array([0, 0, 2.2]), 1.6, 0.8, 0.7, 4.0, 24, 12))
    for side in (-1, 1):   # (the willow: branches arching out and drooping, thinning to tips)
        for k, (reach, droop) in enumerate(((5.6, 4.2), (4.4, 3.6), (3.2, 2.6))):
            pts = [np.array([side * (1.2 + reach * s), (k - 1) * 0.3, 2.4 + 1.4 * math.sin(math.pi * s * 0.6) - droop * s ** 2.4]) for s in np.linspace(0, 1, 16)]
            M.add('Silver', 'silver', *rod(np.array(pts), np.linspace(0.38, 0.06, 16), 8))
            for q in (0.5, 0.75):   # (leaves: little drooping twigs)
                i0 = int(q * 15)
                twig = [pts[i0] + np.array([side * 0.3 * s, 0, -1.2 * s]) for s in np.linspace(0, 1, 5)]
                M.add('Silver', 'silver', *rod(np.array(twig), np.linspace(0.12, 0.03, 5), 6))
    grip_wrap(M, 'leather', -6.6, 1.5, 0.78, 0.8)
    nr = 140; zz = np.linspace(-6.5, 1.4, nr)
    wire = np.stack([0.86 * np.cos(-zz / 0.8 * 2 * np.pi), 0.86 * np.sin(-zz / 0.8 * 2 * np.pi), zz], -1)
    M.add('Silver', 'silver', *rod(wire, 0.07, 6))
    M.add('Silver', 'silver', *lathe([(1.5, 0.8), (1.7, 1.05), (1.9, 0.95)], 20))
    M.add('Silver', 'silver', *lathe([(-6.6, 0.8), (-6.85, 1.05), (-7.1, 0.85), (-7.2, 0.0)], 20))
    tear = [(-7.0, 0.0), (-7.1, 0.45), (-7.6, 0.85), (-8.3, 1.0), (-9.0, 0.85), (-9.5, 0.45), (-9.7, 0.0)]
    M.add('Crystal', 'crystal', *lathe(tear, 18, uvs=(1, 1, 0, 0)))
    fx = Fx()
    fx.sheath('Tears', f'tex={FX["motes"]} color=1,0.93,0.75,0.9 scale=1.8 blend=add vscroll=-3.5', 3.0, 46.0, 2.4, 1.8, 14, 10, 4.0)
    fx.sheath('Grieflight', f'tex={FX["swirl"]} color=1,0.92,0.75,0.3 scale=1.1 blend=add falloff=1 uscroll=2 vscroll=-2', 2.5, 46.0, 3.0, 2.2, 16, 10, 2.0)
    fx.card('CrystalGlow', FX['glow'], (0, 0, -8.3), 2.6, '1,0.94,0.8,0.55', 1.0)
    cD = save(np.dstack([np.full((256, 256), 240.0), np.full((256, 256), 232.0), np.full((256, 256), 214.0)]) * (0.85 + 0.15 * tileable(256, 256, 1.0, 115)[..., None]), 'kros_crystal_d.dds')
    mats = {'blade': f'diffuse={bD} normal={bN} glow={bG} emit=1,0.92,0.75 emitmult=1.8 spec=1.5 gloss=90 alpha=0 doublesided=0',
            'silver': f'diffuse={sD} normal={sN} spec=0.75 gloss=30 alpha=0 doublesided=0',
            'crystal': f'diffuse={cD} normal={sN} glow={cD} emit=1,0.94,0.8 emitmult=1.0 spec=2.5 gloss=200 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12 alpha=0 doublesided=0'}
    return finish(M, 'krosis_griefblade', mats, fx.L, 'WeaponSword')


def hevnoraak(texonly=False):
    """(Hevnoraak of Valthume - "Sealbreaker battleaxe") a two-handed battleaxe: a great crescent bit of barrow stone,
    a cracked seal carved into each face glowing marsh-green, its edge ground keen; riveted iron cheek plates, an iron
    back-spike, iron langets down a haft of black bog-oak bound in leather and iron. Marsh fog curls about the head."""
    iD, iN = metal_tex('hevn_iron', (68, 70, 66), rust=0.45, dark=0.9, seed=122)
    wD, wN = ms.wood_tex() if False else metal_tex('hevn_oak', (42, 34, 28), dark=0.6, seed=123)
    lD, lN = leather_tex('hevn_leather', (40, 34, 28), seed=124)
    M = Mesh()
    # the haft
    M.add('Oak', 'oak', *lathe([(-22.0, 0.0), (-22.0, 1.05), (-21.6, 1.2)] + [(z, 1.05 - 0.004 * z) for z in np.linspace(-21.0, 46.0, 10)] + [(46.5, 1.0)], 24))
    grip_wrap(M, 'leather', -18.0, -4.0, 1.12, 1.15)
    grip_wrap(M, 'leather', 4.0, 12.0, 1.1, 1.15)
    for z in (-22.3, -18.4, -3.6, 3.6, 12.4):
        M.add('Iron', 'iron', *lathe([(z - 0.5, 1.12), (z - 0.35, 1.38), (z + 0.35, 1.38), (z + 0.5, 1.12)], 24))
    for fy in (1, -1):   # (langets: iron strips down the haft under the head)
        M.add('Iron', 'iron', *superbox(np.array([0, fy * 1.05, 29.0]), 0.55, 0.18, 6.0, 6.0, 12, 10))
    # (Noah: "Let's make 8 & 9 different weapons" - "Sealbreaker battleaxe": "the blade carved from the cracked seal
    # stone, glowing green") a great crescent bit of grey barrow stone, its flat faces chisel-dressed and carved each
    # side with the Dragon cult's sealing glyph, split by a crack glowing marsh-green; its edge ground keen, the stone
    # knapped along it; clamped to the haft between riveted iron cheek plates, an iron back-spike behind
    hz = 39.0
    nu, nr = 48, 40
    rows = []
    for i2 in range(nu + 1):
        u = i2 / nu
        H = 3.0 + 6.0 * u ** 1.4 + 3.2 * u ** 5   # (the bit flaring into horns top and bottom)
        t_ = 1.0 if u < 0.68 else max(0.04, 1.0 - (u - 0.68) / 0.32 * 0.96)   # (the slab, then the ground bevel to the edge)
        ring = []
        for j2 in range(nr):
            th = 2 * math.pi * j2 / nr
            c_, s_ = math.cos(th), math.sin(th)
            vv = np.sign(c_) * abs(c_) ** 0.18
            yy = t_ * np.sign(s_) * abs(s_) ** 0.18
            x = 1.15 + u * (12.4 - 5.0 * vv * vv)
            ring.append((x, yy, hz + 1.0 + vv * H))
        rows.append(ring)
    V = np.array(rows)                       # (nu+1, nr, 3)
    V = np.concatenate([V, V[:, :1]], 1)     # (the ring closed)
    du = np.gradient(V, axis=0); dv = np.gradient(V, axis=1)
    Nn_ = unit(np.cross(du, dv))
    cen = V.mean(1, keepdims=True); Nn_ = Nn_ * np.sign(((V - cen) * Nn_).sum(-1, keepdims=True) + 1e-9)
    cr = np.random.default_rng(134)
    knap = 0.22 * np.abs(np.sin(V[..., 2] * 2.6 + cr.random() * 6)) * np.clip((np.arange(nu + 1)[:, None] / nu - 0.86) / 0.14, 0, 1)
    V = V.copy(); V[..., 0] -= knap          # (the edge knapped into shallow scallops)
    UVb = np.stack([V[..., 0] / TILE * 1.6, V[..., 2] / TILE * 1.6], -1) + rng.random(2)
    Fb = np.array(grid_faces(nu + 1, nr + 1))
    Pb = V.reshape(-1, 3); Nb = Nn_.reshape(-1, 3)
    fn = np.cross(Pb[Fb[:, 1]] - Pb[Fb[:, 0]], Pb[Fb[:, 2]] - Pb[Fb[:, 0]]); flip = (fn * Nb[Fb].mean(1)).sum(1) < 0; Fb[flip] = Fb[flip][:, [0, 2, 1]]
    M.add('Stone', 'stone', Pb, Nb, UVb.reshape(-1, 2), Fb)
    # the iron cheek plates and the collar binding the stone to the haft, riveted through
    for sy in (-1, 1):
        M.add('Iron', 'iron', *superbox(np.array([2.4, sy * 1.12, hz + 1.0]), 2.6, 0.22, 4.4, 5.0, 20, 14))
        for (rx, rz) in ((1.0, -2.6), (1.0, 4.6), (3.9, -2.0), (3.9, 4.0)):
            M.add('Iron', 'iron', *superbox(np.array([rx, sy * 1.42, hz + rz]), 0.3, 0.18, 0.3, 2.2, 10, 6))
    M.add('Iron', 'iron', *lathe([(hz - 5.0, 1.18), (hz - 4.6, 1.5), (hz + 6.6, 1.5), (hz + 7.0, 1.2), (hz + 7.4, 0.0)], 24))
    talon(M, 'iron', np.array([-1.2, 0, hz + 1.0]), (-1, 0, 0.15), (-0.6, 0, -0.8), 6.0, 0.75)   # (the back-spike)
    for sy in (-1, 1):   # (the seal carved into each face of the slab, a decal a hair proud of it)
        y = sy * 1.022; c = np.array([8.4, y, hz + 1.0]); R_ = 2.9
        P = np.array([[c[0] - R_, y, c[2] - R_], [c[0] + R_, y, c[2] - R_], [c[0] - R_, y, c[2] + R_], [c[0] + R_, y, c[2] + R_]])
        U = np.array([[0, 1], [1, 1], [0, 0], [1, 0]], float)
        if sy < 0: U[:, 0] = 1 - U[:, 0]
        Ff = np.array([[0, 2, 1], [1, 2, 3]]) if sy > 0 else np.array([[0, 1, 2], [1, 3, 2]])
        M.add('Seal', 'seal', P, np.tile([0.0, sy, 0.0], (4, 1)), U, Ff)
    fx = Fx()
    fx.sheath('MarshFog', f'tex={FX["swirl"]} color=0.55,0.75,0.5,0.4 scale=1.1 blend=add falloff=1 uscroll=2 vscroll=1.5', 30.0, 52.0, 10.0, 9.0, 22, 8, 1.2)
    for sy in (-1, 1):
        fx.card(f'SealGlow{"L" if sy < 0 else "R"}', FX['glow'], (8.4, sy * 1.6, hz + 1.0), 4.2, '0.45,0.9,0.4,0.4', 1.0)
    # the stone: dressed barrow stone, tiling - chisel strokes, pits, lichen in the low places
    S = 2048
    big = tileable(S, S, 2.0, 125); fine = tileable(S, S, 0.5, 126); chis = tileable(S, S, 0.9, 127, aniso=(1.0, 0.1))
    d1, d2 = g.cells(512, 300, 128); pit = np.tile(np.clip(1 - d1 / 10.0, 0, 1), (4, 4))
    pit = pit * np.clip((tileable(S, S, 1.0, 133) - 0.45) * 3, 0, 1)   # (pitted in patches, not evenly)
    col = np.array([108, 106, 98], np.float32) * (0.72 + 0.35 * (big[..., None] - 0.5) + 0.25 * (fine[..., None] - 0.5) + 0.12 * (chis[..., None] - 0.5) - 0.18 * pit[..., None])
    lichen = np.clip((tileable(S, S, 1.6, 129) - 0.62) * 3, 0, 1) * np.clip(0.5 + pit, 0, 1)
    col = col * (1 - 0.55 * lichen[..., None]) + np.array([72, 86, 52]) * 0.55 * lichen[..., None]
    h = 0.4 * big + 0.3 * fine + 0.35 * chis - 0.6 * pit
    stD = save(col, 'hevn_stone_d.dds')
    stN = save(np.dstack([height_to_normal(gaussian_filter(h, 0.8), 9.0), np.clip(30 + 25 * fine, 0, 255)]), 'hevn_stone_n.dds', True)
    # the seal: a ring of the Dragon tongue's letters between two grooves, a dragon's three-taloned claw at its heart, and a
    # crack through it all glowing marsh-green; cut into the stone (colour with alpha: only the cuts)
    T = 1024; SS = 2
    im = Image.new('L', (T * SS, T * SS), 0); dr = ImageDraw.Draw(im)
    ic = Image.new('L', (T * SS, T * SS), 0); dc = ImageDraw.Draw(ic)
    C = T * SS / 2
    for rr, w in ((0.47, 22), (0.335, 14), (0.3, 6)):
        R_ = rr * T * SS; dr.ellipse([C - R_, C - R_, C + R_, C + R_], outline=255, width=w * SS)
    _d, _n, _g = glyph_strip('hevn_glyphs', (255, 255, 255), seed=132)
    gm = np.asarray(Image.open(WORK / 'hevn_glyphs_d.dds.png').convert('RGBA'), np.float32)[..., 3]
    for f in ('hevn_glyphs_d.dds', 'hevn_glyphs_n.dds'):
        (g.TEXDIR / f).unlink(missing_ok=True)
    nl = 14
    for k in range(nl):   # (the letters round the ring, upright to its centre)
        letter = Image.fromarray(gm[(k % 16) * 256:(k % 16 + 1) * 256].astype(np.uint8)).resize((150 * SS, 150 * SS), Image.LANCZOS)
        a = 2 * math.pi * k / nl
        rot = letter.rotate(-math.degrees(a), resample=Image.BICUBIC, expand=True)
        cx_ = C + math.sin(a) * 0.403 * T * SS; cy_ = C - math.cos(a) * 0.403 * T * SS
        im.paste(255, (int(cx_ - rot.width / 2), int(cy_ - rot.height / 2)), rot.point(lambda x: 255 if x > 110 else 0))
    for k in range(3):   # (the claw: three talons sweeping down to a point, a knuckle ring above)
        ox = (k - 1) * 0.11 * T * SS
        pts_l, pts_r = [], []
        for t in np.linspace(0, 1, 24):
            x = C + ox * (1 - 0.55 * t) + (k - 1) * 0.02 * T * SS * t ** 2
            y = C - 0.2 * T * SS + 0.42 * T * SS * t
            w_ = (0.032 * (1 - t) ** 0.8 + 0.004) * T * SS
            pts_l.append((x - w_, y)); pts_r.append((x + w_, y))
        dr.polygon(pts_l + pts_r[::-1], fill=255)
    R_ = 0.07 * T * SS; dr.ellipse([C - R_, C - 0.25 * T * SS - R_, C + R_, C - 0.25 * T * SS + R_], outline=255, width=12 * SS)
    r = np.random.default_rng(130)
    pts = [(C - 0.5 * T * SS, C - 0.3 * T * SS)]
    for t in np.linspace(0, 1, 22)[1:]:   # (the crack: jagged, right across the seal, forking once)
        pts.append((C - 0.5 * T * SS + T * SS * t + r.normal() * 10 * SS, C - 0.3 * T * SS + 0.62 * T * SS * t + r.normal() * 16 * SS))
    dc.line(pts, fill=255, width=9 * SS)
    mid = pts[10]; fork = [mid] + [(mid[0] + 0.18 * T * SS * t + r.normal() * 8 * SS, mid[1] - 0.22 * T * SS * t + r.normal() * 8 * SS) for t in np.linspace(0, 1, 8)[1:]]
    dc.line(fork, fill=255, width=5 * SS)
    from scipy.ndimage import distance_transform_edt
    cut = np.asarray(im, np.float32) / 255 > 0.5; ck = np.asarray(ic, np.float32) / 255 > 0.5
    dep = np.clip(distance_transform_edt(cut) / (SS * 6.0), 0, 1)
    def down(a): return a.reshape(T, SS, T, SS).mean(axis=(1, 3))
    m = down(cut.astype(np.float32)); dp = down(dep); kk = down(ck.astype(np.float32))
    kglow = gaussian_filter(kk, 1.0)
    sc_ = np.array([62, 62, 58], np.float32) * (1 - 0.5 * dp[..., None])
    sc_ = sc_ * (1 - kk[..., None]) + np.array([60, 120, 50]) * kk[..., None]
    alpha = np.clip(m + kk + 0.0, 0, 1) * 255
    sD = save(np.dstack([sc_, alpha]), 'hevn_seal_d.dds', True)
    hs = -dp * 1.5 - kk * 1.5
    sN = save(np.dstack([height_to_normal(gaussian_filter(hs, 0.8), 7.0), np.full((T, T), 30.0)]), 'hevn_seal_n.dds', True)
    sG = glow_tex('hevn_seal_g.dds', (120, 235, 100), np.clip(kglow * 1.3 + m * dp * 0.25, 0, 1), 2.0)
    mats = {'iron': f'diffuse={iD} normal={iN} spec=0.5 gloss=22 alpha=0 doublesided=0',
            'stone': f'diffuse={stD} normal={stN} spec=0.2 gloss=12 alpha=0 doublesided=0',
            'seal': f'diffuse={sD} normal={sN} glow={sG} emit=0.5,0.95,0.45 emitmult=2.2 spec=0.3 gloss=14 alpha=blend doublesided=0',
            'oak': f'diffuse={wD} normal={wN} spec=0.25 gloss=14 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12 alpha=0 doublesided=0'}
    # (Noah: "spell breaker is also facing the wrong way") the bit turned to the front: everything mirrored across the haft
    mir = []
    for gn, m, V, N, UV, F in M.groups:
        UV = np.asarray(UV, float).copy()
        if m == 'seal': UV[:, 0] = 1 - UV[:, 0]
        mir.append((gn, m, np.asarray(V, float) * [-1, 1, 1], np.asarray(N, float) * [-1, 1, 1], UV, np.asarray(F)[:, [0, 2, 1]]))
    M.groups = mir
    fx.L = [' '.join([l.split(' ')[0]] + [f'{-float(l.split(" ")[1]):.4f}'] + l.split(' ')[2:]) if l.startswith('v ') else l for l in fx.L]
    fx.L = [l.replace('at=8.4', 'at=-8.4') for l in fx.L]
    return finish(M, 'hevnoraak_sealbreaker', mats, fx.L, 'WeaponBack')   # (a battleaxe: on the back)


def morokei(texonly=False):
    """(Morokei of Labyrinthian - "Arcane maul", runed warhammer) a two-handed maul whose head is a cluster of great blue
    crystals floating above a steel crown, girdled by three floating steel rings, the Dragon tongue's letters circling it
    in bands of light; the haft of dark steel wound with leather. Magicka swirls slowly round the head."""
    iD, iN = metal_tex('moro_steel', (74, 78, 86), dark=0.85, seed=143)
    lD, lN = leather_tex('moro_leather', (36, 34, 40), seed=144)
    M = Mesh()
    M.add('Steel', 'steel', *lathe([(-22.0, 0.0), (-22.0, 1.0), (-21.4, 1.25)] + [(z, 0.98) for z in np.linspace(-21.0, 44.0, 10)] + [(44.5, 1.1)], 24))
    grip_wrap(M, 'leather', -18.0, -3.0, 1.06, 1.15)
    grip_wrap(M, 'leather', 3.5, 12.0, 1.06, 1.15)
    for z in (-22.3, -18.4, -2.6, 3.0, 12.4, 20.0, 28.0, 36.0):
        M.add('Steel', 'steel', *lathe([(z - 0.55, 1.05), (z - 0.4, 1.35), (z + 0.4, 1.35), (z + 0.55, 1.05)], 24))
    # (Noah: "the hammer head needs to be redone" - "Floating crystal head": "A large blue crystal cluster floating above
    # the haft, held by steel rings, runes orbiting it") the haft ends in a steel crown of four prongs; above it, held in
    # the air by nothing, a cluster of great blue crystals - long hexagonal prisms with pointed ends, the biggest two
    # driven out sideways as the maul's two striking ends - girdled by three steel rings that float round it; the Dragon
    # tongue's letters circling it in two slow bands of light
    hc = np.array([0.0, 0.0, 54.0])
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        d = np.array([math.cos(a), math.sin(a), 0.0])
        talon(M, 'steel', np.array([d[0] * 0.9, d[1] * 0.9, 44.3]), d * 0.8 + np.array([0, 0, 1.0]), -d * 0.3 + np.array([0, 0, 1.0]), 4.5, 0.42)
    def prism(c, d, L, R, sides=6, seed=0):
        """a crystal: a hexagonal prism with a pointed end each way, every face flat (its own normal, its own strip of the
        sheet: u across the face, v along the crystal), the faces a little uneven"""
        d = unit(np.asarray(d, float)); c = np.asarray(c, float)
        a1 = unit(np.cross(d, [0.3, 0.5, 0.8])); a2 = np.cross(d, a1)
        pr = np.random.default_rng(700 + seed)
        rad = R * (0.85 + 0.3 * pr.random(sides)); tl = (R * 1.3, R * 1.8)
        ring = lambda z, k: c + d * z + (a1 * math.cos(2 * math.pi * k / sides) + a2 * math.sin(2 * math.pi * k / sides)) * rad[k % sides]
        tipA = c - d * (L * 0.5 + tl[0]); tipB = c + d * (L * 0.5 + tl[1])
        P, Nn, U, Ff = [], [], [], []
        u0 = pr.random()
        def quad(p0, p1, p2, p3, v0, v1):
            n = unit(np.cross(p1 - p0, p3 - p0)); n = n if np.dot(n, (p0 + p2) / 2 - c - d * np.dot((p0 + p2) / 2 - c, d)) > 0 else -n
            b = len(P); P.extend([p0, p1, p2, p3]); Nn.extend([n] * 4)
            U.extend([(u0, v0), (u0 + 0.16, v0), (u0 + 0.16, v1), (u0, v1)])
            Ff.extend([(b, b + 1, b + 2), (b, b + 2, b + 3)])
        def tri(p0, p1, p2, v0, v1):
            n = unit(np.cross(p1 - p0, p2 - p0)); n = n if np.dot(n, (p0 + p1 + p2) / 3 - c) > 0 else -n
            b = len(P); P.extend([p0, p1, p2]); Nn.extend([n] * 3)
            U.extend([(u0, v0), (u0 + 0.16, v0), (u0 + 0.08, v1)])
            Ff.append((b, b + 1, b + 2))
        Lz = (L + sum(tl)) / 6.0
        for k in range(sides):
            u0 = (k / sides + pr.random() * 0.1) % 0.84
            quad(ring(-L / 2, k), ring(-L / 2, k + 1), ring(L / 2, k + 1), ring(L / 2, k), tl[0] / Lz / 6, (tl[0] + L) / Lz / 6)
            tri(ring(L / 2, k), ring(L / 2, k + 1), tipB, (tl[0] + L) / Lz / 6, 1.0)
            tri(ring(-L / 2, k + 1), ring(-L / 2, k), tipA, tl[0] / Lz / 6, 0.0)
        P = np.array(P); Nn = np.array(Nn); Ff = np.array(Ff)
        fn = np.cross(P[Ff[:, 1]] - P[Ff[:, 0]], P[Ff[:, 2]] - P[Ff[:, 0]])
        flip = (fn * Nn[Ff].mean(1)).sum(1) < 0
        Ff[flip] = Ff[flip][:, [0, 2, 1]]
        M.add('Crystal', 'crystal', P, Nn, np.array(U), Ff)
    prism(hc, (1, 0, 0), 14.0, 2.6, seed=1)                     # (the great crystal: both striking ends)
    prism(hc + np.array([0, 0.5, 1.2]), (1, 0.15, 0.25), 9.0, 1.8, seed=2)
    prism(hc + np.array([0, -0.5, -1.0]), (1, -0.2, -0.2), 9.5, 1.7, seed=3)
    for k in range(9):   # (lesser crystals bursting out of the cluster)
        a = 2 * math.pi * k / 9 + 0.3
        d = np.array([0.35 * math.cos(a * 2), math.cos(a), math.sin(a)])
        prism(hc + d * 1.2, d, 3.0 + 1.6 * (k % 3), 0.7 + 0.25 * (k % 2), seed=10 + k)
    for k, (rr, tilt) in enumerate(((5.4, 0.0), (6.2, 0.5), (6.0, -0.5))):   # (the floating steel rings)
        a = np.linspace(0, 2 * np.pi, 97)
        ring = np.stack([np.zeros_like(a), rr * np.cos(a), rr * np.sin(a)], -1)
        Rm = rotm((0, 0, 1), tilt) @ rotm((0, 1, 0), 0.0)
        M.add('Steel', 'steel', *rod(ring @ Rm.T + hc + np.array([(k - 1) * 4.2, 0, 0]), 0.42, 10))
        for q_ in range(6):   # (studs round each ring)
            th = 2 * math.pi * q_ / 6 + 0.3 * k
            M.add('Steel', 'steel', *superbox(np.array([0, rr * math.cos(th), rr * math.sin(th)]) @ Rm.T + hc + np.array([(k - 1) * 4.2, 0, 0]), 0.6, 0.6, 0.6, 3.0, 10, 6))
    fx = Fx()
    runes = 'CurseOfAlduin\\priests\\moro_runeband.dds'
    for band, (rr, zoff, us, al) in enumerate(((7.8, 0.0, 3.0, 0.9), (9.0, 0.0, -2.2, 0.6))):
        V, N, UV, F = [], [], [], []
        nu = 64
        for i2 in range(2):
            for j2 in range(nu + 1):
                th = 2 * math.pi * j2 / nu
                V.append(hc + np.array([(i2 - 0.5) * 2.4, rr * math.cos(th), rr * math.sin(th)])); N.append((0, math.cos(th), math.sin(th)))
                UV.append((j2 / nu * 4, i2))
        for j2 in range(nu):
            a0 = j2; b0 = j2 + 1; c0 = j2 + nu + 1; d0 = c0 + 1
            F += [(a0, c0, b0), (b0, c0, d0), (a0, b0, c0), (b0, d0, c0)]
        fx.mesh(f'RuneBand{band} tex={runes} color=0.5,0.75,1,{al} scale=2.4 blend=add uscroll={us}', V, N, UV, F)
    fx.sheath('Magicka', f'tex={FX["swirl"]} color=0.45,0.7,1,0.4 scale=1.3 blend=add falloff=1 uscroll=4 vscroll=1.8', 46.0, 62.0, 11.0, 10.0, 24, 8, 1.2)
    fx.card('HeartGlow', FX['glow'], tuple(hc), 10.0, '0.45,0.7,1,0.5', 1.0)
    fx.card('HeartCore', FX['core'], tuple(hc), 4.5, '0.55,0.8,1,1', 2.0, (4, 4, 6, 0))
    # the letters' band: one row of the alphabet laid along, as light
    _d, _n, _g = glyph_strip('moro_glyphs', (255, 255, 255), seed=146)
    gm = np.asarray(Image.open(WORK / 'moro_glyphs_d.dds.png').convert('RGBA'), np.float32)[..., 3] / 255
    rowimg = np.concatenate([np.rot90(gm[k * 256:(k + 1) * 256]) for k in range(16)], axis=1)   # (16 letters side by side)
    rowimg = np.array(Image.fromarray((rowimg * 255).astype(np.uint8)).resize((2048, 128), Image.LANCZOS), np.float32)
    save(np.dstack([np.full(rowimg.shape + (3,), 255.0), np.clip(gaussian_filter(rowimg, 0.8) + gaussian_filter(rowimg, 5) * 0.8, 0, 255)]), 'moro_runeband.dds', True)
    for f in ('moro_glyphs_d.dds', 'moro_glyphs_n.dds'):
        (g.TEXDIR / f).unlink(missing_ok=True)
    # the crystal's sheet: deep blue, paling to its edges and points; milky inclusions and veils inside, growth lines
    # along its length, hairline fractures; it glows from within, brightest along its heart
    T = 1024
    v, u = (np.mgrid[0:T, 0:T].astype(np.float32) + 0.5) / T
    cloud = tileable(T, T, 1.2, 145); veil = tileable(T, T, 0.8, 146, aniso=(0.25, 1.0)); fine = tileable(T, T, 0.4, 147)
    fu = (u * 6.25) % 1.0   # (each face a strip 0.16 wide)
    edge = np.clip(1 - np.minimum(fu, 1 - fu) / 0.12, 0, 1) ** 2
    grow = np.abs(np.sin((v * 17 + 2.5 * cloud + 0.8 * fine) * math.pi)) ** 14 * np.clip((veil - 0.35) * 2, 0, 1)
    d1, d2 = g.cells(T, 90, 148); frac = np.clip(1 - (d2 - d1) / 2.0, 0, 1) * (cloud > 0.5)
    inc = np.clip((cloud - 0.45) * 2.5, 0, 1) * 0.6 + np.clip((veil - 0.55) * 3, 0, 1) * 0.4
    col = np.array([40, 92, 200], np.float32) * (0.75 + 0.4 * (fine[..., None] - 0.5))
    col = col + np.array([150, 200, 255]) * (0.35 * inc + 0.45 * edge + 0.15 * grow + 0.5 * frac)[..., None]
    cD = save(np.clip(col, 0, 255), 'moro_crystal_d.dds')
    hc_ = 0.5 * grow - 0.8 * frac + 0.2 * fine
    cN = save(np.dstack([height_to_normal(gaussian_filter(hc_, 0.7), 4.0), np.clip(220 + 30 * edge, 0, 255)]), 'moro_crystal_n.dds', True)
    cG = glow_tex('moro_crystal_g.dds', (90, 160, 255), np.clip(0.35 + 0.4 * inc + 0.5 * frac + 0.3 * edge, 0, 1), 1.5)
    mats = {'steel': f'diffuse={iD} normal={iN} spec=0.55 gloss=24 alpha=0 doublesided=0',
            'crystal': f'diffuse={cD} normal={cN} glow={cG} emit=0.5,0.75,1 emitmult=1.8 spec=3.0 gloss=240 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12 alpha=0 doublesided=0'}
    return finish(M, 'morokei_maul', mats, fx.L, 'WeaponBack')


WEAPONS = {'zahkriisos': zahkriisos, 'nahkriin': nahkriin, 'volsung': volsung, 'otar': otar, 'rahgot': rahgot,
           'krosis': krosis, 'hevnoraak': hevnoraak, 'morokei': morokei}   # (Vahlok's and Vokun's are bows: tools/priestbows.py)

if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    names = [a for a in sys.argv[1:] if not a.startswith('--')] or list(WEAPONS)
    for n in names:
        WEAPONS[n]()
    if '--keep' not in sys.argv:
        shutil.rmtree(WORK, ignore_errors=True)
