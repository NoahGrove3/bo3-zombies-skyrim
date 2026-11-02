"""The Ranger's throwing daggers: the projectile (PROJ, Missile) model - a sleek steel throwing knife, kunai / leaf-blade
style, a dark leather-wrapped grip and a small ring pommel, modelled here from scratch (no one else's mesh). About 30
units long, the point on +Y (as Skyrim's arrow flight models, tools/bowarrows.py's TIP), its bounds centred on the origin;
the whole knife turns end over end about X, 3 turns a second, from load and looping (tools/modelconv/spinwrap: a
NiTransformController on a parent NiNode, the free-running 0x48 controllers + BSX animated bit our other animated models
use - fxwrite, nifanim). No havok, no Prn, no skin. A faint ember glow along the edges (glow map), nothing more.

    python3 tools/throwdagger.py [--preview <png>]
      -> assets/fx/Meshes/CurseOfAlduin/ranger/throwdagger.nif
         assets/fx/Textures/CurseOfAlduin/ranger/throwdagger_{steel,leather}_{d,n}.dds, throwdagger_steel_g.dds

Pipeline: the mesh (explicit normals: crisp blade facets) -> OBJ -> modelconv (lit BSLightingShaderProperty shapes, glow
map shader on the steel) -> spinwrap (the spin node and its controller). build/throwdagger is emptied afterwards.

NIF layout:  BSFadeNode "ThrowDagger" (+ BSXFlags 1: animated)
               NiNode "DaggerSpin"  <- NiTransformController (active|loop) -> NiTransformInterpolator -> NiTransformData
                 BSTriShape Blade:0     steel, glow map (edges)
                 BSTriShape Fittings:1  steel (collars, ring pommel)
                 BSTriShape Grip:2      leather"""
import math, pathlib, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx'
NIF = OUT / 'Meshes' / 'CurseOfAlduin' / 'ranger' / 'throwdagger.nif'
TEXDIR = OUT / 'Textures' / 'CurseOfAlduin' / 'ranger'
TEXREL = 'CurseOfAlduin\\ranger\\'
WORK = ROOT / 'build' / 'throwdagger'
TOOLS = pathlib.Path('/home/claude/tools')
SPIN_TPS = 3.0                      # turns a second, end over end (about X)

# the knife's stations along Y (before centring): the point, the blade's root, the collars, the grip, the ring
TIP, BLADE0 = 16.5, -1.0
COLLAR = (-1.9, -0.45)
GRIP = (-9.25, -1.75)
POMMEL = (-10.15, -8.9)
RING_C, RING_R, RING_r = -11.32, 1.55, 0.36

rng = np.random.default_rng(1729)


# ------------------------------------------------------------------ mesh
class Mesh:
    def __init__(self):
        self.groups = []                              # (group name, material, V, N, UV, F)

    def add(self, group, mat, V, N, UV, F):
        V = np.asarray(V, float).reshape(-1, 3); N = np.asarray(N, float).reshape(-1, 3)
        UV = np.asarray(UV, float).reshape(-1, 2); F = np.asarray(F, int).reshape(-1, 3)
        # every triangle wound counter-clockwise seen from outside (its corners' normals say which side that is)
        a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
        flip = (np.cross(b - a, c - a) * (N[F[:, 0]] + N[F[:, 1]] + N[F[:, 2]])).sum(1) < 0
        F[flip] = F[flip][:, [0, 2, 1]]
        self.groups.append((group, mat, V, N, UV, F))

    def write_obj(self, path):
        with open(path, 'w') as f:
            base = 0
            for g, m, V, N, UV, F in self.groups:
                f.write(f'g {g}\nusemtl {m}\n')
                f.writelines(f'v {x:.5f} {y:.5f} {z:.5f}\n' for x, y, z in V)
                f.writelines(f'vn {x:.5f} {y:.5f} {z:.5f}\n' for x, y, z in N)
                f.writelines(f'vt {u:.5f} {1 - v:.5f}\n' for u, v in UV)   # (image v runs down; modelconv flips it back)
                f.writelines(f'f {a+base}/{a+base}/{a+base} {b+base}/{b+base}/{b+base} {c+base}/{c+base}/{c+base}\n' for a, b, c in F + 1)
                base += len(V)

    def bounds(self):
        A = np.vstack([g[2] for g in self.groups])
        return A.min(0), A.max(0)


def unit(v):
    return v / (np.linalg.norm(v, axis=-1, keepdims=True) + 1e-12)


def grid_faces(nr, nc, wrap=False):
    F = []
    cc = nc if wrap else nc - 1
    for i in range(nr - 1):
        for j in range(cc):
            a, b = i * nc + j, i * nc + (j + 1) % nc
            c, d = a + nc, b + nc
            F += [(a, c, b), (b, c, d)]
    return F


def grid_normals(P, wrap_cols=True, axis_out=None):
    """P (rows along Y, cols round): normals from the surface's two tangents; turned to face away from the Y axis"""
    dr = np.gradient(P, axis=0)
    dc = (np.roll(P, -1, 1) - np.roll(P, 1, 1)) if wrap_cols else np.gradient(P, axis=1)
    N = np.cross(dr, dc)
    out = P.copy(); out[..., 1] = 0                         # radial direction
    if axis_out is not None: out += axis_out
    s = np.sign((N * out).sum(-1, keepdims=True)); s[s == 0] = 1
    N = N * s
    L = np.linalg.norm(N, axis=-1, keepdims=True)
    pole = (L[..., 0] < 1e-6)
    N = N / np.maximum(L, 1e-12)
    if pole.any():                                          # a lathe's closed end: straight along the axis
        ends = np.where(np.arange(P.shape[0])[:, None] < P.shape[0] / 2, -1.0, 1.0) * np.ones(P.shape[:2])
        N[pole] = np.stack([np.zeros_like(ends), ends, np.zeros_like(ends)], -1)[pole]
    return N


def blade(M):
    """the leaf blade: a diamond section (spine, grind lines, edges) lofted from the collar to the point; each facet its
    own strip of corners so the grind lines and spine stay crisp. UV: u across (edges at 0 and 0.7, the spine 0.35), v
    from the root (1) to the point (0)"""
    n = 56
    s = 1 - (1 - np.linspace(0, 1, n)) ** 1.25             # (closer stations toward the point)
    y = BLADE0 + (TIP - BLADE0) * s
    wb, wm, sm = 1.02, 2.45, 0.30
    w = np.where(s < sm, wb + (wm - wb) * np.sin(np.pi / 2 * s / sm),
                 wm * np.clip(1 - np.clip((s - sm) / (1 - sm), 0, 1) ** 1.65, 0, 1) ** 0.82)
    t = 0.46 * (1 - s) ** 0.75 + 0.035 * (1 - s ** 4)
    xs = np.array([1.0, 0.6, 0.0, -0.6, -1.0])              # edge, grind line, spine, grind line, edge
    zs = np.array([0.0, 0.42, 1.0, 0.42, 0.0])
    us = 0.35 * (xs + 1)
    for side in (1, -1):
        for k in range(4):
            V, UV = [], []
            for i in range(n):
                for c in (k, k + 1):
                    V.append((w[i] * xs[c], y[i], side * t[i] * zs[c]))
                    UV.append((us[c], 1 - s[i] * 0.995))
            V = np.array(V)
            # facet normal per quad (from its diagonals: fine where the point closes to nothing), averaged down the strip
            P = V.reshape(n, 2, 3)
            qn = unit(np.cross(P[1:, 1] - P[:-1, 0], P[1:, 0] - P[:-1, 1]))
            mid = (P[1:, 0] + P[1:, 1] + P[:-1, 0] + P[:-1, 1]) / 4
            o = np.stack([mid[:, 0], np.zeros(n - 1), mid[:, 2] + side * 1e-3], 1)
            qn *= np.sign((qn * o).sum(1, keepdims=True) + 1e-9)
            vn = np.zeros((n, 3)); vn[:-1] += qn; vn[1:] += qn
            N = np.repeat(unit(vn), 2, 0)
            M.add('Blade', 'steel', V, N, UV, grid_faces(n, 2))


def lathe(prof, nc, zscale=1.0, xscale=1.0, u0=0.76, u1=0.99):
    """a turned part round Y from (y, r) profile points (elliptical by xscale / zscale); UV in the steel sheet's plain
    strip (u0..u1)"""
    prof = np.asarray(prof, float)
    th = np.linspace(0, 2 * np.pi, nc, endpoint=False)
    P = np.zeros((len(prof), nc, 3))
    P[..., 0] = prof[:, 1:2] * np.cos(th) * xscale
    P[..., 1] = prof[:, 0:1]
    P[..., 2] = prof[:, 1:2] * np.sin(th) * zscale
    N = grid_normals(P)
    # (the seam: one column repeated with u = u1 so the texture doesn't smear back across)
    P = np.concatenate([P, P[:, :1]], 1); N = np.concatenate([N, N[:, :1]], 1)
    uu = u0 + (u1 - u0) * np.linspace(0, 1, nc + 1)
    vv = np.linspace(0.05, 0.95, len(prof))
    UV = np.stack(np.meshgrid(uu, vv), -1)
    return P.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(len(prof), nc + 1)


def fittings(M):
    # the collar the blade rises out of: flattened to the blade, a bead at each end
    c0, c1 = COLLAR
    prof = [(c0 - 0.05, 0.0), (c0, 0.62), (c0 + 0.08, 0.92), (c0 + 0.25, 1.04), (c0 + 0.45, 1.0), (c0 + 0.9, 1.06),
            (c1 - 0.22, 1.12), (c1 - 0.08, 1.02), (c1, 0.7), (c1 + 0.02, 0.0)]
    M.add('Fittings', 'steel', *lathe(prof, 40, zscale=0.62, xscale=1.08))
    # the pommel cap the ring hangs from
    p0, p1 = POMMEL
    prof = [(p0 - 0.02, 0.0), (p0, 0.42), (p0 + 0.12, 0.72), (p0 + 0.35, 0.95), (p1 - 0.45, 1.0), (p1 - 0.2, 0.98),
            (p1 - 0.06, 0.9), (p1, 0.7), (p1 + 0.02, 0.0)]
    M.add('Fittings', 'steel', *lathe(prof, 40, zscale=0.92))
    # the ring pommel, flat in the blade's plane (XY) so it shows through the flat
    nu, nv = 48, 14
    th = np.linspace(0, 2 * np.pi, nu + 1); ph = np.linspace(0, 2 * np.pi, nv + 1)
    T, Pp = np.meshgrid(th, ph, indexing='ij')
    rr = RING_R + RING_r * np.cos(Pp)
    V = np.stack([rr * np.cos(T), RING_C + rr * np.sin(T), RING_r * np.sin(Pp) * 0.85], -1)
    N = unit(np.stack([np.cos(Pp) * np.cos(T), np.cos(Pp) * np.sin(T), np.sin(Pp) / 0.85], -1))
    UV = np.stack([0.76 + 0.23 * Pp / (2 * np.pi), 0.05 + 0.9 * T / (2 * np.pi)], -1)
    M.add('Fittings', 'steel', V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(nu + 1, nv + 1))


PITCH = 0.95   # the wrap's pitch (units a turn)


def grip(M):
    """the leather wrap: a cord wound round in a helix, raised ridges with grooves between (in the shape itself and in
    its textures: u round, v along, the band's phase = u + along / PITCH)"""
    g0, g1 = GRIP
    nr, nc = 72, 28
    yy = np.linspace(g0, g1, nr)
    th = np.linspace(0, 2 * np.pi, nc + 1)
    Y, T = np.meshgrid(yy, th, indexing='ij')
    ph = T / (2 * np.pi) + (Y - g0) / PITCH
    ridge = np.abs(np.sin(np.pi * ph)) ** 0.45               # 0 in the groove, 1 on the cord
    swell = 1 + 0.06 * np.sin(np.pi * (Y - g0) / (g1 - g0))   # (a touch fuller in the middle of the hand)
    r = (0.74 + 0.075 * ridge) * swell
    V = np.stack([r * np.cos(T) * 1.06, Y, r * np.sin(T) * 0.9], -1)
    N = grid_normals(V[:, :-1])
    N = np.concatenate([N, N[:, :1]], 1)
    UV = np.stack([T / (2 * np.pi), 1 - (Y - g0) / (g1 - g0)], -1)
    M.add('Grip', 'leather', V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(nr, nc + 1))


# ------------------------------------------------------------------ textures
def tileable(n, m, beta, seed, aniso=(1.0, 1.0)):
    r = np.random.default_rng(seed)
    fx = np.fft.fftfreq(m)[None, :] * aniso[0]; fy = np.fft.fftfreq(n)[:, None] * aniso[1]
    rad = np.sqrt(fx ** 2 + fy ** 2); rad[0, 0] = 1
    spec = (r.normal(size=(n, m)) + 1j * r.normal(size=(n, m))) / rad ** beta; spec[0, 0] = 0
    o = np.real(np.fft.ifft2(spec)).astype(np.float32)
    return (o - o.min()) / (o.max() - o.min())


def dds(arr, name, alpha):
    TEXDIR.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    a = np.clip(arr, 0, 255).astype(np.uint8)
    tmp = WORK / (name + '.png'); Image.fromarray(a, 'RGBA' if alpha else 'RGB').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', f'dds:compression={"dxt5" if alpha else "dxt1"}', '-define', 'dds:mipmaps=10',
                    str(TEXDIR / name)], check=True)
    return TEXREL + name


def height_to_normal(h, k):
    gy, gx = np.gradient(h)
    n = unit(np.stack([-gx * k, gy * k, np.ones_like(h)], -1))
    return (n * 0.5 + 0.5) * 255


def steel_textures():
    """512 x 1024: the blade's sheet on u 0..0.7 (edges at 0 and 0.7, spine 0.35; the point at the top), plain blackened
    steel for the collars and ring on 0.75..1"""
    H, W = 1024, 512
    v, u = np.mgrid[0:H, 0:W].astype(np.float32)
    u = (u + 0.5) / W; v = (v + 0.5) / H
    s = 1 - v                                                # 0 at the root, 1 at the point
    brushed = tileable(H, W, 1.3, 3, aniso=(1.0, 0.04))      # long streaks down the blade
    fine = tileable(H, W, 0.6, 4)
    x = np.clip(np.abs(u - 0.35) / 0.35, 0, 1)                # 0 spine .. 1 edge
    blade = u < 0.72
    base = 128 + 34 * (brushed - 0.5) + 10 * (fine - 0.5)
    col = np.stack([base, base * 1.02 + 2, base * 1.06 + 5], -1)
    honed = np.clip((x - 0.6) / 0.06, 0, 1)                  # the bright honed bevel past the grind line
    col = col * (1 - honed[..., None]) + (np.stack([188, 190, 194], -1) + 22 * (fine[..., None] - 0.5)) * honed[..., None]
    col *= (1 - 0.18 * np.exp(-((x - 0.6) / 0.012) ** 2))[..., None]          # the grind line, a hair darker
    col *= (1 - 0.25 * np.exp(-(x / 0.018) ** 2) * (s > 0.03) * (s < 0.8))[..., None]   # the spine's crease
    # heat tint near the root (straw to faint blue), and a dark fuller groove etched down the first half of the spine
    tint = np.exp(-((s - 0.05) / 0.07) ** 2)[..., None] * np.array([0.10, -0.02, -0.12]) + \
           np.exp(-((s - 0.15) / 0.06) ** 2)[..., None] * np.array([-0.10, -0.04, 0.10])
    col *= 1 + tint
    fuller = np.exp(-(((u - 0.35) / 0.035) ** 2)) * np.clip((s - 0.06) / 0.04, 0, 1) * np.clip((0.5 - s) / 0.08, 0, 1)
    col *= (1 - 0.35 * fuller)[..., None]
    plain = np.stack([78 + 24 * (fine - 0.5), 80 + 24 * (fine - 0.5), 86 + 24 * (fine - 0.5)], -1) + 18 * (brushed[..., None] - 0.5)
    col = np.where(blade[..., None], col, plain)
    D = dds(col, 'throwdagger_steel_d.dds', False)
    h = brushed * 0.6 + fine * 0.15 - fuller * 2.0
    nrm = height_to_normal(h, 6.0)
    spec = np.where(blade, 175 + 60 * honed, 130) + 15 * (fine - 0.5)
    N = dds(np.dstack([nrm, spec]), 'throwdagger_steel_n.dds', True)
    # the glow: a thin warm ember line on both edges, uneven (a few hotter beads), strongest mid-blade, out by the root
    emb = tileable(H, 64, 1.6, 9, aniso=(1.0, 0.3))[:, (u[0] * 63).astype(int)]
    edge = np.exp(-(((1 - x) * 0.35) / 0.016) ** 2) * blade
    along = np.clip(s / 0.18, 0, 1) * np.clip((1.0 - s) / 0.08, 0.35, 1)
    g = edge * along * (0.35 + 0.65 * np.clip((emb - 0.3) * 1.8, 0, 1))
    g = np.maximum(g, np.asarray(Image.fromarray((g * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3)), np.float32) / 255 * 0.6)
    G = dds(np.stack([g * 255, g * 150, g * 70], -1), 'throwdagger_steel_g.dds', False)
    return D, N, G


def leather_textures():
    """256 x 512: dark leather cord, the wound bands following the grip's helix (phase = u + along / PITCH)"""
    H, W = 512, 256
    v, u = np.mgrid[0:H, 0:W].astype(np.float32)
    u = (u + 0.5) / W; v = (v + 0.5) / H
    along = (1 - v) * (GRIP[1] - GRIP[0])
    ph = u + along / PITCH
    ridge = np.abs(np.sin(np.pi * ph)) ** 0.45
    grain = tileable(H, W, 0.9, 21)
    streak = tileable(H, W, 1.4, 22, aniso=(0.25, 1.0))
    lum = (0.45 + 0.55 * ridge) * (0.82 + 0.3 * grain) * (0.9 + 0.2 * streak)
    col = np.stack([58 * lum, 38 * lum, 27 * lum], -1)
    D = dds(col, 'throwdagger_leather_d.dds', False)
    nrm = height_to_normal(ridge * 1.5 + grain * 0.4, 8.0)
    spec = 40 + 50 * ridge * grain
    N = dds(np.dstack([nrm, spec]), 'throwdagger_leather_n.dds', True)
    return D, N


# ------------------------------------------------------------------ build
def tool(name):
    """the converter in /home/claude/tools, compiled from tools/modelconv/<name>.cpp when missing or older"""
    exe = TOOLS / name; src = ROOT / 'tools' / 'modelconv' / f'{name}.cpp'
    if not exe.exists() or (src.exists() and src.stat().st_mtime > exe.stat().st_mtime):
        subprocess.run(['g++', '-std=c++20', '-O2', f'-I{TOOLS}/nifly/include', f'-I{TOOLS}/nifly/external', str(src),
                        str(TOOLS / 'nifly' / 'build' / 'src' / 'libnifly.a'), '-o', str(exe)], check=True)
    return str(exe)


def build():
    WORK.mkdir(parents=True, exist_ok=True)
    M = Mesh()
    blade(M); fittings(M); grip(M)
    lo, hi = M.bounds()
    obj = WORK / 'throwdagger.obj'; M.write_obj(obj)
    sD, sN, sG = steel_textures()
    lD, lN = leather_textures()
    NIF.parent.mkdir(parents=True, exist_ok=True)
    static = WORK / 'throwdagger_static.nif'
    job = [f'in {obj}', f'out {static}', 'scale 1', 'ground 0', 'offset 0 0 0',
           f'mat steel diffuse={sD} normal={sN} glow={sG} emit=1.0,0.48,0.16 emitmult=1.1 spec=1.6 gloss=95 alpha=0 doublesided=0',
           f'mat leather diffuse={lD} normal={lN} emit=0,0,0 emitmult=0 spec=0.35 gloss=18 alpha=0 doublesided=0']
    jf = WORK / 'throwdagger.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([str(TOOLS / 'modelconv'), str(jf)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip()[-400:])
    if r.returncode: raise SystemExit('modelconv failed')
    r = subprocess.run([tool('spinwrap'), str(static), str(NIF), 'DaggerSpin', '1', '0', '0', str(SPIN_TPS), '24'], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('spinwrap failed')
    rename_root(NIF, 'ThrowDagger')
    size = hi - lo
    print(f'length {size[1]:.1f} (Y), width {size[0]:.1f} (X), thickness {size[2]:.1f} (Z); point on +Y at {TIP - (lo[1] + hi[1]) / 2:.1f}')


def rename_root(nif, name):
    """modelconv names its root 'Scene Root'; ours is named for the knife (a one-off python-side tweak through the same
    nifly: nothing else in the file changes)"""
    src = WORK / 'rootname.cpp'
    src.write_text('#include "NifFile.hpp"\nusing namespace nifly;\nint main(int c,char**v){NifFile n;if(n.Load(v[1]))return 1;'
                   'n.GetRootNode()->name.get()=v[2];return n.Save(v[1]);}\n')
    exe = WORK / 'rootname'
    subprocess.run(['g++', '-std=c++20', '-O1', f'-I{TOOLS}/nifly/include', f'-I{TOOLS}/nifly/external', str(src),
                    str(TOOLS / 'nifly' / 'build' / 'src' / 'libnifly.a'), '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(nif), name], check=True)


# ------------------------------------------------------------------ preview
def nif_shapes(nif):
    d = tempfile.mkdtemp()
    subprocess.run([str(TOOLS / 'nifexport'), str(nif), d], check=True, capture_output=True)
    out = []
    for f in sorted(pathlib.Path(d).glob('shape_*.txt')):
        meta, V, F = {}, [], []
        for l in open(f):
            if l.startswith('v '): V.append([float(x) for x in l.split()[1:6]])
            elif l.startswith('f '): F.append([int(x) for x in l.split()[1:4]])
            else:
                k, _, val = l.rstrip('\n').partition('\t'); meta.setdefault(k, val)
        if V: out.append((meta, np.array(V), np.array(F)))
    shutil.rmtree(d, ignore_errors=True)
    return out


def tex_for(rel):
    if not rel: return None
    rel = rel.replace('\\', '/')
    if rel.lower().startswith('textures/'): rel = rel[9:]
    p = OUT / 'Textures' / rel
    return np.asarray(Image.open(p).convert('RGB'), np.float32) / 255 if p.exists() else None


def rot(ax, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return {'x': np.array([[1, 0, 0], [0, c, -s], [0, s, c]]), 'y': np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]]),
            'z': np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])}[ax]


def render(sh, R, W=360, H=560, span=36.0):
    """point-splat raster (as tools/bowpreview.py): textured, lit, glow added; R turns the model, then x right, y up,
    z toward the viewer"""
    zb = np.full((H, W), -1e9); img = np.zeros((H, W, 3), np.float32)
    sc = H / span
    L = unit(np.array([0.35, 0.55, 0.75])); Hh = unit(L + np.array([0, 0, 1.0]))
    rr = np.random.default_rng(2)
    for meta, V, F in sh:
        P = V[:, :3] @ R.T
        T = P[F]; UV = V[F][:, :, 3:5]
        nrm = unit(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]))
        d = tex_for(meta.get('tex0')); g = tex_for(meta.get('tex2'))
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
        col = samp(d) if d is not None else np.full((len(px), 3), 0.6)
        nn = nrm[rep] * np.sign(nrm[rep][:, 2:3] + 1e-9)     # (facing the viewer)
        dif = 0.28 + 0.72 * np.clip(nn @ L, 0, 1)
        spec = np.clip(nn @ Hh, 0, 1) ** (40 if g is not None else 8) * (0.9 if g is not None else 0.15)
        col = col * dif[:, None] + spec[:, None]
        if g is not None: col = col + samp(g) * 1.1
        o = np.argsort(pz); px, py, pz, col = px[o], py[o], pz[o], col[o]
        upd = pz > zb[py, px]
        zb[py[upd], px[upd]] = pz[upd]; img[py[upd], px[upd]] = col[upd]
    img[zb < -1e8] = np.array([0.09, 0.09, 0.11])
    return np.clip(img, 0, 1)


def preview(path):
    sh = nif_shapes(NIF)
    allV = np.vstack([V[:, :3] for _, V, _ in sh])
    lo, hi = allV.min(0), allV.max(0)
    tip = allV[np.argmax(allV[:, 1])]
    views = [('flat (looking down -Z)', np.eye(3)), ('edge (from +X)', rot('y', -90)),
             ('3/4', rot('x', 20) @ rot('y', -35)),
             ('spin 60deg about X', rot('y', -35) @ rot('x', 60)), ('spin 120deg', rot('y', -35) @ rot('x', 120)),
             ('spin 200deg', rot('y', -35) @ rot('x', 200))]
    W, H = 360, 560
    out = Image.new('RGB', (W * len(views), H + 30), (20, 20, 24))
    dr = ImageDraw.Draw(out)
    for i, (label, R) in enumerate(views):
        out.paste(Image.fromarray((render(sh, R, W, H) * 255).astype(np.uint8)), (i * W, 0))
        dr.text((i * W + 8, 8), label, fill=(235, 235, 235))
        if i < 2:   # the +Y arrow
            x = i * W + W - 30
            dr.line([(x, 120), (x, 40)], fill=(90, 220, 120), width=3); dr.polygon([(x - 7, 50), (x + 7, 50), (x, 34)], fill=(90, 220, 120))
            dr.text((x - 12, 18), '+Y', fill=(90, 220, 120))
    dr.text((8, H + 8), f'throwdagger.nif  bounds X {lo[0]:.1f}..{hi[0]:.1f}  Y {lo[1]:.1f}..{hi[1]:.1f}  Z {lo[2]:.1f}..{hi[2]:.1f}'
            f'  length {hi[1] - lo[1]:.1f}  tip at ({tip[0]:.1f}, {tip[1]:.1f}, {tip[2]:.1f})  spins {SPIN_TPS:g} turns/s about X',
            fill=(200, 200, 200))
    out.save(path)
    print('preview', path, '| bounds', np.round(lo, 2), np.round(hi, 2), '| tip', np.round(tip, 2))


if __name__ == '__main__':
    build()
    if '--preview' in sys.argv:
        preview(sys.argv[sys.argv.index('--preview') + 1])
    shutil.rmtree(WORK, ignore_errors=True)   # (disk is tight: the OBJ, PNGs and the static NIF go)
