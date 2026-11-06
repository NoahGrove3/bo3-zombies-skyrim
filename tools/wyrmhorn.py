"""(Noah: the Horn of the Violet Wyrm - "Held in hand", "Dragon horn": "A curved dark dragon horn bound in bands of
tarnished silver, a violet gem set in the mouthpiece") the Summoner's wonder, modelled here from scratch (no one else's
mesh): a dragon's horn curving from a silver mouthpiece to a wide bell rimmed in silver, ridged with growth rings, bound
with silver bands where the hand holds it; a violet gem set in the mouthpiece's collar.

Passive animation (looping from load): the gem's pulsing violet light and a slow glimmer turning in it; faint violet
wisps breathing from the bell.

Held in the hand as Skyrim's daggers are: on the WEAPON node, the grip at the origin, along +Y (the bell ahead and up,
the mouthpiece behind the fist); Prn WeaponDagger (its place at your hip). The same model is the first-person one.
src/Classes.cpp's blow lifts the hand to bring the mouthpiece (HornLip, a node at its end) to your lips.

    python3 tools/wyrmhorn.py [--preview <png>] [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/summoner/wyrm_horn.nif
         assets/fx/Textures/CurseOfAlduin/summoner/wyrmhorn_{horn,silver}_{d,n}.dds (2048), wyrmhorn_gem_{d,g}.dds"""
import math, pathlib, shutil, subprocess, sys
import numpy as np
from scipy.ndimage import gaussian_filter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gauntlet as g
import mutagenstaff as ms
from throwdagger import Mesh, unit, grid_faces, tileable, height_to_normal, tool, rename_root

ROOT = g.ROOT
MESHDIR = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'summoner'
g.TEXDIR = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'summoner'
g.TEXREL = 'CurseOfAlduin\\summoner\\'
WORK = ROOT / 'build' / 'wyrmhorn'
g.WORK = WORK
TOOLS = g.TOOLS
ms.BRASS_TILE = 5.0

# the horn's line: an arc in the YZ plane (Y ahead, Z up), from the mouthpiece (t 0) to the bell (t 1); the grip at t GRIP
ARC_R, ARC_SWEEP, LENGTH = 26.0, math.radians(78), 36.0
GRIP = 0.34


def centre(t):
    """the horn's centre line at t, the grip at the origin: it rises as it runs ahead, the bell turned up and out"""
    a0 = -ARC_SWEEP * 0.5
    def raw(tt):
        a = a0 + ARC_SWEEP * tt
        return np.array([0.0, ARC_R * math.sin(a), -ARC_R * math.cos(a)])
    p = raw(t) - raw(GRIP)
    # a gentle twist out of the plane, as a real horn grows
    p[0] += 1.6 * math.sin(math.pi * t) * (t - GRIP)
    return p


def radius(t):
    return 0.52 + 3.6 * t ** 1.75


def frames(ts):
    P = np.array([centre(t) for t in ts])
    T = unit(np.gradient(P, axis=0))
    N0 = unit(np.cross(T[0], [1.0, 0, 0]))
    out = []
    n = N0
    for k in range(len(ts)):
        n = unit(n - T[k] * (n @ T[k]))
        out.append((n, np.cross(T[k], n)))
    return P, T, out


def horn(M):
    """the horn itself: swept round its centre line, its girth ridged with growth rings (bigger toward the root), open at
    the bell with a lip turned inside. UV: u round, v along (the horn sheet stretched once along the horn)"""
    ts = np.linspace(0.05, 1.0, 160)
    P, T, fr = frames(ts)
    nc = 40
    a = np.linspace(0, 2 * np.pi, nc + 1)
    V = np.zeros((len(ts), nc + 1, 3)); N = np.zeros_like(V)
    for k, t in enumerate(ts):
        ring = 0.04 * radius(t) * (0.5 + 0.5 * math.cos(t * 2 * math.pi * 26)) * (1 - t) ** 0.5
        r = radius(t) + ring
        n1, n2 = fr[k]
        for j, x in enumerate(a):
            d = n1 * math.cos(x) + n2 * math.sin(x)
            # (a slight flattening: horn is a little oval)
            V[k, j] = P[k] + d * r * (1.0 - 0.08 * math.sin(x) ** 2)
            N[k, j] = d
    UV = np.stack(np.meshgrid(np.linspace(0, 1, nc + 1), np.linspace(0, 1, len(ts))), -1)
    M.add('Horn', 'horn', V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(len(ts), nc + 1))
    # the bell's inside: a short sleeve turned in from the rim, darker (the same sheet's end)
    k = len(ts) - 1
    inner = []
    for s in np.linspace(0, 1, 8):
        tt = 1.0 - 0.12 * s
        c = centre(tt); t_ = unit(centre(min(1.0, tt + 0.01)) - centre(tt - 0.01))
        n1, n2 = fr[int((len(ts) - 1) * (tt - 0.05) / 0.95)]
        inner.append([c + (n1 * math.cos(x) + n2 * math.sin(x)) * radius(tt) * 0.9 for x in a])
    inner = np.array(inner)
    Ni = np.zeros_like(inner)
    for i2 in range(inner.shape[0]):
        cc = inner[i2].mean(0)
        Ni[i2] = -unit(inner[i2] - cc)
    UVi = np.stack(np.meshgrid(np.linspace(0, 1, nc + 1), np.linspace(0.97, 0.99, inner.shape[0])), -1)
    Fi = np.array(grid_faces(inner.shape[0], nc + 1))[:, [0, 2, 1]]
    M.add('Horn', 'horn', inner.reshape(-1, 3), Ni.reshape(-1, 3), UVi.reshape(-1, 2), Fi)
    return P, fr


def band_at(M, t, w, lift, profile='band'):
    """a silver band round the horn at t (w its half width along, lift its thickness off the horn)"""
    nc = 40
    a = np.linspace(0, 2 * np.pi, nc + 1)
    rows = np.linspace(-1, 1, 9)
    ts = [t + w * q / LENGTH for q in rows * 1.0]
    P, T, fr = frames(np.clip(ts, 0.0, 1.0))
    V = np.zeros((len(rows), nc + 1, 3)); N = np.zeros_like(V)
    for k, q in enumerate(rows):
        bump = lift * (1 - abs(q) ** 4) ** 0.6 + (0.06 if profile == 'beaded' and abs(q) < 0.3 else 0)
        r = radius(ts[k]) + 0.04 + bump
        n1, n2 = fr[k]
        for j, x in enumerate(a):
            d = n1 * math.cos(x) + n2 * math.sin(x)
            V[k, j] = P[k] + d * r * (1.0 - 0.08 * math.sin(x) ** 2)
            N[k, j] = unit(d - T[k] * (q * lift * 1.5))
    circ = 2 * np.pi * (radius(t) + lift)
    UV = np.stack(np.meshgrid(np.linspace(0, circ / ms.BRASS_TILE, nc + 1), np.linspace(0, 2 * w / ms.BRASS_TILE, len(rows))), -1)
    UV = UV + np.random.default_rng(int(t * 1000)).random(2)
    M.add('Silver', 'silver', V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(len(rows), nc + 1))


def mouthpiece(M):
    """the silver mouthpiece: a cup and a collar over the horn's root, the gem's setting on its top. Returns the lip's
    point (its end) and the gem's place and facing"""
    t0 = 0.05
    c0 = centre(t0)
    tdir = unit(centre(t0) - centre(t0 + 0.01))         # (pointing out of the horn's root)
    # turned about the line through c0 along tdir: (h along, r)
    prof = [(-0.4, radius(t0) + 0.02), (-0.2, radius(t0) + 0.2), (0.4, radius(t0) + 0.24), (0.7, 0.62), (1.4, 0.46),
            (2.2, 0.48), (2.5, 0.72), (2.8, 0.78), (2.95, 0.6), (2.9, 0.32), (2.6, 0.0)]
    P, N, UV, F = g.axis_lathe([(h, r) for h, r in prof], c0, tdir, 32)
    UV = UV * [2 * np.pi * 0.8 / ms.BRASS_TILE, 3.4 / ms.BRASS_TILE] + 0.37
    M.add('Silver', 'silver', P, N, UV, F)
    lip = c0 + tdir * 2.95
    # the gem's setting: on the collar's top (the side away from the palm: +Z there), a little bezel
    up = unit(np.cross(tdir, [1.0, 0, 0]))
    if up[2] < 0: up = -up
    gc = c0 + tdir * 0.05 + up * (radius(t0) + 0.32)
    P, N, UV, F = g.axis_lathe([(-0.25, 0.0), (-0.25, 0.8), (0.05, 0.85), (0.2, 0.74), (0.14, 0.64)], gc, up, 24)
    UV = UV * [0.6, 0.15] + 0.11
    M.add('Silver', 'silver', P, N, UV, F)
    # the gem: a faceted violet stone (a low dome of eight facets)
    facets = []
    nf = 8
    top = gc + up * 0.55
    xax = unit(np.cross(up, tdir)); yax = np.cross(up, xax)
    ring1 = [gc + up * 0.1 + (xax * math.cos(2 * math.pi * k / nf) + yax * math.sin(2 * math.pi * k / nf)) * 0.62 for k in range(nf)]
    ring2 = [gc + up * 0.3 + (xax * math.cos(2 * math.pi * (k + 0.5) / nf) + yax * math.sin(2 * math.pi * (k + 0.5) / nf)) * 0.4 for k in range(nf)]
    V, Nn, UVg, Fg = [], [], [], []
    def tri(a, b, c):
        n = unit(np.cross(b - a, c - a))
        if n @ up < -0.2: a, b = b, a; n = -n
        base = len(V)
        for p in (a, b, c):
            V.append(p); Nn.append(n); UVg.append((0.5 + 0.5 * (p - gc) @ xax, 0.5 + 0.5 * (p - gc) @ yax))
        Fg.append((base, base + 1, base + 2))
    for k in range(nf):
        a1, b1 = ring1[k], ring1[(k + 1) % nf]
        a2, b2 = ring2[k - 1 if k else nf - 1], ring2[k]
        tri(a1, b1, b2); tri(a1, b2, a2)
        tri(ring2[k], ring2[(k + 1) % nf], top)
    M.add('Gem', 'gem', np.array(V), np.array(Nn), np.array(UVg), Fg)
    return lip, top, up


def bell_rim(M):
    """the bell's silver rim: a rolled lip round the mouth, beaded, and four small dragon-claw clasps over it"""
    t = 1.0
    P, T, fr = frames(np.array([0.995, 1.0]))
    c = P[-1]; n1, n2 = fr[-1]
    a = np.linspace(0, 2 * np.pi, 121)
    ring = np.array([c + (n1 * math.cos(x) + n2 * math.sin(x)) * radius(t) * (1.0 - 0.08 * math.sin(x) ** 2) for x in a])
    M.add('Silver', 'silver', *ms.btube(ring, 0.32, 10))
    for k in range(4):
        x = math.pi / 4 + k * math.pi / 2
        d = n1 * math.cos(x) + n2 * math.sin(x)
        pts = []
        for s in np.linspace(0, 1, 10):
            tt = 1.0 - 0.09 * (1 - s)
            cc = centre(tt)
            pts.append(cc + d * (radius(tt) * (1.0 - 0.08 * math.sin(x) ** 2) + 0.12 + 0.15 * math.sin(math.pi * s)))
        M.add('Silver', 'silver', *ms.btube(np.array(pts), np.linspace(0.12, 0.22, 10), 8))


# ------------------------------------------------------------------ textures
def horn_tex():
    """2048: dark dragon horn - near black at the root warming to a deep oxblood-brown at the bell, long fibrous
    striations along it, growth rings across it, a polished lustre worn into the grip's middle, pale nicks"""
    S = 2048
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    fibre = tileable(S, S, 0.9, 121, aniso=(0.03, 1.0))     # (streaks running along v)
    fibre2 = tileable(S, S, 0.5, 122, aniso=(0.08, 1.0))
    warp = tileable(S, S, 2.0, 123)
    rings = 0.5 + 0.5 * np.cos(2 * np.pi * (v * 26 + 0.15 * warp))
    big = tileable(S, S, 2.2, 124)
    rng = np.random.default_rng(125)
    nick = np.zeros((S, S), np.float32)
    for _ in range(160):
        cu, cv = rng.random(), rng.random()
        du = np.minimum(np.abs(u - cu), 1 - np.abs(u - cu)); dv = np.abs(v - cv)
        nick = np.maximum(nick, np.exp(-((du / 0.004) ** 2 + (dv / 0.0016) ** 2)) * rng.random())
    root = np.clip(1 - v * 1.3, 0, 1)
    colA = np.array([16, 12, 14], np.float32); colB = np.array([50, 30, 30], np.float32)
    base = colA * root[..., None] + colB * (1 - root[..., None])
    lum = 0.75 + 0.35 * (fibre - 0.5) + 0.2 * (fibre2 - 0.5) + 0.12 * rings + 0.2 * (big - 0.5)
    col = base * lum[..., None]
    col = col + np.array([120, 104, 92]) * nick[..., None] * 0.6
    grip = np.exp(-((v - 0.3) / 0.08) ** 2)
    col = col * (1 + 0.15 * grip[..., None])
    D = g.save(col, 'wyrmhorn_horn_d.dds')
    h = 0.5 * fibre + 0.3 * fibre2 + 0.6 * rings - 0.8 * nick
    nrm = height_to_normal(gaussian_filter(h, 0.7), 5.0)
    spec = np.clip(110 + 70 * grip + 40 * (fibre - 0.5) - 60 * nick, 0, 255)
    N = g.save(np.dstack([nrm, spec]), 'wyrmhorn_horn_n.dds', True)
    return D, N


def gem_tex():
    """512: a violet stone with darker inclusions; its glow the same, brighter at the heart"""
    S = 512
    yy, xx = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    inc = tileable(S, S, 1.4, 131)
    r = np.hypot(xx - 0.5, yy - 0.5) * 2
    lum = 0.55 + 0.45 * np.clip(1 - r, 0, 1) - 0.25 * (inc - 0.5)
    col = np.array([150, 70, 235], np.float32) * lum[..., None]
    D = g.save(col, 'wyrmhorn_gem_d.dds')
    G = g.save(col * 0.9, 'wyrmhorn_gem_g.dds')
    return D, G


def fx_job(base, out, gemtop, gemup):
    L = [f'base {base}', f'out {out}']
    tex = g.FX
    def card(name, t, at, hs, col, sc, flip=None):
        extra = f' flip={flip[0]}x{flip[1]} fps={flip[2]} phase={flip[3]}' if flip else ''
        L.append(f'shape {name} tex={t} color={col} scale={sc} blend=add billboard=1 at={at[0]:.3f},{at[1]:.3f},{at[2]:.3f}{extra}')
        cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
        for X, Y, uu, vv in ((-hs, hs, 0, 0), (hs, hs, cu, 0), (hs, -hs, cu, cv), (-hs, -hs, 0, cv)):
            L.append(f'v {X:.3f} {Y:.3f} 0 {uu} {vv} 1 1 1 1')
        L.extend(['t 0 1 2', 't 0 2 3'])
    card('GemGlow', tex['core'], gemtop - gemup * 0.1, 0.9, '0.68,0.36,1,1', 2.0, (4, 4, 8, 0))
    card('GemHalo', tex['glow'], gemtop, 2.0, '0.6,0.3,1,0.45', 0.9)
    # wisps breathing from the bell: a short open sleeve ahead of the mouth, the motes texture rising slowly out of it
    c1 = centre(1.0); t1 = unit(centre(1.0) - centre(0.97))
    n1 = unit(np.cross(t1, [1.0, 0, 0])); n2 = np.cross(t1, n1)
    L.append(f'shape BellWisps tex={tex["swirl"]} color=0.62,0.32,1,0.55 scale=1.4 blend=add falloff=1 uscroll=4 vscroll=6')
    nu, nv = 24, 6
    for i in range(nv + 1):
        s = i / nv
        for j in range(nu + 1):
            x = 2 * math.pi * j / nu
            r = radius(1.0) * (0.8 + 0.5 * s)
            p = c1 + t1 * (0.3 + 4.0 * s) + (n1 * math.cos(x) + n2 * math.sin(x)) * r
            n = n1 * math.cos(x) + n2 * math.sin(x)
            fade = math.sin(math.pi * s) ** 1.2 * 0.9
            L.append(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} {j / nu * 2:.4f} {s:.4f} 1 1 1 {fade:.3f}')
            L.append(f'n {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}')
    for i in range(nv):
        for j in range(nu):
            a = i * (nu + 1) + j; b = a + 1; cc = a + nu + 1; d = cc + 1
            L += [f't {a} {cc} {b}', f't {b} {cc} {d}']
    return L


def build(tex, lip):
    M = Mesh()
    horn(M)
    for t, w, lift in ((0.2, 0.55, 0.16), (GRIP - 0.07, 0.42, 0.14), (GRIP + 0.09, 0.42, 0.14), (0.62, 0.7, 0.18), (0.86, 0.5, 0.15)):
        band_at(M, t, w, lift, 'beaded' if t > 0.5 else 'band')
    lipp, gemtop, gemup = mouthpiece(M)
    bell_rim(M)
    merged = {}
    for gname, m, V, N, UV, F in M.groups:
        if m not in merged: merged[m] = [gname, m, [], [], [], [], 0]
        e = merged[m]; e[2].append(V); e[3].append(N); e[4].append(UV); e[5].append(np.asarray(F) + e[6]); e[6] += len(V)
    M.groups = [(m.capitalize(), m, np.vstack(e[2]), np.vstack(e[3]), np.vstack(e[4]), np.vstack(e[5])) for m, e in merged.items()]
    WORK.mkdir(parents=True, exist_ok=True)
    obj = WORK / 'wyrm_horn.obj'; M.write_obj(obj)
    static = WORK / 'wyrm_horn_static.nif'
    hD, hN = tex['horn']; sD, sN = tex['silver']; gD, gG = tex['gem']
    job = [f'in {obj}', f'out {static}', 'scale 1', 'ground 0', 'origin 1', 'offset 0 0 0',
           f'mat horn diffuse={hD} normal={hN} spec=1.0 gloss=60 alpha=0 doublesided=0',
           f'mat silver diffuse={sD} normal={sN} spec=1.8 gloss=95 alpha=0 doublesided=0',
           f'mat gem diffuse={gD} normal={hN} glow={gG} emit=0.7,0.4,1 emitmult=2.2 spec=3.0 gloss=220 alpha=0 doublesided=0']
    jf = WORK / 'horn.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([str(TOOLS / 'modelconv'), str(jf)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('modelconv failed')
    import throwdagger; throwdagger.WORK.mkdir(parents=True, exist_ok=True)
    rename_root(static, 'WyrmHorn')
    MESHDIR.mkdir(parents=True, exist_ok=True)
    nif = MESHDIR / 'wyrm_horn.nif'
    L = fx_job(static, nif, gemtop, gemup)
    # (the lip: a node at the mouthpiece's end, for the blow to bring to your lips)
    L.insert(2, f'node HornLip WyrmHorn {lipp[0]:.4f} {lipp[1]:.4f} {lipp[2]:.4f}')
    (WORK / 'fx.job').write_text('\n'.join(g.fx_fix(L)) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / 'fx.job')], capture_output=True, text=True)
    if r.returncode or 'no node' in r.stderr: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed')
    subprocess.run([str(TOOLS / 'addprn'), str(nif), str(nif), 'WeaponDagger'], check=True)
    lo, hi = M.bounds()
    print(f'{nif.name}: x {lo[0]:.1f}..{hi[0]:.1f}  y {lo[1]:.1f}..{hi[1]:.1f}  z {lo[2]:.1f}..{hi[2]:.1f}; {sum(len(gg[5]) for gg in M.groups)} triangles; lip at {np.round(lipp, 2)}')
    return M, obj


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    g.FX.clear()   # (the Mutagen Staff's effect textures, shared: tools/gauntlet.py writes them)
    g.FX.update({k: 'CurseOfAlduin\\alchemist\\gauntlet_fx_' + k + '.dds' for k in ('core', 'glow', 'swirl')})
    tex = {'horn': horn_tex(), 'gem': gem_tex(),
           'silver': ms.brass_tex('wyrmhorn_silver', (178, 176, 170), (52, 46, 56))}
    build(tex, None)
    if '--keep' not in sys.argv:
        shutil.rmtree(WORK, ignore_errors=True)
