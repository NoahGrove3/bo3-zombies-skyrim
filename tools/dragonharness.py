"""The glider dragon's harness (Noah: a saddle not to sit on, to hang from): two leather bands round its chest and belly,
fitted to its body, a padded belly plate between them with an iron ring on each side, and from the rings two straps down
to a bar with leather grips - the bar you hang from. The bands and plate move with its body (each vertex takes the skin
weights of the nearest point of the dragon); the straps and bar hang from DG_Trapeze, which the game swings.

Texture: Textures/CurseOfAlduin/Dragon/harness.dds (+ _n): left half leather (straps run along v), right half iron."""
import math, pathlib, sys
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).parent))

LEATHER = (0.02, 0.48)   # u range in the atlas
IRON = (0.52, 0.98)


class Mesh:
    def __init__(self):
        self.P, self.N, self.U, self.T, self.bone = [], [], [], [], []   # bone: None (from the body) or a name

    def add(self, P, U, T, bone=None, N=None):
        base = len(self.P)
        P = np.asarray(P, float)
        if N is None:
            N = smooth_normals(P, np.asarray(T))
        self.P += list(P); self.U += list(np.asarray(U, float)); self.N += list(N)
        self.T += [tuple(np.asarray(t) + base) for t in T]
        self.bone += [bone] * len(P)


def smooth_normals(P, T):
    N = np.zeros_like(P)
    for a, b, c in T:
        n = np.cross(P[b] - P[a], P[c] - P[a])
        N[a] += n; N[b] += n; N[c] += n
    return N / (np.linalg.norm(N, axis=1)[:, None] + 1e-9)


def tube(path, frame_side, w, h, u0, u1, closed=False, vscale=0.02):
    """a rectangular strap w wide, h thick along a path of points; frame_side(i) gives the strap's flat-side normal"""
    P, U, T = [], [], []
    n = len(path)
    L = 0.0
    corners = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
    cu = [u0, u1, u1, u0]
    for i in range(n):
        if i:
            L += np.linalg.norm(path[i] - path[i - 1])
        t = path[(i + 1) % n] - path[i - 1] if closed else path[min(i + 1, n - 1)] - path[max(i - 1, 0)]
        t = t / (np.linalg.norm(t) + 1e-9)
        nrm = frame_side(i); nrm = nrm - t * np.dot(nrm, t); nrm /= np.linalg.norm(nrm) + 1e-9
        side = np.cross(t, nrm)
        for k, (a, b) in enumerate(corners):
            P.append(path[i] + side * a + nrm * b)
            U.append((cu[k], L * vscale))
    segs = n if closed else n - 1
    for i in range(segs):
        j = (i + 1) % n
        for k in range(4):
            a, b = i * 4 + k, i * 4 + (k + 1) % 4
            c, d = j * 4 + k, j * 4 + (k + 1) % 4
            T += [(a, c, b), (b, c, d)]
    return P, U, T


def cylinder(p0, p1, r, u0, u1, seg=12):
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    ax = (p1 - p0) / np.linalg.norm(p1 - p0)
    s = np.cross(ax, [0, 0, 1.0]);
    if np.linalg.norm(s) < 1e-3: s = np.cross(ax, [0, 1.0, 0])
    s /= np.linalg.norm(s); t = np.cross(ax, s)
    P, U, T = [], [], []
    for k, p in enumerate((p0, p1)):
        for j in range(seg + 1):
            a = 2 * math.pi * j / seg
            P.append(p + (s * math.cos(a) + t * math.sin(a)) * r)
            U.append((u0 + (u1 - u0) * j / seg, k * np.linalg.norm(p1 - p0) * 0.02))
    for j in range(seg):
        a, b, c, d = j, j + 1, seg + 1 + j, seg + 2 + j
        T += [(a, b, c), (b, d, c)]
    # caps
    for k, p in enumerate((p0, p1)):
        ci = len(P); P.append(p); U.append(((u0 + u1) / 2, 0.5))
        for j in range(seg):
            a, b = k * (seg + 1) + j, k * (seg + 1) + j + 1
            T.append((ci, b, a) if k == 0 else (ci, a, b))
    return P, U, T


def torus(c, axis, R, r, u0, u1, seg=16, sides=8):
    axis = np.asarray(axis, float); axis /= np.linalg.norm(axis)
    s = np.cross(axis, [0, 0, 1.0]);
    if np.linalg.norm(s) < 1e-3: s = np.cross(axis, [0, 1.0, 0])
    s /= np.linalg.norm(s); t = np.cross(axis, s)
    P, U, T = [], [], []
    for i in range(seg + 1):
        a = 2 * math.pi * i / seg
        d = s * math.cos(a) + t * math.sin(a)
        for j in range(sides + 1):
            b = 2 * math.pi * j / sides
            P.append(np.asarray(c) + d * (R + r * math.cos(b)) + axis * r * math.sin(b))
            U.append((u0 + (u1 - u0) * j / sides, i / seg))
    for i in range(seg):
        for j in range(sides):
            a = i * (sides + 1) + j; b = a + 1; cc = a + sides + 1; d = cc + 1
            T += [(a, cc, b), (b, cc, d)]
    return P, U, T


def body_ring(P, mask, y, zc, bins=48, pct=70, push=3.0):
    """the body's outline round (0, y, zc): for each direction the radius most of the skin reaches (spikes left out)"""
    s = mask & (np.abs(P[:, 1] - y) < 14)
    q = P[s]
    ang = np.arctan2(q[:, 2] - zc, q[:, 0])
    rad = np.hypot(q[:, 0], q[:, 2] - zc)
    out = np.zeros(bins)
    for k in range(bins):
        a0 = -math.pi + 2 * math.pi * k / bins
        m = (ang >= a0) & (ang < a0 + 2 * math.pi / bins)
        out[k] = np.percentile(rad[m], pct) if m.sum() >= 2 else np.nan
    # fill gaps, smooth round the ring
    idx = np.arange(bins)
    good = ~np.isnan(out)
    out = np.interp(idx, idx[good], out[good], period=bins)
    for _ in range(3):
        out = 0.25 * np.roll(out, 1) + 0.5 * out + 0.25 * np.roll(out, -1)
    pts = []
    for k in range(bins):
        a = -math.pi + 2 * math.pi * (k + 0.5) / bins
        pts.append(np.array([math.cos(a) * (out[k] + push), y, zc + math.sin(a) * (out[k] + push)]))
    return np.array(pts)


def belly_z(P, mask, x, y):
    s = mask & (np.abs(P[:, 1] - y) < 12) & (np.abs(P[:, 0] - x) < 10)
    return P[s, 2].min() if s.any() else None


# where the bar hangs (the dragon's space): your hands are about here once it holds you
BAR_Y, BAR_Z, BAR_HALF = -1560.0, -48.0, 112.0
HANDS_X = 84.0
RING_Y = -1518.0


def build(rig):
    P = rig.pos
    body = [rig.skin.index(b) for b in ('DG_Body', 'DG_Spine1', 'DG_Spine2', 'DG_Chest')]
    mask = rig.W[:, body].sum(1) > 0.6
    m = Mesh()
    lu = LEATHER
    for y in (-1478.0, -1566.0):
        ring = body_ring(P, mask, y, 168.0, pct=92, push=3.5)
        m.add(*tube(ring, lambda i, r=ring: r[i] - np.array([0, r[i][1], 168.0]), 16.0, 3.0, lu[0], lu[1], closed=True))
        # a buckle on top, iron
        top = ring[np.argmax(ring[:, 2])]
        m.add(*torus(top + np.array([0, 0, 1.5]), (0, 0, 1), 7.0, 1.6, IRON[0], IRON[1]))
    # the belly plate: a padded leather sheet under the belly between the bands
    xs = np.linspace(-40, 40, 9); ys = np.linspace(-1478, -1566, 10)
    grid = np.zeros((len(ys), len(xs), 3))
    for i, y in enumerate(ys):
        for j, x in enumerate(xs):
            z = belly_z(P, mask, x, y)
            grid[i, j] = (x, y, (z if z is not None else 95.0) - 3.5)
    # (smoothed, so it reads as stiff leather)
    for _ in range(2):
        g = grid[:, :, 2].copy()
        grid[1:-1, 1:-1, 2] = 0.2 * (g[:-2, 1:-1] + g[2:, 1:-1] + g[1:-1, :-2] + g[1:-1, 2:] + g[1:-1, 1:-1])
    PP, UU, TT = [], [], []
    for i in range(len(ys)):
        for j in range(len(xs)):
            PP.append(grid[i, j]); UU.append((lu[0] + (lu[1] - lu[0]) * j / (len(xs) - 1), i * 0.12))
    for i in range(len(ys) - 1):
        for j in range(len(xs) - 1):
            a = i * len(xs) + j; b = a + 1; c = a + len(xs); d = c + 1
            TT += [(a, b, c), (b, d, c)]
    # its underside, a little below (a thickness)
    n0 = len(PP)
    for k in range(n0):
        PP.append(PP[k] - np.array([0, 0, 2.5])); UU.append(UU[k])
    TT += [(a + n0, c + n0, b + n0) for a, b, c in TT[:]]
    m.add(PP, UU, TT)
    # the rings, on the plate's sides
    rings = []
    for sx in (-1, 1):
        z = belly_z(P, mask, sx * 40, RING_Y) or 95.0
        c = np.array([sx * 44.0, RING_Y, z - 9.0])
        rings.append(c)
        m.add(*torus(c, (0, 1, 0), 8.0, 2.0, IRON[0], IRON[1]))
    pivot = (rings[0] + rings[1]) / 2
    pivot[0] = 0
    # the straps down to the bar, and the bar (on DG_Trapeze: the game swings them)
    for sx, rc in zip((-1, 1), rings):
        top = rc - np.array([0, 0, 8.0])
        end = np.array([sx * (BAR_HALF - 12), BAR_Y, BAR_Z + 6])
        path = np.array([top + (end - top) * t for t in np.linspace(0, 1, 8)])
        m.add(*tube(path, lambda i: np.array([0, 1.0, 0]), 14.0, 2.6, lu[0], lu[1]), bone='DG_Trapeze')
        # a loop of the strap round the bar
        m.add(*torus(np.array([sx * (BAR_HALF - 10), BAR_Y, BAR_Z]), (1, 0, 0), 6.5, 1.8, lu[0], lu[1], seg=12, sides=6), bone='DG_Trapeze')
    m.add(*cylinder((-BAR_HALF, BAR_Y, BAR_Z), (BAR_HALF, BAR_Y, BAR_Z), 4.2, IRON[0], IRON[1]), bone='DG_Trapeze')
    for sx in (-1, 1):   # the grips: leather wrapped round the bar where your hands go
        m.add(*cylinder((sx * (HANDS_X - 16), BAR_Y, BAR_Z), (sx * (HANDS_X + 16), BAR_Y, BAR_Z), 4.6, lu[0], lu[1], seg=14), bone='DG_Trapeze')
    return m, pivot


def weights(rig, m):
    """each vertex: the nearest dragon vertex's weights (body parts), or all on its bone (the bar and straps)"""
    P = np.array(m.P)
    W = np.zeros((len(P), len(rig.skin)))
    body = [i for i, b in enumerate(m.bone) if b is None]
    if body:
        from scipy.spatial import cKDTree
        tree = cKDTree(rig.pos)
        _, idx = tree.query(P[body], k=6)
        for row, ids in zip(body, idx):
            W[row] = rig.W[ids].mean(0)
    for i, b in enumerate(m.bone):
        if b is not None:
            W[i, rig.skin.index(b)] = 1.0
    return W


def texture(texdir):
    d = pathlib.Path(texdir) / 'CurseOfAlduin' / 'Dragon'
    d.mkdir(parents=True, exist_ok=True)
    S = 512
    rng = np.random.default_rng(3)
    u = (np.arange(S) + 0.5) / S
    v = (np.arange(S) + 0.5) / S
    def noise(octaves, seed):
        r = np.random.default_rng(seed)
        out = np.zeros((S, S))
        for k in range(octaves):
            n = 2 ** (k + 2)
            g = r.random((n, n))
            im = np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((S, S), Image.BICUBIC), float) / 255
            out += im / (k + 1)
        return (out - out.min()) / (out.max() - out.min())
    n1, n2 = noise(6, 1), noise(5, 2)
    U, Vv = np.meshgrid(u, v)
    leather = U < 0.5
    h = np.zeros((S, S))
    col = np.zeros((S, S, 3))
    # leather: dark oiled brown, fine grain, darker worn edges, a stitch line near each edge
    lu = (U - LEATHER[0]) / (LEATHER[1] - LEATHER[0])
    edge = np.clip(np.minimum(lu, 1 - lu) / 0.12, 0, 1)
    stitch = (np.abs(np.minimum(lu, 1 - lu) - 0.11) < 0.018) & ((Vv * 64) % 1 < 0.55)
    base = np.array([74, 46, 28]) / 255
    lcol = base[None, None, :] * (0.75 + 0.45 * n1[..., None]) * (0.6 + 0.4 * edge[..., None])
    lcol[stitch] = np.array([150, 128, 92]) / 255
    lh = 0.5 * n2 + 0.3 * edge - 0.4 * stitch
    # iron: blackened, mottled with rust at the edges
    icol = (np.array([58, 58, 62]) / 255)[None, None, :] * (0.7 + 0.6 * n1[..., None])
    rust = np.clip((n2 - 0.65) * 3, 0, 1)
    icol = icol * (1 - rust[..., None] * 0.6) + (np.array([110, 60, 30]) / 255)[None, None, :] * rust[..., None] * 0.6
    ih = 0.3 * n1
    col = np.where(leather[..., None], lcol, icol)
    h = np.where(leather, lh, ih)
    img = Image.fromarray((np.clip(col, 0, 1) * 255).astype(np.uint8), 'RGB').convert('RGBA')
    from dragontex import dds
    dds(img, d / 'harness.dds', 'DXT1')
    gy, gx = np.gradient(h)
    n = np.stack([-gx * 6, -gy * 6, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1)[..., None]
    nm = ((n * 0.5 + 0.5) * 255).astype(np.uint8)
    spec = np.where(leather, 90, 160).astype(np.uint8)   # (alpha: how glossy)
    nimg = Image.fromarray(np.dstack([nm, spec]), 'RGBA')
    dds(nimg, d / 'harness_n.dds', 'DXT5')
