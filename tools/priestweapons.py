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
def blade(M, mat, y0, length, width, thick, tip=0.22, fuller=0.0, jag=0.0, seed=0, uv=(0, 1, 0, 1), name='Blade', curve=0.0, ns=120, nx=17):
    """a blade along +Z from z=y0: its half width w(s) (s 0 at the guard, 1 at the point), a lens cross-section thinning to
    sharp edges, an optional fuller groove down its middle (0..1 of its length), optional crystal jags along the edges.
    UV: u across (0..1 left to right), v along (0 at the guard), into the given window of the sheet"""
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
        return body * np.sqrt(np.clip(pt * (2 - pt), 0, 1))
    X = np.linspace(-1, 1, nx)
    V = np.zeros((2, ns, nx, 3)); N = np.zeros_like(V); UV = np.zeros((2, ns, nx, 2))
    for i, s in enumerate(S):
        hw = half(s) + jagged[i] * (1 - s) ** 0.3
        t = thick * (1.0 - 0.45 * s) * 0.5
        for j, x in enumerate(X):
            prof = (1 - abs(x) ** 2.2) ** 0.75
            fz = 0.0
            if fuller and s < fuller:
                fz = 0.38 * t * math.exp(-(x / 0.18) ** 2) * min(1.0, (fuller - s) * 8) * min(1.0, s * 12)
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
    h = 0.35 * hammer + 0.25 * brushed + 0.15 * fine - 0.35 * scr - 0.6 * pits
    lum = 0.8 + 0.2 * (brushed - 0.5) + 0.25 * (big - 0.5) + 0.15 * hammer + 0.2 * scr
    col = np.array(base_rgb, np.float32) * lum[..., None]
    if dark:
        col = col * (1 - dark * 0.7 * low[..., None]) + np.array([20, 18, 18]) * dark * 0.7 * low[..., None]
    rmask = np.clip(rust * (pits * 1.4 + np.clip((mid - 0.6) * 3, 0, 1) * low), 0, 1)
    col = col * (1 - rmask[..., None]) + np.array([96, 50, 28]) * (0.7 + 0.5 * fine[..., None]) * rmask[..., None]
    if etch is not None:
        col = col * (1 - 0.55 * etch[..., None]) + np.array([30, 28, 26]) * 0.55 * etch[..., None]
        h = h - 0.9 * etch
    D = save(col, name + '_d.dds')
    nrm = height_to_normal(gaussian_filter(h, 0.8), 6.5)
    spec = np.clip(170 + 50 * (hammer - 0.5) + 50 * scr - 150 * rmask - 90 * dark * low - (80 * etch if etch is not None else 0), 0, 255)
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
    nrm = height_to_normal(gaussian_filter(0.5 * peb + 0.25 * fine - 0.8 * cr, 0.6), 5.0)
    spec = np.clip(45 + 50 * peb + 40 * big - 30 * cr, 0, 255)
    N = save(np.dstack([nrm, spec]), name + '_n.dds', True)
    return D, N


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


def blade_tex(name, steel_rgb, edge_rgb=None, glow_rgb=None, script=True, pattern='folded', seed=0, ice=False):
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
        spec = np.clip(170 + 70 * edge - 40 * centre + 20 * layers, 0, 255)
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
    D = save(col, name + '_d.dds')
    nrm = height_to_normal(gaussian_filter(h, 0.8), 5.0)
    N = save(np.dstack([nrm, np.clip(spec, 0, 255)]), name + '_n.dds', True)
    G = None
    if glow_rgb is not None:
        G = save(np.array(glow_rgb, np.float32) * gaussian_filter(glow, 1.5)[..., None] * 1.2, name + '_g.dds')
    return D, N, G


# ================================================================== the build
def finish(M, name, mats, fx_lines=None, prn='WeaponSword', root=None):
    """join each material's parts, turn Z up to +Y, write it (modelconv), its effects (fxwrite), its Prn"""
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
    rootname = root or ''.join(w.capitalize() for w in name.split('_'))
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
    bD, bN, bG = blade_tex('zahk_blade', None, glow_rgb=(110, 190, 255), script=True, ice=True, seed=1)
    iD, iN = metal_tex('zahk_iron', (92, 96, 104), rust=0.15, dark=0.6, seed=2)
    lD, lN = leather_tex('zahk_leather', (40, 36, 40), seed=3)
    M = Mesh()
    blade(M, 'ice', 6.5, 62.0, 7.6, 1.5, tip=0.16, fuller=0.0, jag=0.9, seed=11)
    # the guard: ice crystals flaring out and up from a block, like antlers
    M.add('Guard', 'icesolid', *box(np.array([0, 0, 5.4]), 5.0, 1.3, 1.25))
    for side in (-1, 1):   # (crystal shards jutting from the block's faces)
        for k in range(4):
            c = np.array([(k - 1.5) * 2.2, side * 1.2, 5.4 + 0.3 * (k % 2)])
            P, N, UV, F = g.axis_lathe([(0, 0.55), (1.0, 0.35), (1.9 + 0.4 * (k % 2), 0.0)], c, unit(np.array([0.2 * (k - 1.5), side, 0.5])), 6)
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
            'icesolid': f'diffuse={bD} normal={bN} glow={bG} emit=0.6,0.85,1 emitmult=1.2 spec=2.4 gloss=150 alpha=0 doublesided=0',
            'iron': f'diffuse={iD} normal={iN} spec=1.2 gloss=60 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.4 gloss=20 alpha=0 doublesided=0'}
    return finish(M, 'zahkriisos_greatsword', mats, fx.L, 'WeaponBack')


def place(M, src, R=np.eye(3), t=(0, 0, 0)):
    """another mesh's parts turned by R and moved by t, added to M"""
    t = np.asarray(t, float)
    for gname, m, V, N, UV, F in src.groups:
        M.add(gname, m, np.asarray(V) @ R.T + t, np.asarray(N) @ R.T, UV, F)


def rotm(axis, ang):
    axis = unit(np.asarray(axis, float)); x, y, z = axis; c, s_ = math.cos(ang), math.sin(ang); C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s_, x * z * C + y * s_], [y * x * C + z * s_, c + y * y * C, y * z * C - x * s_], [z * x * C - y * s_, z * y * C + x * s_, c + z * z * C]])


VAHLOK_LINKS, VAHLOK_PITCH = 9, 1.75


def vahlok(texonly=False):
    """(Vahlok the Jailor - "Chain flail"; Noah: "Have the chain actually connect to the ball ... make it look more ghostly
    not just a normal chain flail, and chain to move like one I don't want it to be stiff") a jailor's flail: an iron haft
    wrapped in leather, capped with a shackle; from it a chain and a spiked ball that are a ghost of iron - spectral,
    half-seen, glowing cold green-blue - the ball hung from the last link by an eye of its own.

    The chain moves: each link hangs on its own node (VahlokLink0 .. 8, each under the one before; VahlokBall under the
    last), so src/Classes.cpp swings them as a chain under gravity and the weapon's own motion. At rest they droop in an
    arc."""
    iD, iN = metal_tex('vahlok_iron', (88, 86, 84), rust=0.55, dark=0.75, seed=12)
    lD, lN = leather_tex('vahlok_leather', (52, 34, 26), seed=13)
    M = Mesh()
    M.add('Iron', 'iron', *lathe([(-9.0, 0.0), (-9.0, 0.9), (-8.4, 1.2), (-7.8, 1.0), (-7.6, 0.78)] + [(z, 0.74) for z in np.linspace(-7.5, 14.0, 6)] + [(14.2, 0.95), (15.0, 1.05), (15.6, 0.9)], 24))
    grip_wrap(M, 'leather', -7.4, 3.0, 0.8, 0.9)
    for z in (3.3, 8.0, 12.5):
        M.add('Iron', 'iron', *lathe([(z - 0.4, 0.76), (z - 0.3, 0.95), (z + 0.3, 0.95), (z + 0.4, 0.76)], 20))
    sh = [np.array([math.sin(a) * 1.3, 0.0, 15.6 + 1.6 * (1 - math.cos(a)) * 0.8]) for a in np.linspace(-math.pi / 2, math.pi / 2, 14)]
    sh = [np.array([-1.3, 0, 15.4])] + sh + [np.array([1.3, 0, 15.4])]
    M.add('Iron', 'iron', *rod(np.array(sh), 0.32, 10))
    M.add('Iron', 'iron', *rod(np.array([(-1.7, 0, 16.6), (1.7, 0, 16.6)]), 0.3, 10))
    fx = Fx()
    ghost = f'tex=CurseOfAlduin\\priests\\vahlok_iron_d.dds color=0.55,1,0.9,0.85 scale=1.5 blend=alpha falloff=1'
    glow = f'tex={FX["swirl"]} color=0.35,1,0.85,0.7 scale=1.6 blend=add falloff=1 uscroll=3 vscroll=5'
    # the links: each on its node (its pivot where it hangs from the one before), the chain's rest an arc out and down
    top = np.array([0.0, 0.0, 16.6])
    dirs = []
    for k in range(VAHLOK_LINKS):
        s = (k + 0.5) / VAHLOK_LINKS
        dirs.append(unit(np.array([math.cos(s * 2.0) * 1.0, 0.0, 0.55 - 1.6 * s])))
    parent, at = 'VahlokFlail', top
    for k in range(VAHLOK_LINKS):
        name = f'VahlokLink{k}'
        off = at if k == 0 else dirs[k - 1] * VAHLOK_PITCH
        fx.L.append(f'node {name} {parent} {off[0]:.4f} {off[1]:.4f} {off[2]:.4f}')
        d = dirs[k]
        perp = unit(np.cross(d, [0, 1.0, 0])) if k % 2 == 0 else np.array([0, 1.0, 0])
        V, N, UV, F = torus(d * VAHLOK_PITCH * 0.5, 0.62, 0.21, perp, 18, 8, stretch=1.55, long=d)
        fx.mesh(f'LinkGhost{k} {ghost} parent={name}', V, N, UV * 2.0, F)
        V2, N2, UV2, F2 = torus(d * VAHLOK_PITCH * 0.5, 0.62, 0.34, perp, 14, 6, stretch=1.55, long=d)
        fx.mesh(f'LinkGlow{k} {glow} parent={name}', V2, N2, UV2, F2)
        parent, at = name, None
    # the ball, under the last link: an eye ring the last link passes through, the spiked ball beneath it
    d = dirs[-1]
    off = d * VAHLOK_PITCH
    fx.L.append(f'node VahlokBall {parent} {off[0]:.4f} {off[1]:.4f} {off[2]:.4f}')
    eye_c = d * 0.35
    V, N, UV, F = torus(eye_c, 0.55, 0.22, np.array([0, 1.0, 0]) if VAHLOK_LINKS % 2 else unit(np.cross(d, [0, 1.0, 0])), 16, 8, 1.0, long=d)
    fx.mesh(f'BallEye {ghost} parent=VahlokBall', V, N, UV * 2.0, F)
    bc = d * 3.4
    V, F = g.sphere(bc, 2.6, 30, 16)
    fx.mesh(f'BallGhost {ghost} parent=VahlokBall', [p for p, uu, vv, n in V], [n for p, uu, vv, n in V], [(uu * 3, vv * 1.5) for p, uu, vv, n in V], F)
    V, F = g.sphere(bc, 3.0, 26, 13)
    fx.mesh(f'BallGlow {glow} parent=VahlokBall', [p for p, uu, vv, n in V], [n for p, uu, vv, n in V], [(uu, vv) for p, uu, vv, n in V], F)
    k = np.arange(14) + 0.5
    phi = np.arccos(1 - 2 * k / 14); th = np.pi * (1 + 5 ** 0.5) * k
    sV, sN, sUV, sF = [], [], [], []
    for i in range(14):
        sd = np.array([math.sin(phi[i]) * math.cos(th[i]), math.sin(phi[i]) * math.sin(th[i]), math.cos(phi[i])])
        P, Nn, UV, F = g.axis_lathe([(2.3, 0.55), (2.9, 0.42), (3.9, 0.15), (4.6, 0.0)], bc, sd, 8)
        base = len(sV); sV += list(P); sN += list(Nn); sUV += list(UV); sF += [tuple(np.asarray(f) + base) for f in F]
    fx.mesh(f'BallSpikes {ghost} parent=VahlokBall', sV, sN, sUV, sF)
    fx.card('BallCore', FX['core'], bc, 2.2, '0.45,1,0.85,1', 1.8, (4, 4, 8, 0))
    fx.card('BallHalo', FX['glow'], bc, 6.5, '0.35,1,0.8,0.4', 0.9)
    # (the haft's shackle breathing the same cold light)
    fx.card('ShackleGlow', FX['glow'], (0, 0, 16.4), 3.2, '0.35,1,0.8,0.45', 0.9)
    # (the ghost's own wisps drifting up off the ball)
    fx.L.append(f'shape BallWisps tex={FX["swirl"]} color=0.4,1,0.85,0.45 scale=1.2 blend=add falloff=1 uscroll=2 vscroll=5 parent=VahlokBall')
    nu, nv = 18, 6
    for i2 in range(nv + 1):
        s2 = i2 / nv
        for j in range(nu + 1):
            a = 2 * math.pi * j / nu
            r = 2.8 + 1.2 * s2
            p = bc + np.array([r * math.cos(a), r * math.sin(a), -1.0 + 6.0 * s2])
            fx.L.append(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} {j / nu * 2:.4f} {s2:.4f} 1 1 1 {math.sin(math.pi * s2) ** 1.3 * 0.8:.3f}')
            fx.L.append(f'n {math.cos(a):.4f} {math.sin(a):.4f} 0')
    for i2 in range(nv):
        for j in range(nu):
            a = i2 * (nu + 1) + j; b = a + 1; c = a + nu + 1; dd = c + 1
            fx.L += [f't {a} {c} {b}', f't {b} {c} {dd}']
    mats = {'iron': f'diffuse={iD} normal={iN} spec=1.1 gloss=55 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.4 gloss=20 alpha=0 doublesided=0'}
    return finish(M, 'vahlok_flail', mats, fx.L, 'WeaponMace', root='VahlokFlail')


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
        for i2, z in enumerate(np.linspace(L0 + 2.0, L0 + LEN * 0.78, 2)):
            for x in (-0.75, 0.75):
                V.append((x, 0.02 * fy, z)); N.append((0, fy, 0)); UV.append((0.5 + x / 1.5, i2 * 1.0))
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
    im = Image.new('L', (256, 2048), 0); dr = ImageDraw.Draw(im); r = np.random.default_rng(73)
    y = 90
    while y < 2000:
        n = r.integers(2, 4)
        for k in range(n):
            a = r.choice([-1, 0, 1]) * 0.5 + (r.random() - 0.5) * 0.25
            ox = 128 + (k - (n - 1) / 2) * 46
            l = 100 + 50 * r.random()
            dr.line([(ox - math.sin(a) * l * 0.5, y - math.cos(a) * l * 0.5), (ox + math.sin(a) * l * 0.5, y + math.cos(a) * l * 0.5)], fill=255, width=13)
        if r.random() < 0.5: dr.ellipse([118, y + 80, 138, y + 100], fill=255)
        y += 180
    rn = np.asarray(im.filter(ImageFilter.GaussianBlur(2.0)), np.float32)
    glow = np.asarray(im.filter(ImageFilter.GaussianBlur(9.0)), np.float32)
    save(np.dstack([np.full(rn.shape + (3,), 255.0), np.clip(rn + glow * 0.8, 0, 255)]), 'nahk_runes.dds', True)
    stD = save(np.dstack([np.full((256, 256), 205.0), np.full((256, 256), 222.0), np.full((256, 256), 240.0)]) * (0.85 + 0.15 * tileable(256, 256, 1.0, 66)[..., None]), 'nahk_stone_d.dds')
    mats = {'bronze': f'diffuse={bD} normal={bN} spec=1.3 gloss=60 alpha=0 doublesided=0',
            'stone': f'diffuse={stD} normal={bN} glow={stD} emit=0.8,0.9,1 emitmult=1.2 spec=2.5 gloss=200 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.35 gloss=18 alpha=0 doublesided=0'}
    return finish(M, 'nahkriin_mistblade', mats, fx.L, 'WeaponSword')


WEAPONS = {'zahkriisos': zahkriisos, 'vahlok': vahlok, 'nahkriin': nahkriin}

if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    names = [a for a in sys.argv[1:] if not a.startswith('--')] or list(WEAPONS)
    for n in names:
        WEAPONS[n]()
    if '--keep' not in sys.argv:
        shutil.rmtree(WORK, ignore_errors=True)
