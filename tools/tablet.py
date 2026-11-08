"""(Noah: "an extremely detailed floating tablet with 4k textures and hyper realistic animations called the Tablet Of
Mysteries"; "used and forgotten / donated weapons ... more run-down or disheveled") the mod's mystery box, modelled here
from scratch (no one else's mesh): an old arched stone tablet floating over a cracked plinth. Its face is carved with a
border of Dragon-tongue glyphs round a round recess - the light pool - where two glyph rings sit (they grind round during
a roll) ringed by eight small gems (they light one by one). It is split by a crack, patched with rusted iron straps and
rivets; its top corner has broken off and floats beside it. Candle stubs, old coins and folded notes from past owners sit
on the plinth and in the cracks.

Docs: docs/TABLET_OF_MYSTERIES.md. Driven by src (the Tablet): by name -
  TabletBody      the floating slab and all that rides on it (bobs, turns slowly; shakes in a roll)
  RingOuter/RingInner  the glyph rings (turned about the pool's centre, unevenly)
  LooseCorner     the broken corner (drifts, turns)
  Gem0..Gem7      the gems' glow cards (hidden until lit)
  Spark0..Spark5  sparks spitting from the crack (hidden until a roll)
  Ghost           the weapons' ghostly outlines flickering in the pool (hidden until a roll)
  GoldFlare       a battered (stronger) win's gold light (hidden)
  Pool            the pool's own glow (always: a slow swirl)
Looping from load: the pool's swirl, the motes round the tablet, the candles' flames, the glyphs' uneven breathing.

    python3 tools/tablet.py [--preview <png>] [--size 4096]
      -> assets/fx/Meshes/CurseOfAlduin/tablet/tablet.nif
         assets/fx/Textures/CurseOfAlduin/tablet/*.dds (4096: the face, the stone, the rings; 2048: iron, wax, paper)

Built Z up, the face toward -Y (the stand turns it to face the street)."""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import gaussian_filter, map_coordinates
from scipy.spatial import Delaunay
from matplotlib.path import Path as MPath

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import throwdagger as td
from throwdagger import Mesh, unit, grid_faces, tileable, height_to_normal, rename_root

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx'
MESHDIR = OUT / 'Meshes' / 'CurseOfAlduin' / 'tablet'
TEXDIR = OUT / 'Textures' / 'CurseOfAlduin' / 'tablet'
TEXREL = 'CurseOfAlduin\\tablet\\'
WORK = ROOT / 'build' / 'tablet'
TOOLS = pathlib.Path('/home/claude/tools')
rng = np.random.default_rng(4242)
SIZE = int(sys.argv[sys.argv.index('--size') + 1]) if '--size' in sys.argv else 4096

# ---- the shape (units: Skyrim's, about 1.43 cm each)
W2, H = 28.0, 84.0          # the slab's half-width and height (its arch's top at Z1)
Z0 = 74.0                   # the slab's foot, floating over the plinth's top (42)
Z1 = Z0 + H
T2 = 4.6                    # half its thickness
ARCH = 22.0                 # the arch's rise over its shoulders
PC = np.array([0.0, Z0 + 42.0])   # the pool's centre on the face (x, z)
POOL_R = 18.0               # the recess
RING_O = (14.6, 17.2)       # the outer glyph ring (inner, outer radius)
RING_I = (9.4, 12.6)        # the inner one
GEM_R = 20.6                # the gems sit round the recess
PLINTH_TOP = 42.0
CRACK = np.array([[-6.0, Z1 + 1], [-3.5, Z1 - 12], [-8.0, Z1 - 22], [-4.0, PC[1] + POOL_R + 1]])   # down to the recess's rim
CRACK2 = np.array([[3.0, PC[1] - POOL_R - 1], [6.5, PC[1] - 28], [2.0, Z0 + 8], [5.0, Z0 - 1]])
# the broken corner: the cut runs from the arch's right shoulder in across the top
CUT = np.array([[W2 + 1, Z1 - ARCH - 6], [20.0, Z1 - ARCH - 1], [16.5, Z1 - ARCH + 7], [19.0, Z1 - 7], [14.0, Z1 + 2]])


def arch_outline(n=90):
    """the whole slab's outline (x, z), counter-clockwise from its bottom left"""
    pts = [(-W2, Z0), (W2, Z0), (W2, Z1 - ARCH)]
    for t in np.linspace(0, math.pi, n)[1:-1]:
        pts.append((W2 * math.cos(t), Z1 - ARCH + ARCH * math.sin(t)))
    pts.append((-W2, Z1 - ARCH))
    return np.array(pts)


def jag(path, amp, n, seed):
    """a broken line: the path resampled and shaken (a fracture, not a cut)"""
    r = np.random.default_rng(seed)
    seg = np.diff(path, axis=0); L = np.concatenate([[0], np.cumsum(np.linalg.norm(seg, axis=1))])
    s = np.linspace(0, L[-1], n)
    P = np.stack([np.interp(s, L, path[:, 0]), np.interp(s, L, path[:, 1])], 1)
    d = np.gradient(P, axis=0); nrm = np.stack([-d[:, 1], d[:, 0]], 1); nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-9
    off = gaussian_filter(r.normal(0, 1, n), 1.2) * amp
    off[0] = off[-1] = 0
    return P + nrm * off[:, None]


def split_outline():
    """the slab's outline with its corner broken off, and the corner piece's own outline (both counter-clockwise)"""
    arch = arch_outline(240)
    # resample the outline evenly so the break's ends land on it
    seg = np.linalg.norm(np.diff(np.vstack([arch, arch[:1]]), axis=0), axis=1)
    L = np.concatenate([[0], np.cumsum(seg)]); s_ = np.arange(0, L[-1], 0.6)
    closed = np.vstack([arch, arch[:1]])
    O = np.stack([np.interp(s_, L, closed[:, 0]), np.interp(s_, L, closed[:, 1])], 1)
    ia = int(np.argmin(np.linalg.norm(O - CUT[0], axis=1)))      # on the right side, under the shoulder
    ib = int(np.argmin(np.linalg.norm(O - CUT[-1], axis=1)))     # on the arch, right of its top
    inner = CUT[1:-1]
    cut = jag(np.vstack([O[ia], inner, O[ib]]), 0.9, 44, 7)
    cut[0], cut[-1] = O[ia], O[ib]
    corner = np.vstack([O[ia:ib + 1], cut[::-1][1:-1]])           # the outline from A on round to B, back along the break
    slab = np.vstack([O[ib:], O[:ia + 1], cut[1:-1]])              # from B on round to A, then along the break to B
    return slab, corner, cut, MPath(arch)


def fill(outline, step):
    """a triangulated flat region inside an outline (its border kept as given): points and triangles"""
    P = MPath(outline)
    lo, hi = outline.min(0), outline.max(0)
    gx, gz = np.meshgrid(np.arange(lo[0] + step / 2, hi[0], step), np.arange(lo[1] + step / 2, hi[1], step))
    G = np.stack([gx.ravel(), gz.ravel()], 1)
    G = G + rng.normal(0, step * 0.12, G.shape)
    inside = P.contains_points(G, radius=-step * 0.35)
    # (the pool's lip is steep: rings of points round it so its slope is smooth, not sawn)
    rp = np.hypot(G[:, 0] - PC[0], G[:, 1] - PC[1])
    inside &= np.abs(rp - POOL_R) > 1.6
    ring = []
    for rr in np.linspace(POOL_R - 1.5, POOL_R + 1.5, 9):
        a = np.linspace(0, 2 * np.pi, int(2 * np.pi * rr / 0.45), endpoint=False)
        ring.append(np.stack([PC[0] + rr * np.cos(a), PC[1] + rr * np.sin(a)], 1))
    ring = np.vstack(ring); ring = ring[P.contains_points(ring, radius=-0.3)]
    pts = np.vstack([outline, G[inside], ring])
    tri = Delaunay(pts).simplices
    cen = pts[tri].mean(1)
    tri = tri[P.contains_points(cen)]
    return pts, tri


def slab_mesh(M, outline, name, mat_face, mat_side, front_disp, step=1.1, y0=0.0):
    """a slab of stone: its face (-Y) and back (+Y) filled over the outline, its sides a bevelled strip round it.
    front_disp(x, z) -> how far into the stone the face is carved there (the recess, chips)"""
    pts, tri = fill(outline, step)
    nb = len(outline)
    # the face: planar uv over the whole tablet (the face texture is painted to it: the border, the crack, the glyphs)
    def uv(x, z): return np.stack([(x + W2) / (2 * W2), 1 - (z - Z0) / H], 1)
    d = front_disp(pts[:, 0], pts[:, 1])
    d[:nb] = 0.0
    Vf = np.stack([pts[:, 0], y0 - T2 + d, pts[:, 1]], 1)
    # normals of the carved face: from the displacement's slope
    eps = 0.15
    dx = (front_disp(pts[:, 0] + eps, pts[:, 1]) - front_disp(pts[:, 0] - eps, pts[:, 1])) / (2 * eps)
    dz = (front_disp(pts[:, 0], pts[:, 1] + eps) - front_disp(pts[:, 0], pts[:, 1] - eps)) / (2 * eps)
    Nf = unit(np.stack([dx, -np.ones_like(dx), dz], 1))
    M.add(name + 'Face', mat_face, Vf, Nf, uv(pts[:, 0], pts[:, 1]), tri)
    Vb = np.stack([pts[:, 0], np.full(len(pts), y0 + T2), pts[:, 1]], 1)
    M.add(name + 'Back', mat_side, Vb, np.tile([0, 1, 0], (len(pts), 1)), np.stack([pts[:, 0] / 40 + 0.3, pts[:, 1] / 40], 1), tri[:, [0, 2, 1]])
    # the sides: the outline swept from face to back through a rounded bevel, chipped
    o = outline
    t = np.gradient(o, axis=0); t /= np.linalg.norm(t, axis=1, keepdims=True) + 1e-9
    out2 = np.stack([t[:, 1], -t[:, 0]], 1)
    cen = o.mean(0)
    if (np.sum(out2 * (o - cen), 1) < 0).mean() > 0.5: out2 = -out2
    chip = np.clip(gaussian_filter(rng.normal(0, 1, len(o)), 2.0), -1, 1) * 0.5
    rows = []
    prof = [(-1.0, 0.9), (-0.86, 0.35), (-0.6, 0.06), (0.0, 0.0), (0.6, 0.06), (0.86, 0.35), (1.0, 0.9)]   # (y in T2, inset)
    for yy, ins in prof:
        inset = ins + np.abs(chip) * (abs(yy) > 0.7) * 1.4
        P2 = o - out2 * inset[:, None] if np.ndim(inset) else o - out2 * inset
        rows.append(np.stack([P2[:, 0], np.full(len(o), y0 + yy * T2), P2[:, 1]], 1))
    R = np.stack(rows)                       # (rows, n, 3)
    R[0, :, 1] = y0 - T2 + 0.0
    Vs = R.reshape(-1, 3)
    Ns = []
    for k, (yy, ins) in enumerate(prof):
        ny = -0.9 if yy <= -0.86 else 0.9 if yy >= 0.86 else yy * 0.4
        n3 = np.stack([out2[:, 0], np.full(len(o), ny), out2[:, 1]], 1)
        Ns.append(unit(n3))
    Ns = np.vstack(Ns)
    L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(o, axis=0), axis=1))])
    UVs = np.stack(np.meshgrid(L / 40.0, np.linspace(0, T2 * 2, len(prof)) / 40.0), -1).reshape(-1, 2)
    F = grid_faces(len(prof), len(o), wrap=True)
    M.add(name + 'Side', mat_side, Vs, Ns, UVs, F)


def recess(x, z):
    """the face's carving: the round pool sunk 1.6 deep with a sloped lip, the glyph border a hair proud"""
    r = np.hypot(x - PC[0], z - PC[1])
    d = 1.6 * np.clip((POOL_R + 0.8 - r) / 1.6, 0, 1) ** 1.4
    return d


def annulus(r0, r1, y, nr=6, nc=180, c=PC, u0=0.0):
    rr = np.linspace(r0, r1, nr); th = np.linspace(0, 2 * np.pi, nc + 1)
    R, TH = np.meshgrid(rr, th, indexing='ij')
    V = np.stack([c[0] + R * np.cos(TH), np.full(R.shape, y), c[1] + R * np.sin(TH)], -1).reshape(-1, 3)
    N = np.tile([0, -1, 0], (len(V), 1)).astype(float)
    # polar uv into the ring texture: u round, v across
    UV = np.stack([u0 + TH / (2 * np.pi), (R - r0) / (r1 - r0)], -1).reshape(-1, 2)
    return V, N, UV, grid_faces(nr, nc + 1)


def ring_mesh(M, name, r0, r1, y, vband):
    V, N, UV, F = annulus(r0, r1, y, 8, 200)
    UV[:, 1] = vband[0] + UV[:, 1] * (vband[1] - vband[0])
    UV[:, 0] *= 4.0     # (four repeats of the glyph strip round the ring)
    M.add(name, 'ring', V, N, UV, F)
    # its outer and inner rims: thin walls so it stands proud of the pool
    for rr, s in ((r1, 1), (r0, -1)):
        th = np.linspace(0, 2 * np.pi, 201)
        Vw = np.vstack([np.stack([PC[0] + rr * np.cos(th), np.full(201, y), PC[1] + rr * np.sin(th)], 1),
                        np.stack([PC[0] + rr * np.cos(th), np.full(201, y + 0.7), PC[1] + rr * np.sin(th)], 1)])
        Nw = np.vstack([np.stack([np.cos(th) * s, np.zeros(201), np.sin(th) * s], 1)] * 2)
        UVw = np.vstack([np.stack([th * 4 / (2 * np.pi), np.full(201, vband[1] + 0.01)], 1)] * 2)
        M.add(name, 'ring', Vw, Nw, UVw, grid_faces(2, 201))


def lathe_y(prof, c, nc=16, axis='z'):
    """a turned part: profile (h, r) along Z (axis 'z') or along -Y (axis 'y', out of the face) about centre c"""
    prof = np.asarray(prof, float); th = np.linspace(0, 2 * np.pi, nc + 1)
    P = np.zeros((len(prof), nc + 1, 3)); N = np.zeros_like(P)
    dh = np.gradient(prof[:, 0]); dr = np.gradient(prof[:, 1]); l = np.hypot(dh, dr) + 1e-9
    nr, nh = dh / l, -dr / l
    a, b = np.cos(th), np.sin(th)
    if axis == 'z':
        P[..., 0] = c[0] + prof[:, 1:2] * a; P[..., 1] = c[1] + prof[:, 1:2] * b; P[..., 2] = c[2] + prof[:, 0:1]
        N[..., 0] = nr[:, None] * a; N[..., 1] = nr[:, None] * b; N[..., 2] = nh[:, None]
    else:
        P[..., 0] = c[0] + prof[:, 1:2] * a; P[..., 2] = c[2] + prof[:, 1:2] * b; P[..., 1] = c[1] - prof[:, 0:1]
        N[..., 0] = nr[:, None] * a; N[..., 2] = nr[:, None] * b; N[..., 1] = -nh[:, None]
    L = np.concatenate([[0], np.cumsum(np.hypot(np.diff(prof[:, 0]), np.diff(prof[:, 1])))])
    UV = np.stack(np.meshgrid(th / (2 * np.pi), L / max(L[-1], 1e-6)), -1)
    return P.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(len(prof), nc + 1)


def plinth(M):
    """the cracked pedestal: an eight-sided foot, a shaft and a cap, chipped all over; rubble round it"""
    prof = [(0.0, 27.0), (3.0, 27.0), (4.0, 25.0), (7.5, 24.0), (8.5, 19.0), (12.0, 17.0), (30.0, 15.5), (34.0, 17.5),
            (36.5, 22.0), (39.5, 22.5), (PLINTH_TOP, 21.0), (PLINTH_TOP, 0.0)]
    V, N, UV, F = lathe_y(prof, (0, 0, 0), 8 * 6)
    # chipped: the radius shaken (more on the corners), a broken bite out of the cap's front edge
    th = np.arctan2(V[:, 1], V[:, 0]); r = np.hypot(V[:, 0], V[:, 1])
    oct8 = 1.0 + 0.07 * np.abs(np.cos(4 * th)) ** 6          # (eight soft corners)
    noise = (np.sin(th * 13 + V[:, 2] * 0.7) * 0.3 + np.sin(th * 29 - V[:, 2] * 1.3) * 0.18) * (r > 1)
    bite = np.exp(-((th + 1.75) ** 2) / 0.05 - ((V[:, 2] - 38) ** 2) / 10) * 3.5
    k = (oct8 * r + noise - bite) / (r + 1e-9)
    V[:, 0] *= k; V[:, 1] *= k
    UV = np.stack([th / (2 * np.pi) * 3, V[:, 2] / 30.0], 1)
    M.add('Plinth', 'stone', V, N, UV, F)
    # rubble: broken lumps of the same stone at its foot
    for i in range(9):
        a = rng.uniform(0, 2 * np.pi); d = rng.uniform(28, 40); s = rng.uniform(1.5, 4.0)
        c = (d * math.cos(a), d * math.sin(a), s * 0.4)
        Vr, Nr, UVr, Fr = lathe_y([(-s * 0.4, 0), (-s * 0.3, s * 0.8), (s * 0.2, s), (s * 0.6, s * 0.6), (s * 0.7, 0)], c, 7)
        Vr = c + (Vr - c) * rng.uniform(0.7, 1.3, 3)
        M.add('Rubble', 'stone', Vr, Nr, UVr * 0.3 + rng.random(2), Fr)
    # the offerings on the cap: candle stubs, coins, a folded note
    for i, (a, d, h) in enumerate(((0.4, 15.0, 3.2), (1.1, 16.5, 1.8), (2.5, 14.0, 4.4), (-0.7, 15.5, 2.4))):
        c = (d * math.cos(a), d * math.sin(a), PLINTH_TOP)
        wax = [(0, 1.25), (h * 0.9, 1.2), (h, 1.0), (h + 0.2, 0.7), (h + 0.25, 0.0)]
        Vc, Nc, UVc, Fc = lathe_y(wax, c, 14)
        Vc[:, 2] += np.where(Vc[:, 2] > PLINTH_TOP + h * 0.5, np.sin(np.arctan2(Vc[:, 1] - c[1], Vc[:, 0] - c[0]) * 3) * 0.18, 0)   # (melted, uneven)
        M.add('Candles', 'wax', Vc, Nc, UVc, Fc)
        M.add('Candles', 'wax', *lathe_y([(-0.1, 1.6), (0.15, 1.9), (0.35, 1.5), (0.4, 0)], c, 14))   # (the drip pooled under it)
    for i in range(7):
        a = rng.uniform(-math.pi, math.pi); d = rng.uniform(5, 18)
        c = (d * math.cos(a), d * math.sin(a), PLINTH_TOP + 0.05 + 0.18 * (i % 3))
        Vc, Nc, UVc, Fc = lathe_y([(0, 0), (0.0, 1.1), (0.16, 1.15), (0.2, 0)], c, 16)
        M.add('Coins', 'coin', Vc, Nc, UVc * 0.25 + [0.75 * (i % 2), 0], Fc)


def note(M, c, w, h, tilt, bend, seed):
    """a folded note tucked in a crack: a bent paper quad (both sides), a corner curled"""
    nu, nv = 8, 10
    u, v = np.meshgrid(np.linspace(-0.5, 0.5, nu), np.linspace(0, 1, nv))
    x = u * w; z = v * h; y = bend * np.sin(u * np.pi) + 0.6 * (v ** 2) * (u > 0.2) * (u - 0.2)
    ca, sa = math.cos(tilt), math.sin(tilt)
    P = np.stack([c[0] + x * ca - z * sa, c[1] - y, c[2] + x * sa + z * ca], -1).reshape(-1, 3)
    N = np.tile([0, -1, 0], (len(P), 1)).astype(float)
    UV = np.stack([u + 0.5, 1 - v], -1).reshape(-1, 2) * [0.5, 1.0] + [0.5 * (seed % 2), 0]
    M.add('Notes', 'paper', P, N, UV, grid_faces(nv, nu))


def straps(M, outline):
    """rusted iron straps patching the tablet: two bands round it, a brace across the crack, rivets in rows"""
    def band(z, hgt, th=0.55, gap=0.0):
        # a flat band hugging the slab's outline at height z (round the sides and across face and back)
        w = W2 + th
        loop = np.array([(-w, -T2 - th), (w, -T2 - th), (w, T2 + th), (-w, T2 + th), (-w, -T2 - th)])
        seg = np.diff(loop, axis=0); L = np.concatenate([[0], np.cumsum(np.linalg.norm(seg, axis=1))])
        s = np.linspace(0, L[-1], 120); X = np.interp(s, L, loop[:, 0]); Y = np.interp(s, L, loop[:, 1])
        dent = gaussian_filter(rng.normal(0, 0.12, len(s)), 2)
        P = np.vstack([np.stack([X, Y, np.full(len(s), z - hgt / 2) + dent], 1), np.stack([X, Y, np.full(len(s), z + hgt / 2) + dent], 1)])
        Nn = np.stack([np.sign(X) * (np.abs(X) >= w - 0.01), np.sign(Y) * (np.abs(X) < w - 0.01), np.zeros(len(s))], 1)
        N = np.vstack([Nn, Nn])
        UV = np.vstack([np.stack([s / 30.0, np.zeros(len(s))], 1), np.stack([s / 30.0, np.full(len(s), hgt / 30.0)], 1)])
        F = np.array(grid_faces(2, len(s)))
        if gap > 0:   # (across the face it stops short of the pool and its gems: two ends, each riveted)
            mid = (P[F].mean(1))
            F = F[~((np.abs(mid[:, 0]) < gap) & (mid[:, 1] < 0))]
        M.add('Straps', 'iron', P, N, UV, F)
        xs = [x for x in np.linspace(-w + 3.5, w - 3.5, 7) if abs(x) > gap + 1.0] if gap > 0 else np.linspace(-w + 3.5, w - 3.5, 7)
        for xr in xs:                                           # rivets on the face
            rivet(M, (xr, -T2 - th, z), 0.55)
        if gap > 0:
            for sgn in (-1, 1): rivet(M, (sgn * (gap + 0.9), -T2 - th, z), 0.7)
        for xr in np.linspace(-w + 3.5, w - 3.5, 7):
            rivet(M, (xr, T2 + th, z), 0.55, back=True)
    band(Z0 + 12.0, 4.2)
    band(PC[1] + 8.0, 3.6, gap=GEM_R + 2.2)
    # the brace across the upper crack: a plate bent over the face, three rivets each side
    c = np.array([-5.5, -T2 - 0.5, Z1 - 12.0])
    for k, ang in ((0, 0.25),):
        Vb, Nb, UVb, Fb = plate(c, 13.0, 3.4, ang)
        M.add('Straps', 'iron', Vb, Nb, UVb, Fb)
        for s in (-1, 1):
            for j in range(2):
                o = np.array([math.cos(ang), 0, math.sin(ang)]) * (s * (3.6 + j * 2.4))
                rivet(M, c + o, 0.5)


def plate(c, L, Wd, ang):
    ca, sa = math.cos(ang), math.sin(ang)
    u, v = np.meshgrid(np.linspace(-L / 2, L / 2, 14), np.linspace(-Wd / 2, Wd / 2, 3))
    x = u * ca - v * sa; z = u * sa + v * ca
    P = np.stack([c[0] + x, np.full(x.shape, c[1]) - 0.15 * np.cos(u / L * np.pi), c[2] + z], -1).reshape(-1, 3)
    N = np.tile([0, -1, 0], (len(P), 1)).astype(float)
    UV = np.stack([u / 30 + 0.5, v / 30 + 0.5], -1).reshape(-1, 2)
    return P, N, UV, grid_faces(3, 14)


def rivet(M, c, r, back=False):
    prof = [(0, r * 1.15), (r * 0.25, r * 1.05), (r * 0.55, r * 0.75), (r * 0.7, 0)]
    V, N, UV, F = lathe_y(prof, c, 10, axis='y')
    if back: V[:, 1] = 2 * c[1] - V[:, 1]; N[:, 1] *= -1
    M.add('Rivets', 'iron', V, N, UV * 0.05 + 0.2, F)


def gems(M):
    """eight small cut gems set round the recess (each in a little iron collet)"""
    out = []
    for i in range(8):
        a = math.pi / 2 - i * 2 * math.pi / 8
        c = (PC[0] + GEM_R * math.cos(a), -T2, PC[1] + GEM_R * math.sin(a))
        V, N, UV, F = lathe_y([(-0.2, 1.25), (0.25, 1.25), (0.5, 0.95), (0.6, 0)], c, 12, axis='y')
        M.add('Collets', 'iron', V, N, UV * 0.05 + 0.6, F)
        V, N, UV, F = lathe_y([(0.1, 0.95), (0.55, 0.85), (1.05, 0.45), (1.2, 0)], c, 8, axis='y')
        M.add('Gems', 'gem', V, N, UV * 0.5 + 0.25, F)
        out.append(np.array([c[0], c[1] - 1.4, c[2]]))
    return out


# ------------------------------------------------------------------ textures
def save(arr, name, alpha=False):
    TEXDIR.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    a = np.clip(arr, 0, 255).astype(np.uint8)
    tmp = WORK / (name + '.png'); Image.fromarray(a, 'RGBA' if alpha else 'RGB').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', f'dds:compression={"dxt5" if alpha else "dxt1"}', '-define', 'dds:mipmaps=12',
                    str(TEXDIR / name)], check=True)
    return TEXREL + name


GLYPHS = None
def glyph_shapes():
    """Dragon-tongue-like strokes (original: claw scratches - slashes, dots, hooks), as stroke lists in a unit cell"""
    global GLYPHS
    if GLYPHS: return GLYPHS
    r = np.random.default_rng(77); out = []
    for _ in range(40):
        g = []
        for _ in range(r.integers(2, 5)):
            kind = r.integers(0, 4)
            if kind == 0: g.append(('line', [(r.uniform(0.15, 0.5), 0.1), (r.uniform(0.5, 0.85), 0.9)]))
            elif kind == 1: g.append(('line', [(0.2, r.uniform(0.2, 0.8)), (0.8, r.uniform(0.2, 0.8))]))
            elif kind == 2: g.append(('dot', [(r.uniform(0.25, 0.75), r.uniform(0.2, 0.8))]))
            else:
                x = r.uniform(0.25, 0.75); g.append(('line', [(x, 0.15), (x + r.uniform(-0.2, 0.2), 0.55), (x + r.uniform(-0.3, 0.3), 0.85)]))
        out.append(g)
    GLYPHS = out
    return out


def draw_glyph(dr, g, x0, y0, s, w, fill):
    for kind, pts in g:
        P = [(x0 + px * s, y0 + py * s) for px, py in pts]
        if kind == 'dot': dr.ellipse([P[0][0] - w, P[0][1] - w, P[0][0] + w, P[0][1] + w], fill=fill)
        else: dr.line(P, fill=fill, width=int(w), joint='curve')


def stone_base(S, seed, warm=(118, 112, 102)):
    """weathered grey stone: layered noise, pits, lichen and grime; height for its normal"""
    f1 = tileable(S, S, 1.9, seed); f2 = tileable(S, S, 1.2, seed + 1); f3 = tileable(S, S, 2.6, seed + 2)
    pits = (tileable(S, S, 0.6, seed + 3) > 0.83).astype(np.float32)
    pits = gaussian_filter(pits, 1.2)
    hgt = 0.55 * f1 + 0.25 * f2 + 0.2 * f3 - 0.5 * pits
    base = np.array(warm, np.float32)
    tone = 0.72 + 0.42 * f1[..., None] + 0.12 * (f2[..., None] - 0.5) - 0.35 * pits[..., None]
    col = base * tone
    lichen = np.clip((tileable(S, S, 2.2, seed + 4) - 0.66) * 4, 0, 1)[..., None] * (f3[..., None] > 0.45)
    col = col * (1 - 0.45 * lichen) + np.array([96, 104, 70], np.float32) * 0.45 * lichen
    grime = np.clip((tileable(S, S, 1.6, seed + 5) - 0.5) * 2, 0, 1)[..., None]
    col *= 1 - 0.28 * grime
    return col, hgt


def face_tex():
    """the face (planar over the whole tablet): weathered stone, a carved border of glyphs down both sides and round the
    arch, the pool's carved rim, the crack (dark, with soot and rust weeping from the straps), wax drips, a soot halo"""
    S = SIZE
    col, hgt = stone_base(S, 11)
    yy, xx = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    X = xx * 2 * W2 - W2; Z = Z0 + (1 - yy) * H                       # texel -> tablet face position
    # carvings drawn at full size: the border band and its glyphs, the pool rim's rings of glyphs
    cv = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(cv)
    px = lambda x, z: ((x + W2) / (2 * W2) * S, (1 - (z - Z0) / H) * S)
    # the border: two incised lines inset 2.5 and 5.5 from the outline (straight sides, the arch)
    for ins, wdt in ((2.4, 0.22), (6.2, 0.22)):
        pts = []
        for t in np.linspace(math.pi, 0, 180):
            pts.append(px((W2 - ins) * math.cos(t), Z1 - ARCH + (ARCH - ins * 0.8) * math.sin(t)))
        pts = [px(-W2 + ins, Z0 + ins)] + pts + [px(W2 - ins, Z0 + ins)]
        dr.line(pts, fill=255, width=max(2, int(wdt / (2 * W2) * S)))
        dr.line([px(-W2 + ins, Z0 + ins), px(W2 - ins, Z0 + ins)], fill=255, width=max(2, int(wdt / (2 * W2) * S)))
    G = glyph_shapes(); gi = 0
    gs = 2.8 / (2 * W2) * S
    for z in np.arange(Z0 + 7.5, Z1 - ARCH - 1, 3.6):                 # glyphs down both sides of the band
        for x in (-W2 + 4.3, W2 - 4.3):
            x0, y0 = px(x - 1.4, z + 1.4)
            draw_glyph(dr, G[gi % len(G)], x0, y0, gs, max(2, gs * 0.11), 255); gi += 1
    for t in np.linspace(math.pi * 0.95, math.pi * 0.05, 15):          # and round the arch
        x = (W2 - 4.3) * math.cos(t); z = Z1 - ARCH + (ARCH - 3.4) * math.sin(t)
        x0, y0 = px(x - 1.4, z + 1.4)
        draw_glyph(dr, G[gi % len(G)], x0, y0, gs, max(2, gs * 0.11), 255); gi += 1
    # the pool's rim: a ring of small glyphs round its lip, and a deep incised circle
    for k in range(36):
        a = k / 36 * 2 * math.pi
        x0, y0 = px(PC[0] + (POOL_R + 1.4) * math.cos(a) - 0.7, PC[1] + (POOL_R + 1.4) * math.sin(a) + 0.7)
        draw_glyph(dr, G[(k * 7) % len(G)], x0, y0, gs * 0.5, max(2, gs * 0.07), 255)
    cx, cy = px(PC[0], PC[1]); rr = (POOL_R + 0.3) / (2 * W2) * S
    dr.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=255, width=max(3, int(S / 700)))
    carve = np.asarray(cv.filter(ImageFilter.GaussianBlur(S / 2200)), np.float32) / 255
    # worn: the carving softened and broken up by the stone's own wear
    carve *= np.clip(0.55 + 0.9 * tileable(S, S, 1.4, 21), 0, 1)
    hgt = hgt - 0.9 * carve
    col *= (1 - 0.5 * carve)[..., None]
    # the cracks: a dark fissure along each path, its edges chipped, soot round it
    ck = Image.new('L', (S, S), 0); dk = ImageDraw.Draw(ck)
    for path, seed in ((CRACK, 3), (CRACK2, 4)):
        P = jag(path, 0.7, 70, seed)
        dk.line([px(x, z) for x, z in P], fill=255, width=max(3, int(S / 500)))
        for j in range(3):   # hairline branches
            i0 = rng.integers(5, 60); br = P[i0] + np.cumsum(rng.normal(0, 0.8, (10, 2)), 0)
            dk.line([px(x, z) for x, z in br], fill=170, width=max(1, int(S / 1400)))
    crack = np.asarray(ck.filter(ImageFilter.GaussianBlur(S / 3000)), np.float32) / 255
    soot = gaussian_filter(crack, S / 260)
    hgt = hgt - 2.0 * crack
    col *= (1 - 0.85 * crack)[..., None]
    col *= (1 - 0.5 * np.clip(soot * 3, 0, 1))[..., None]
    # rust weeping down from the straps' rivet rows
    for zs in (Z0 + 12.0, PC[1] + 8.0):
        band = np.clip(1 - np.abs(Z - zs) / 2.6, 0, 1)
        streak = (Z < zs) * np.exp(-(zs - Z) / 9.0) * (tileable(S, S, 1.2, int(zs)) ** 2) * (np.sin(X * 1.1) * 0.5 + 0.5)
        r_ = np.clip(streak * 1.6 + band * 0.4, 0, 1)[..., None]
        col = col * (1 - 0.55 * r_) + np.array([120, 60, 30], np.float32) * 0.55 * r_
    # the pool's floor: darker, smoothed by countless hands, a soot halo from the light
    rP = np.hypot(X - PC[0], Z - PC[1])
    pool = np.clip((POOL_R - rP) / 1.5, 0, 1)
    col *= (1 - 0.35 * pool)[..., None]
    halo = np.exp(-((rP - POOL_R - 1) ** 2) / 12.0)
    col *= (1 - 0.25 * halo)[..., None]
    # wax drips running down from the arch's foot of the left shoulder
    for xs in (-W2 + 1.2, W2 - 1.4):
        d = 0.5 * np.exp(-((X - xs) / 0.3) ** 2) * (Z > Z0 + 20) * (Z < Z0 + 20 + rng.uniform(6, 14))
        col = col * (1 - 0.6 * d[..., None]) + np.array([218, 208, 180], np.float32) * 0.6 * d[..., None]
    nrm = height_to_normal(gaussian_filter(hgt, 0.6), S / 320.0)
    # the glow: the carved glyphs hold a faint failing light (the code breathes it unevenly)
    glow = np.zeros((S, S, 3), np.float32)
    lit = carve * (tileable(S, S, 0.9, 31) > 0.35)
    glow += np.array([255, 150, 60], np.float32) * lit[..., None] * 0.55
    return save(col, 'tablet_face_d.dds'), save(nrm, 'tablet_face_n.dds'), save(glow, 'tablet_face_g.dds')


def stone_tex():
    S = SIZE
    col, hgt = stone_base(S, 41, (112, 108, 100))
    # broken faces: a few deep fractures
    fr = (tileable(S, S, 1.7, 44) > 0.78).astype(np.float32)
    fr = gaussian_filter(fr - gaussian_filter(fr, 3), 1.0)
    hgt -= 1.4 * np.clip(fr, 0, 1)
    nrm = height_to_normal(gaussian_filter(hgt, 0.8), S / 300.0)
    return save(col, 'tablet_stone_d.dds'), save(nrm, 'tablet_stone_n.dds')


def ring_tex():
    """the glyph rings (polar uv: u round, v across): carved bronze-dark stone, a row of deep glyphs that glow"""
    S = SIZE
    col, hgt = stone_base(S, 61, (96, 88, 78))
    gl = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(gl)
    G = glyph_shapes()
    for band, (v0, v1) in enumerate(((0.0, 0.5), (0.5, 1.0))):
        n = 18 if band == 0 else 12
        s = S / n * 0.62
        for k in range(n):
            x0 = (k + 0.5) / n * S - s / 2; y0 = (v0 + v1) / 2 * S - s / 2
            draw_glyph(dr, G[(k * 5 + band * 3) % len(G)], x0, y0, s, max(3, s * 0.1), 255)
        dr.line([(0, v0 * S + S * 0.02), (S, v0 * S + S * 0.02)], fill=150, width=max(2, S // 900))
        dr.line([(0, v1 * S - S * 0.02), (S, v1 * S - S * 0.02)], fill=150, width=max(2, S // 900))
    g = np.asarray(gl.filter(ImageFilter.GaussianBlur(S / 1600)), np.float32) / 255
    wear = np.clip(0.4 + tileable(S, S, 1.3, 63), 0, 1)
    hgt -= 0.9 * g * wear
    col *= (1 - 0.6 * g * wear)[..., None]
    nrm = height_to_normal(gaussian_filter(hgt, 0.7), S / 300.0)
    glow = np.array([255, 160, 70], np.float32) * (g * (0.4 + 0.6 * wear))[..., None]
    return save(col, 'tablet_ring_d.dds'), save(nrm, 'tablet_ring_n.dds'), save(glow, 'tablet_ring_g.dds')


def iron_tex():
    S = 2048
    f1 = tileable(S, S, 1.6, 81); f2 = tileable(S, S, 2.4, 82); f3 = tileable(S, S, 0.9, 83)
    rust = np.clip((f1 * 0.7 + f3 * 0.5 - 0.45) * 2.2, 0, 1)
    flake = (f2 > 0.7).astype(np.float32) * rust
    iron = np.array([62, 60, 58], np.float32); rc = np.array([124, 62, 28], np.float32); rd = np.array([70, 34, 18], np.float32)
    col = iron * (0.8 + 0.4 * f2[..., None])
    col = col * (1 - rust[..., None]) + (rc * (1 - f3[..., None] * 0.5) + rd * f3[..., None] * 0.5) * rust[..., None]
    hgt = 0.4 * f2 + 0.6 * flake - 0.3 * (f3 > 0.85)
    # hammer dents
    dents = gaussian_filter((tileable(S, S, 0.5, 84) > 0.88).astype(np.float32), 6)
    hgt -= dents * 0.8
    nrm = height_to_normal(gaussian_filter(hgt, 0.8), S / 260.0)
    return save(col, 'tablet_iron_d.dds'), save(nrm, 'tablet_iron_n.dds')


def small_tex():
    """wax, gems, coins and paper (2048, each its own)"""
    S = 2048
    f = tileable(S, S, 1.8, 91)
    wax = np.array([214, 202, 170], np.float32) * (0.85 + 0.2 * f[..., None])
    soot = (np.mgrid[0:S, 0:S][0] < S * 0.12)[..., None] * 0.6
    wax = wax * (1 - soot)
    wD = save(wax, 'tablet_wax_d.dds'); wN = save(height_to_normal(f, 4.0), 'tablet_wax_n.dds')
    gem = np.array([150, 20, 30], np.float32) * (0.6 + 0.6 * tileable(S, S, 1.0, 92)[..., None])
    gD = save(gem, 'tablet_gem_d.dds'); gN = save(np.full((S, S, 3), (128, 128, 255), np.float32), 'tablet_gem_n.dds')
    coin = np.zeros((S, S, 3), np.float32)
    coin[:, : S // 2] = np.array([176, 130, 58]) * (0.7 + 0.5 * tileable(S, S // 2, 1.4, 93)[..., None])   # gold, tarnished
    coin[:, S // 2:] = np.array([120, 110, 96]) * (0.7 + 0.5 * tileable(S, S // 2, 1.4, 94)[..., None])   # old silver
    cD = save(coin, 'tablet_coin_d.dds'); cN = save(height_to_normal(tileable(S, S, 1.6, 95), 3.0), 'tablet_coin_n.dds')
    # paper: two notes side by side, faded ink in a hand nobody reads now, stained and creased
    pp = np.array([206, 190, 150], np.float32) * (0.82 + 0.25 * tileable(S, S, 1.5, 96)[..., None])
    im = Image.fromarray(np.clip(pp, 0, 255).astype(np.uint8)); dr = ImageDraw.Draw(im)
    rr = np.random.default_rng(97)
    for half in (0, 1):
        for line in range(16):
            y = S * (0.1 + line * 0.05); x = S * (0.05 + half * 0.5)
            while x < S * (0.45 + half * 0.5):
                wl = rr.uniform(S * 0.02, S * 0.06)
                dr.line([(x, y + rr.uniform(-3, 3)), (x + wl, y + rr.uniform(-3, 3))], fill=(70, 52, 40), width=4)
                x += wl + S * 0.012
    paper = np.asarray(im, np.float32)
    stain = gaussian_filter((tileable(S, S, 0.8, 98) > 0.75).astype(np.float32), 20)[..., None]
    paper = paper * (1 - 0.3 * stain)
    pD = save(paper, 'tablet_paper_d.dds'); pN = save(height_to_normal(tileable(S, S, 1.0, 99), 2.0), 'tablet_paper_n.dds')
    return (wD, wN), (gD, gN), (cD, cN), (pD, pN)


def fx_tex():
    """the additive effects: the pool's swirl (gold-amber), a soft glow, motes, a candle-flame flipbook, a spark flipbook,
    the weapons' ghost outlines (a flipbook of silhouettes - the reel)"""
    out = {}
    S = 1024
    yy, xx = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    w1 = tileable(S, S, 1.7, 101); w2 = tileable(S, S, 2.3, 102)
    sw = np.clip((w1 * 0.7 + w2 * 0.5 - 0.35) * 1.6, 0, 1)
    out['swirl'] = save(np.dstack([sw * 255, sw * 170, sw * 70, sw * 255]), 'tablet_fx_swirl.dds', True)
    r = np.hypot(xx - 0.5, yy - 0.5) * 2
    g = np.clip(1 - r, 0, 1) ** 2.2
    out['glow'] = save(np.dstack([g * 255] * 4), 'tablet_fx_glow.dds', True)
    m = np.zeros((S, S), np.float32)
    rr = np.random.default_rng(103)
    for _ in range(260):
        x, y = rr.integers(0, S, 2); s = rr.uniform(1.5, 4.5)
        m[max(0, y - 12):y + 12, max(0, x - 12):x + 12] += np.exp(-((np.arange(max(0, y - 12), min(S, y + 12))[:, None] - y) ** 2 + (np.arange(max(0, x - 12), min(S, x + 12))[None, :] - x) ** 2) / (2 * s * s))
    m = np.clip(m, 0, 1)
    out['motes'] = save(np.dstack([m * 255, m * 200, m * 120, m * 255]), 'tablet_fx_motes.dds', True)
    # flame: 4x4 flipbook of a guttering flame
    C = S // 4; fl = np.zeros((S, S, 4), np.float32)
    v, u = (np.mgrid[0:C, 0:C].astype(np.float32) + 0.5) / C
    for k in range(16):
        ph = k / 16 * 2 * math.pi
        cx = 0.5 + 0.05 * math.sin(ph * 2) + 0.03 * math.sin(ph * 5)
        h = 0.62 + 0.08 * math.sin(ph * 3)
        shape = np.clip(1 - ((u - cx - (1 - v - 0.2) * 0.06 * math.sin(ph)) / (0.17 * np.clip(v - (1 - h), 0, 1) ** 0.6 + 1e-3)) ** 2, 0, 1) * (v > 1 - h) * np.clip((0.97 - v) * 6, 0, 1)
        core = shape ** 3
        cell = np.dstack([shape * 255, shape * 150 + core * 90, shape * 40 + core * 160, shape * 255])
        y0, x0 = (k // 4) * C, (k % 4) * C
        fl[y0:y0 + C, x0:x0 + C] = cell
    out['flame'] = save(fl, 'tablet_fx_flame.dds', True)
    # sparks: 4x4 flipbook of a spray of short streaks flying out and fading
    sp = Image.new('RGBA', (S, S), (0, 0, 0, 0)); ds = ImageDraw.Draw(sp)
    for k in range(16):
        y0, x0 = (k // 4) * C, (k % 4) * C; t = k / 15
        r2 = np.random.default_rng(200 + (k % 4))
        for j in range(18):
            a = r2.uniform(-2.6, -0.5); d0 = t * r2.uniform(0.15, 0.45) * C; L2 = C * 0.06 * (1 - t)
            px0 = x0 + C / 2 + math.cos(a) * d0; py0 = y0 + C * 0.65 + math.sin(a) * d0 + t * t * C * 0.25
            al = int(255 * (1 - t) ** 1.2)
            ds.line([(px0, py0), (px0 - math.cos(a) * L2, py0 - math.sin(a) * L2)], fill=(255, 200, 110, al), width=3)
    out['sparks'] = save(np.asarray(sp, np.float32), 'tablet_fx_sparks.dds', True)
    # ghost outlines: a 4x2 flipbook of weapon silhouettes (fists, mace, sickle, axe, bow, staff, greatsword, a star)
    gh = Image.new('L', (S, S // 2), 0); dg = ImageDraw.Draw(gh); C2 = S // 4
    def cell(i): return (i % 4) * C2, (i // 4) * C2
    shapes = [
        lambda x, y: [dg.rounded_rectangle([x + C2 * .3, y + C2 * .4, x + C2 * .7, y + C2 * .62], 18, outline=255, width=6)] + [dg.ellipse([x + C2 * (.32 + k * .1), y + C2 * .32, x + C2 * (.4 + k * .1), y + C2 * .42], outline=255, width=5) for k in range(4)],
        lambda x, y: [dg.line([(x + C2 * .5, y + C2 * .9), (x + C2 * .5, y + C2 * .35)], fill=255, width=7), dg.pieslice([x + C2 * .3, y + C2 * .1, x + C2 * .7, y + C2 * .5], 180, 360, outline=255, width=6)],
        lambda x, y: [dg.line([(x + C2 * .45, y + C2 * .9), (x + C2 * .45, y + C2 * .5)], fill=255, width=7), dg.arc([x + C2 * .2, y + C2 * .1, x + C2 * .7, y + C2 * .6], 200, 360, fill=255, width=7)],
        lambda x, y: [dg.line([(x + C2 * .4, y + C2 * .92), (x + C2 * .55, y + C2 * .1)], fill=255, width=7), dg.chord([x + C2 * .45, y + C2 * .1, x + C2 * .85, y + C2 * .45], 260, 100, outline=255, width=6)],
        lambda x, y: [dg.arc([x + C2 * .25, y + C2 * .08, x + C2 * .75, y + C2 * .92], 270, 90, fill=255, width=7), dg.line([(x + C2 * .5, y + C2 * .1), (x + C2 * .5, y + C2 * .9)], fill=200, width=3)],
        lambda x, y: [dg.line([(x + C2 * .5, y + C2 * .95), (x + C2 * .5, y + C2 * .25)], fill=255, width=7), dg.ellipse([x + C2 * .38, y + C2 * .06, x + C2 * .62, y + C2 * .3], outline=255, width=6)],
        lambda x, y: [dg.polygon([(x + C2 * .5, y + C2 * .05), (x + C2 * .58, y + C2 * .7), (x + C2 * .42, y + C2 * .7)], outline=255, width=6), dg.line([(x + C2 * .3, y + C2 * .7), (x + C2 * .7, y + C2 * .7)], fill=255, width=7), dg.line([(x + C2 * .5, y + C2 * .7), (x + C2 * .5, y + C2 * .95)], fill=255, width=7)],
        lambda x, y: [dg.polygon([(x + C2 * (.5 + .38 * math.cos(a) * (1 if k % 2 == 0 else .45)), y + C2 * (.5 + .38 * math.sin(a) * (1 if k % 2 == 0 else .45))) for k, a in enumerate(np.linspace(-math.pi / 2, 1.5 * math.pi, 11)[:-1])], outline=255, width=6)],
    ]
    for i, f in enumerate(shapes): f(*cell(i))
    ghs = np.asarray(gh.filter(ImageFilter.GaussianBlur(3)), np.float32) / 255
    ghs = np.clip(ghs * 1.6, 0, 1)
    out['ghost'] = save(np.dstack([ghs * 200, ghs * 230, ghs * 255, ghs * 255]), 'tablet_fx_ghost.dds', True)
    return out


# ------------------------------------------------------------------ the effects layer (fxwrite)
def fx_job(base, out, fx, gem_at):
    L = [f'base {base}', f'out {out}']

    def card(name, t, at, hs, col, sc, flip=None, parent='TabletBody'):
        extra = f' flip={flip[0]}x{flip[1]} fps={flip[2]} phase={flip[3]}' if flip else ''
        L.append(f'shape {name} tex={t} color={col} scale={sc} blend=add billboard=1 parent={parent} at={at[0]:.3f},{at[1]:.3f},{at[2]:.3f}{extra}')
        cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
        for X, Y, uu, vv in ((-hs, hs, 0, 0), (hs, hs, cu, 0), (hs, -hs, cu, cv), (-hs, -hs, 0, cv)):
            L.append(f'v {X:.3f} {Y:.3f} 0 {uu} {vv} 1 1 1 1')
        L.extend(['t 0 1 2', 't 0 2 3'])

    def disc(name, t, r, y, col, sc, us, vs, parent='TabletBody'):
        L.append(f'shape {name} tex={t} color={col} scale={sc} blend=add parent={parent} uscroll={us} vscroll={vs}')
        n = 64
        L.append(f'v {PC[0]:.3f} {y:.3f} {PC[1]:.3f} 0.5 0.5 1 1 1 1')
        for k in range(n + 1):
            a = k / n * 2 * math.pi
            L.append(f'v {PC[0] + r * math.cos(a):.3f} {y:.3f} {PC[1] + r * math.sin(a):.3f} {0.5 + 0.5 * math.cos(a):.3f} {0.5 + 0.5 * math.sin(a):.3f} 1 1 1 0.2')
        for k in range(n): L.append(f't 0 {k + 1} {k + 2}')

    pool_y = -T2 + 1.5
    disc('Pool', fx['swirl'], POOL_R - 0.6, pool_y - 0.05, '1,0.85,0.6,0.8', 1.2, 14, 23)
    disc('PoolDeep', fx['swirl'], POOL_R * 0.7, pool_y - 0.1, '1,0.7,0.4,0.6', 1.0, -19, -31)
    # the reel's ghosts: a card in the pool flicking through the silhouettes (hidden until a roll)
    L.append(f'shape Ghost tex={fx["ghost"]} color=0.8,0.9,1,1 scale=1.8 blend=add parent=TabletBody flip=4x2 fps=9 phase=0')
    hs = POOL_R * 0.62
    for X, Z, uu, vv in ((-hs, hs, 0, 0), (hs, hs, 0.25, 0), (hs, -hs, 0.25, 0.5), (-hs, -hs, 0, 0.5)):
        L.append(f'v {PC[0] + X:.3f} {pool_y - 0.6:.3f} {PC[1] + Z:.3f} {uu} {vv} 1 1 1 1')
    L.extend(['t 0 1 2', 't 0 2 3'])
    card('GoldFlare', fx['glow'], (PC[0], -T2 - 3, PC[1]), POOL_R * 2.4, '1,0.8,0.35,0.9', 1.6)
    for i, at in enumerate(gem_at):
        card(f'Gem{i}', fx['glow'], at, 3.2, '1,0.25,0.15,1', 2.4)
    # sparks along the cracks
    for i, (x, z) in enumerate([CRACK[1], CRACK[2], (CRACK[3] + CRACK[2]) / 2, CRACK2[1], CRACK2[2], (CRACK2[0] + CRACK2[1]) / 2]):
        card(f'Spark{i}', fx['sparks'], (x, -T2 - 1.0, z), 6.0, '1,0.8,0.5,1', 2.2, (4, 4, 14 + i, i * 0.13))
    # the candles' flames (on the plinth: under the root, not the floating body)
    for i, (a, d, h) in enumerate(((0.4, 15.0, 3.2), (1.1, 16.5, 1.8), (2.5, 14.0, 4.4), (-0.7, 15.5, 2.4))):
        c = (d * math.cos(a), d * math.sin(a), PLINTH_TOP + h + 1.4)
        card(f'Flame{i}', fx['flame'], c, 1.6, '1,0.85,0.6,1', 2.0, (4, 4, 11 + i * 2, i * 0.21), parent='TabletOfMysteries')
        card(f'FlameGlow{i}', fx['glow'], c, 5.0, '1,0.6,0.3,0.35', 0.8, parent='TabletOfMysteries')
    # motes rising round the tablet
    for band, (rad, za, zb, us, vs, al) in enumerate(((W2 + 6, Z0 - 4, Z1 + 6, 6.0, 17, 0.7), (W2 + 11, Z0 + 10, Z1 - 4, -9.0, 25, 0.45))):
        L.append(f'shape Motes{band} tex={fx["motes"]} color=1,0.85,0.6,{al} scale=1.6 blend=add parent=TabletBody uscroll={us} vscroll=-{vs}')
        nu, nv = 48, 10
        for i in range(nv + 1):
            z = za + (zb - za) * i / nv
            for j in range(nu + 1):
                th = 2 * math.pi * j / nu
                fade = math.sin(math.pi * i / nv) ** 1.5
                L.append(f'v {rad * math.cos(th):.3f} {rad * 0.5 * math.sin(th):.3f} {z:.3f} {j / nu * 3:.3f} {i / nv * 1.5:.3f} 1 1 1 {fade:.3f}')
        for i in range(nv):
            for j in range(nu):
                a = i * (nu + 1) + j; b = a + 1; cc = a + nu + 1; d = cc + 1
                L += [f't {a} {cc} {b}', f't {b} {cc} {d}']
    return L


# ------------------------------------------------------------------ build
def build():
    WORK.mkdir(parents=True, exist_ok=True); MESHDIR.mkdir(parents=True, exist_ok=True)
    td.WORK = WORK
    print('textures...', SIZE)
    fD, fN, fG = face_tex(); sD, sN = stone_tex(); rD, rN, rG = ring_tex(); iD, iN = iron_tex()
    (wD, wN), (gD, gN), (cD, cN), (pD, pN) = small_tex()
    fx = fx_tex()
    print('mesh...')
    slab, piece, cut, full = split_outline()
    body = Mesh(); base = Mesh(); corner = Mesh(); rings_o = Mesh(); rings_i = Mesh()
    plinth(base)
    slab_mesh(body, slab, 'Slab', 'face', 'stone', recess)
    straps(body, slab)
    gem_at = gems(body)
    note(body, (-8.6, -T2 - 0.25, Z1 - 27.5), 3.0, 4.2, 0.35, 0.25, 0)       # tucked into the upper crack
    note(body, (W2 - 7.0, -T2 - 0.7, Z0 + 10.6), 3.6, 5.2, -0.2, 0.3, 1)     # behind the lower strap
    note(base, (6.0, -14.0, PLINTH_TOP + 0.1), 4.0, 5.0, 1.5, 0.05, 1)        # laid on the cap
    # the broken corner: its own slab piece (same face texture: its carving continues across the break)
    slab_mesh(corner, piece, 'Corner', 'face', 'stone', lambda x, z: np.zeros_like(x), step=0.9)
    ring_mesh(rings_o, 'RingOuter', RING_O[0], RING_O[1], -T2 + 1.25, (0.0, 0.5))
    ring_mesh(rings_i, 'RingInner', RING_I[0], RING_I[1], -T2 + 1.15, (0.5, 1.0))
    parts = {'Base': base, 'Body': body, 'Corner': corner, 'RingOuter': rings_o, 'RingInner': rings_i}
    # one OBJ per part (each its own NIF node, so the code can move them), joined afterwards
    mats = [f'mat face diffuse={fD} normal={fN} glow={fG} emit=1,0.7,0.4 emitmult=0.9 spec=0.35 gloss=18 alpha=0 doublesided=0',
            f'mat stone diffuse={sD} normal={sN} spec=0.3 gloss=14 alpha=0 doublesided=0',
            f'mat ring diffuse={rD} normal={rN} glow={rG} emit=1,0.7,0.4 emitmult=1.2 spec=0.4 gloss=22 alpha=0 doublesided=0',
            f'mat iron diffuse={iD} normal={iN} spec=0.9 gloss=40 alpha=0 doublesided=0',
            f'mat wax diffuse={wD} normal={wN} spec=0.4 gloss=20 alpha=0 doublesided=0',
            f'mat gem diffuse={gD} normal={gN} spec=2.6 gloss=200 alpha=0 doublesided=0',
            f'mat coin diffuse={cD} normal={cN} spec=1.6 gloss=70 alpha=0 doublesided=0',
            f'mat paper diffuse={pD} normal={pN} spec=0.2 gloss=10 alpha=0 doublesided=1']
    nifs = {}
    for name, M in parts.items():
        merged = {}
        for gname, m, V, N, UV, F in M.groups:
            if m not in merged: merged[m] = [m, [], [], [], [], 0]
            e = merged[m]; e[1].append(V); e[2].append(N); e[3].append(UV); e[4].append(np.asarray(F) + e[5]); e[5] += len(V)
        M.groups = [(f'{name}_{m}', m, np.vstack(e[1]), np.vstack(e[2]), np.vstack(e[3]), np.vstack(e[4])) for m, e in merged.items()]
        obj = WORK / f'{name}.obj'; M.write_obj(obj)
        nif = WORK / f'{name}.nif'
        job = [f'in {obj}', f'out {nif}', 'scale 1', 'ground 0', 'origin 1', 'offset 0 0 0'] + mats
        jf = WORK / f'{name}.job'; jf.write_text('\n'.join(job) + '\n')
        r = subprocess.run([str(TOOLS / 'modelconv'), str(jf)], capture_output=True, text=True)
        if r.returncode: print(r.stdout, r.stderr); raise SystemExit('modelconv failed: ' + name)
        nifs[name] = nif
    # the parts joined under one root: TabletOfMysteries > (the base's shapes), TabletBody > (the body, RingOuter,
    # RingInner nodes), LooseCorner (beside the body, offset out and tilted)
    joined = WORK / 'tablet_joined.nif'
    jsrc = WORK / 'join.cpp'
    jsrc.write_text(JOIN_CPP)
    exe = WORK / 'join'
    if not exe.exists() or exe.stat().st_mtime < jsrc.stat().st_mtime:
        subprocess.run(['g++', '-std=c++20', '-O1', f'-I{TOOLS}/nifly/include', f'-I{TOOLS}/nifly/external', str(jsrc),
                        str(TOOLS / 'nifly' / 'build' / 'src' / 'libnifly.a'), '-o', str(exe)], check=True)
    cc = piece.mean(0)
    subprocess.run([str(exe), str(joined), str(nifs['Base']), str(nifs['Body']), str(nifs['RingOuter']), str(nifs['RingInner']),
                    str(nifs['Corner']), f'{cc[0]:.3f}', f'{cc[1]:.3f}'], check=True)
    out = MESHDIR / 'tablet.nif'
    L = fx_job(joined, out, fx, gem_at)
    (WORK / 'fx.job').write_text('\n'.join(L) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / 'fx.job')], capture_output=True, text=True)
    if r.returncode or 'no node' in r.stderr: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed')
    # the glyphs' uneven breathing (the lit shader's emissive): the face and the rings, a little apart
    aj = WORK / 'anim.job'
    aj.write_text(f'in {out}\nout {out}\npulse Body_face 0.35 1.15 2.7\npulse RingOuter_ring 0.5 1.4 1.9\npulse RingInner_ring 0.45 1.3 2.3\npulse Corner_face 0.2 0.9 3.4\n')
    r = subprocess.run([str(TOOLS / 'nifanim'), str(aj)], capture_output=True, text=True) if (TOOLS / 'nifanim').exists() else None
    if r is None:
        td.tool('nifanim'); r = subprocess.run([str(TOOLS / 'nifanim'), str(aj)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('nifanim failed')
    tri = sum(len(g[5]) for M in parts.values() for g in M.groups)
    print(f'{out}: {tri} triangles; textures {SIZE}')
    return parts, out


# joins the parts' NIFs into one: argv: out base body ringO ringI corner cornerX cornerZ
JOIN_CPP = r'''#include "NifFile.hpp"
#include <cmath>
#include <cstdlib>
using namespace nifly;
static void graft(NifFile& dst, NiNode* parent, NifFile& src) {
	auto& h = dst.GetHeader();
	auto* root = dst.GetRootNode();
	for (auto* sh : src.GetShapes()) {
		auto* c = dst.CloneShape(sh, sh->name.get(), &src);
		if (!c) continue;
		const uint32_t id = h.GetBlockID(c);
		if (parent != root) { root->childRefs.RemoveBlockRef(id); parent->childRefs.AddBlockRef(id); }
	}
}
int main(int argc, char** argv) {
	NifFile out;
	if (out.Load(argv[2])) return 1;                       // the base's NIF is the start (its root: the scene)
	auto* root = out.GetRootNode();
	root->name.get() = "TabletOfMysteries";
	MatTransform id;
	auto* body = out.AddNode("TabletBody", id, root);
	auto* ro = out.AddNode("RingOuter", id, body);
	auto* ri = out.AddNode("RingInner", id, body);
	// the broken corner turned 18 degrees about its own middle (c) and drifted out and up from the break
	const float cx = std::atof(argv[7]), cz = std::atof(argv[8]), a = 0.32f, ca = std::cos(a), sa = std::sin(a);
	MatTransform ct;
	ct.rotation = Matrix3(ca, 0, sa, 0, 1, 0, -sa, 0, ca);
	const float dx = 5.5f, dy = -2.5f, dz = 4.0f;                           // (the drift)
	ct.translation = Vector3(cx + dx - (ca * cx + sa * cz), dy, cz + dz - (-sa * cx + ca * cz));
	auto* corner = out.AddNode("LooseCorner", ct, body);
	NifFile b, o, i, c;
	if (b.Load(argv[3]) || o.Load(argv[4]) || i.Load(argv[5]) || c.Load(argv[6])) return 2;
	graft(out, body, b); graft(out, ro, o); graft(out, ri, i); graft(out, corner, c);
	return out.Save(argv[1]);
}
'''


def preview(path, parts):
    """the model from the front, three-quarter and side (a point-splat render of the shapes and their textures)"""
    from throwdagger import render, rot
    import tempfile
    FXN = ('Motes', 'Pool', 'Ghost', 'Gem', 'Spark', 'Flame', 'GoldFlare')
    sh = [x for x in td.nif_shapes(MESHDIR / 'tablet.nif') if not any(str(x[0].get('name', '')).startswith(f) for f in FXN)]
    td.OUT = OUT
    # the corner's own offset is in its node: shown where the code would hang it
    W, Hh = 520, 760
    views = [('front', rot('x', -90)), ('3/4', rot('y', 35) @ rot('x', -90)), ('side', rot('y', 90) @ rot('x', -90))]
    out = Image.new('RGB', (W * len(views), Hh + 28), (18, 18, 22)); dr = ImageDraw.Draw(out)
    for k, (label, R) in enumerate(views):
        sh2 = [(m, V.copy(), F) for m, V, F in sh]
        for m, V, F in sh2:   # (the corner's node: turned about its middle and drifted, as JOIN_CPP sets it)
            if str(m.get('name', '')).startswith('Corner'):
                _, piece, _, _ = split_outline(); cx, cz = piece.mean(0); a = 0.32; ca, sa = math.cos(a), math.sin(a)
                x, z = V[:, 0].copy(), V[:, 2].copy()
                V[:, 0] = ca * x + sa * z + (cx + 5.5 - (ca * cx + sa * cz)); V[:, 2] = -sa * x + ca * z + (cz + 4.0 - (-sa * cx + ca * cz)); V[:, 1] -= 2.5
        for m, V, F in sh2: V[:, 2] -= 82.0       # (centred on the tablet)
        R2 = R
        img = render([(m, np.hstack([(V[:, :3] - [0, 0, 0]), V[:, 3:]]), F) for m, V, F in sh2], R2, W, Hh, span=190.0)
        out.paste(Image.fromarray((img * 255).astype(np.uint8)), (k * W, 0))
        dr.text((k * W + 8, 8), label, fill=(230, 230, 230))
    dr.text((8, Hh + 8), 'Tablet of Mysteries (static frame: rings, sparks, ghosts and gem lights are driven in game)', fill=(200, 200, 200))
    out.save(path)
    print('preview', path)


if __name__ == '__main__':
    parts, nif = build()
    if '--preview' in sys.argv:
        preview(sys.argv[sys.argv.index('--preview') + 1], parts)
