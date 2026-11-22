"""(Noah, 2026-11-21: Phase 2 - "start phase 2 for prepping our mod for nexus by making our own stuff"; the underwear "permanently
attached to male and female bodies under clothing and armor" - it "can never come off") our own underwear in place of the
Underwear mod's: briefs and a bandeau for her, briefs for him, in an undyed linen of our own.

Made over the body meshes installed on your PC (read at build time from your install's femalebody_0/1.nif and malebody_0/1.nif):
their shape is not copied, only stood upon - the cloth lies where the body is, a hair off the skin, and bends with it (each of
its vertices on the bones the skin beneath it follows). The _0 and _1 meshes are made from the bodies' own _0 and _1, vertex for
vertex, so the weight slider shapes the cloth as it shapes the body. Worn in its own slot (52), under whatever you wear (the
plugin puts it on whenever nothing covers the body, src/Classes.cpp DressUnder).

    python3 tools/underwear.py [--render]
      -> assets/fx/Meshes/CurseOfAlduin/underwear/{female,male}_underwear_{0,1}.nif, Textures/CurseOfAlduin/underwear/linen_{d,n}.dds"""
import math, struct, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import gaussian_filter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gauntlet as g
from throwdagger import tileable, height_to_normal, tool

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME = pathlib.Path('/mnt/user-data/uploads/common--Skyrim Special Edition/Data/meshes/actors/character/character assets')
WORK = ROOT / 'build' / 'underwear'
MESHDIR = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'underwear'
g.TEXDIR = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'underwear'
g.TEXREL = 'CurseOfAlduin\\underwear\\'
g.WORK = WORK
TOOLS = g.TOOLS if hasattr(g, 'TOOLS') else pathlib.Path('/home/claude/tools')
PARTITION = 52     # (its own slot: the plugin's underwear armour, src/Classes.cpp)
S = 2048


class Body:
    """one body mesh's main shape, as nifexport gives it: positions (skin space), uvs, bones, weights, triangles"""
    def __init__(self, nif, shape_name):
        out = WORK / nif.stem
        subprocess.run([tool('nifexport'), str(nif), str(out)], check=True, capture_output=True)
        for f in sorted(out.glob('shape_*.txt')):
            if open(f).readline().rstrip('\n').split('\t')[-1] == shape_name:
                break
        else:
            raise SystemExit(f'{nif.name}: no shape {shape_name}')
        self.order, V, UV, B, W, F = [], [], [], [], [], []
        for l in open(f):
            q = l.rstrip('\n').split('\t')
            if q[0] == 'bone':
                self.order.append(q[1])
            elif l.startswith('v '):
                s = l.split(); V.append([float(x) for x in s[1:4]]); UV.append([float(x) for x in s[4:6]])
                B.append([int(x) for x in s[6:10]]); W.append([float(x) for x in s[10:14]])
            elif l.startswith('f '):
                F.append([int(x) for x in l.split()[1:4]])
        self.V, self.UV, self.B, self.W, self.F = np.array(V), np.array(UV), np.array(B), np.array(W), np.array(F)
        self.N = normals(self.V, self.F)

    def smooth(self, it=200):
        """the body with its small details smoothed away (Taubin's smoothing over its own triangles, seam twins joined: the
        size kept, the nipples, creases and folds gone) - the surface the cloth is laid over"""
        if getattr(self, '_vs', None) is None:
            avg = graph(self.F, len(self.V), self.V)
            S = self.V.copy()
            # (only her breasts are smoothed - smoothing the whole body draws the arms and the hollows in - and plainly, which takes
            # the nipples away and flattens the breasts a little as well: Noah, "flatten the chest more")
            w = np.clip(self.weight('Breast') * 1.5, 0, 1)
            # (and between her legs, front and under: Noah, "covering ... private parts so it's not outlines")
            c = crotch(self)
            w = np.maximum(w, smooth(self.V[:, 2], c + 12, c + 6) * smooth(self.V[:, 2], c - 4, c - 1) * smooth(np.abs(self.V[:, 0]), 7, 4)
                           * (np.sign(self.V[:, 1] - self.V[self.V[:, 2] < c + 8, 1].mean()) == front_sign(self))
                           * (1 - np.abs(self.N[:, 0])) ** 3)   # (not the inner thighs: smoothed, they draw across the gap)
            for _ in range(10):
                w = 0.5 * w + 0.5 * avg(w)
            for _ in range(it):
                S = S + 0.5 * w[:, None] * (avg(S) - S)
            self._vs = S
        return self._vs

    def weight(self, *names):
        """each vertex's weight on the bones whose names hold any of these"""
        m = np.zeros(len(self.V))
        for i, n in enumerate(self.order):
            if any(k in n for k in names):
                m += (self.W * (self.B == i)).sum(1)
        return m


def normals(V, F):
    """smooth vertex normals - the corners split at a uv seam given one normal between them, so the cloth stood off along them
    never opens a crack there"""
    _, first, inv = np.unique(np.round(V, 4), axis=0, return_index=True, return_inverse=True)
    rep = first[inv.ravel()]
    N = np.zeros_like(V)
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    for k in range(3):
        np.add.at(N, rep[F[:, k]], fn)
    N = N[rep]
    return N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-9)


# ================================================================== where the cloth lies
def front_sign(b):
    """+1 if the body faces +y, -1 if -y (the breasts, or the belly, stand out to the front)"""
    chest = b.weight('Breast')
    if (chest > 0.3).sum() > 20:
        return 1.0 if b.V[chest > 0.3, 1].mean() > b.V[:, 1].mean() else -1.0
    belly = (b.V[:, 2] > 80) & (b.V[:, 2] < 90) & (np.abs(b.V[:, 0]) < 4)
    return 1.0 if b.V[belly, 1].mean() > b.V[(b.V[:, 2] > 80) & (b.V[:, 2] < 90), 1].mean() else -1.0


def crotch(b):
    """the height where the legs meet: the lowest body point on the middle line between the hips and the knees"""
    mid = (np.abs(b.V[:, 0]) < 1.2) & (b.V[:, 2] > 45) & (b.V[:, 2] < 85)
    return b.V[mid, 2].min()


def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def briefs(b, female):
    """the pelvis wrapped from a little below the navel to a cut across the top of each thigh, higher at the hip (a brief's leg
    opening), fuller behind - as a field over the body, positive where the cloth is (its edge where it crosses 0, cut smooth)"""
    fs = front_sign(b)
    z0 = crotch(b)
    X, Y, Z = b.V[:, 0], b.V[:, 1] * fs, b.V[:, 2]
    fr = smooth(Y, -2.0, 2.0)                     # (0 behind, 1 in front)
    top = z0 + (13.5 if female else 14.5) + 1.2 * (1 - fr)   # (Noah: "fuller coverage" - up to the navel's line)
    side = np.maximum(np.abs(X) - 2.0, 0.0)
    cut = z0 - 6.0 + 0.04 * side - 0.5 * (1 - fr)   # (Noah: "fuller coverage" - shorts: a level hem a hand below the crotch)
    arms = b.weight('Arm', 'Hand', 'Clavicle')
    return np.minimum.reduce([top - Z, Z - cut, (0.3 - arms) * 20.0, Z - (z0 - 4.0)])


def bandeau(b):
    """her chest wrapped: a band from just under the breasts to above them, round the back, covering both"""
    fs = front_sign(b)
    breast = b.weight('Breast')
    under = b.V[breast > 0.3, 2].min()
    over = np.percentile(b.V[breast > 0.3, 2], 92)
    X, Y, Z = b.V[:, 0], b.V[:, 1] * fs, b.V[:, 2]
    fr = smooth(Y, -3.0, 2.0)
    arms = b.weight('UpperArm', 'Forearm', 'Hand', 'UpperarmTwist')
    upper = under + 7.0 + (over + 3.0 - under - 7.0) * fr   # (taller: well over the breasts in front, a deep band behind)
    band = np.minimum.reduce([Z - (under - 2.5), upper - Z, 18.5 - np.abs(X), (0.25 - arms) * 20.0])
    return band


def clip(F, f):
    """the triangles where the field is positive, cut along its 0 line: kept corners, and new corners on the cut edges (each
    a blend of an edge's two ends, shared by the triangles either side) - the recipe, applied alike to the _0 and _1 bodies"""
    new = {}                       # (i, j) -> index among the new corners, with its blend t toward j
    recipe = []                    # each corner: (i, j, t)
    def kept(i):
        k = ('v', i)
        if k not in new: new[k] = len(recipe); recipe.append([(i, 1.0)])
        return new[k]
    def cut(i, j):
        a, c = (i, j) if i < j else (j, i)
        k = ('e', a, c)
        if k not in new:
            t = float(np.clip(f[a] / (f[a] - f[c]), 0, 1)); new[k] = len(recipe); recipe.append([(a, 1 - t), (c, t)])
        return new[k]
    out = []
    for tri in F:
        ins = [f[v] >= 0 for v in tri]
        n = sum(ins)
        if n == 0: continue
        if n == 3: out.append([kept(v) for v in tri]); continue
        poly = []
        for k in range(3):
            a, c = tri[k], tri[(k + 1) % 3]
            if f[a] >= 0: poly.append(kept(a))
            if (f[a] >= 0) != (f[c] >= 0): poly.append(cut(a, c))
        for k in range(1, len(poly) - 1):
            out.append([poly[0], poly[k], poly[k + 1]])
    return recipe, np.array(out)


def blend(b, recipe, offset, V=None):
    """the recipe's corners on this body (each a blend of the body's own vertices): position (stood off the skin), uv, and
    bones (the blended weights, the four strongest kept)"""
    P, UV, BW = [], [], []
    for parts in recipe:
        P.append(sum((b.V if V is None else V)[i] * x for i, x in parts) + sum(b.N[i] * x for i, x in parts) * offset)
        UV.append(sum(b.UV[i] * x for i, x in parts))
        w = {}
        for i, x in parts:
            for k in range(4):
                if b.W[i][k] > 0: w[b.B[i][k]] = w.get(b.B[i][k], 0) + b.W[i][k] * x
        top = sorted(w.items(), key=lambda kv: -kv[1])[:4]
        tot = sum(y for _, y in top) or 1
        BW.append([(bb, y / tot) for bb, y in top])
    return np.array(P), np.array(UV), BW


def fill_holes(b, recipe, Fl, most=14.0):
    """the body's own openings (its crotch is a separate shape on 3BA, leaving a hole in the body there) closed in the cloth:
    every small loop of open edge - not the cloth's own cut edges - filled with a fan from its middle"""
    P, _, _ = blend(b, recipe, 0.0)
    _, first, inv = np.unique(np.round(P, 4), axis=0, return_index=True, return_inverse=True)
    rp = first[inv.ravel()]
    E = np.sort(np.concatenate([rp[Fl[:, [0, 1]]], rp[Fl[:, [1, 2]]], rp[Fl[:, [2, 0]]]]), 1)
    uniq, cnt = np.unique(E, axis=0, return_counts=True)
    nxt = {}
    for t in Fl:
        for k in range(3):
            a, c = rp[t[k]], rp[t[(k + 1) % 3]]
            key = (min(a, c), max(a, c))
            nxt.setdefault(key, []).append((a, c))
    bnd = {tuple(e) for e, n in zip(uniq, cnt) if n == 1}
    succ = {}
    for e in bnd:
        a, c = nxt[e][0]
        succ[a] = c
    seen = set(); loops = []
    for start in list(succ):
        if start in seen: continue
        loop = [start]; seen.add(start); v = succ[start]
        while v != start and v in succ and v not in seen:
            loop.append(v); seen.add(v); v = succ[v]
        if v == start: loops.append(loop)
    recipe = list(recipe); Fl = [list(t) for t in Fl]
    filled = 0
    for loop in loops:
        per = sum(np.linalg.norm(P[loop[k]] - P[loop[(k + 1) % len(loop)]]) for k in range(len(loop)))
        if per > most or len(loop) < 3: continue
        parts = {}
        for v in loop:
            for i, x in recipe[v]: parts[i] = parts.get(i, 0) + x / len(loop)
        c = len(recipe); recipe.append(list(parts.items()))
        for k in range(len(loop)):
            Fl.append([loop[(k + 1) % len(loop)], loop[k], c])   # (wound against the open edge's own direction)
        filled += 1
    return recipe, np.array(Fl), filled


def graph(F, n, P=None):
    """averaging over each vertex's neighbours - the corners split at a uv seam (one place, two vertices) joined as one"""
    rep = np.arange(n)
    if P is not None:
        _, first, inv = np.unique(np.round(P, 4), axis=0, return_index=True, return_inverse=True)
        rep = first[inv.ravel()]
    nb = [[] for _ in range(n)]
    for a, c, d in F:
        a, c, d = rep[a], rep[c], rep[d]
        nb[a] += [c, d]; nb[c] += [a, d]; nb[d] += [a, c]
    for i in range(n):
        if rep[i] != i: nb[i] = nb[rep[i]]
    deg = np.array([max(1, len(x)) for x in nb], float)
    rows = np.concatenate([[i] * len(x) for i, x in enumerate(nb)]).astype(int); cols = np.concatenate([x for x in nb]).astype(int)
    def avg(X):
        out = np.zeros_like(X); np.add.at(out, rows, X[cols]); return out / (deg[:, None] if X.ndim == 2 else deg)
    return avg


def tips(b):
    """her nipples: on each side the breast's front-most point, and how far it stands out of the ring of skin round it"""
    fs = front_sign(b)
    w = b.weight('Breast')
    out = []
    for sgn in (-1, 1):
        m = (w > 0.3) & (np.sign(b.V[:, 0]) == sgn)
        if not m.any(): continue
        i = np.flatnonzero(m)[np.argmax(b.V[m, 1] * fs)]
        d = np.linalg.norm(b.V - b.V[i], axis=1)
        ring = (d > 1.0) & (d < 1.6)
        h = float(np.dot(b.V[i] - b.V[ring].mean(0), b.N[i])) if ring.any() else 0.0
        out.append((b.V[i], max(h, 0.0)))
    return out


def drape(P0, Fl, offset, tipl=(), it=600, P0s=None, skin=0.1):
    """(Noah, 2026-11-21: "covering the nipples and private parts so it's not outlines under the underwear") cloth is not paint:
    it is drawn taut over the body and held off it - relaxed again and again over its own triangles (which pulls it tight and
    smooth) and pushed back out wherever that would bring it nearer the skin than its thickness. What it settles to is a smooth
    sheet over the body: across the hollows (the crotch, between the breasts) and over every small rise, with no print of what
    is under it. Its edges stay where they were cut."""
    n = len(P0)
    avg = graph(Fl, n, P0)
    N = normals(P0, Fl)
    E = np.sort(np.concatenate([Fl[:, [0, 1]], Fl[:, [1, 2]], Fl[:, [2, 0]]]), 1)
    uniq, cnt = np.unique(E, axis=0, return_counts=True)
    edge = np.zeros(n, bool); edge[uniq[cnt == 1].ravel()] = True
    _, first, inv = np.unique(np.round(P0, 4), axis=0, return_index=True, return_inverse=True)
    rep = first[inv.ravel()]
    edge_w = np.zeros(n, bool); np.logical_or.at(edge_w, rep, edge); edge = edge_w[rep]   # (a seam's twin of an edge corner is an edge corner)
    # (Noah: "flatten the chest more") held off the body as smoothed - its nipples and folds taken away - by the cloth's
    # thickness, and never nearer the real skin than a hair: thick enough over the smoothed body that nothing small under it
    # ever reaches the cloth, so nothing prints through
    Ps = P0 if P0s is None else P0s
    # (and held off it along the smoothed body's own normals: the real skin's turn every which way round a nipple, and a
    # thickness laid along them prints the nipple again)
    Nr = N
    N = normals(Ps, Fl)
    # where the real body still stands out of the smoothed one (her nipples, mostly), the cloth's thickness there grows to clear
    # it with a hair to spare - and that thickening spread wide and soft (grown out over the neighbours, then blurred), so
    # what rises is a smooth swell of padding, not a point
    d0 = np.maximum(((P0 - Ps) * N).sum(1) + skin - offset, 0)
    Eu = np.unique(np.sort(rep[E], 1), axis=0)
    dl = d0[rep].copy()
    for _ in range(8):
        np.maximum.at(dl, Eu[:, 0], dl[Eu[:, 1]]); np.maximum.at(dl, Eu[:, 1], dl[Eu[:, 0]])
    for _ in range(150):
        dl = 0.5 * dl + 0.5 * avg(dl)
    dl = dl[rep]
    if (d0 > 0).any():   # (the blur takes the top off; raised again until the swell clears every point under it)
        dl = dl * min(2.0, max(1.0, float((d0[d0 > 0] / np.maximum(dl[d0 > 0], 1e-6)).max())))
    offset = offset + np.maximum(dl, d0)
    base = Ps + N * offset[:, None]
    held = P0 + Nr * offset[:, None]   # (its cut edges stay on the real body: the smoothed one has slid down the breasts' slopes)
    S = base.copy(); S[edge] = held[edge]
    # (stiff, like a knit band and not a film: each step bends the cloth smooth as well as pulling it taut - a film stretched
    # over a small rise comes to a point there, a stiff band rounds over it)
    for k in range(it * 4):
        L = avg(S) - S
        S = S + (0.6 if k < it else 0.15) * L - 0.12 * (avg(L) - L)
        d = ((S - Ps) * N).sum(1)
        S = S + N * np.maximum(offset - d, 0)[:, None]
        d = ((S - P0) * Nr).sum(1)
        S = S + Nr * np.maximum(skin - d, 0)[:, None]
        S[edge] = held[edge]
    return S


def piece(b, recipe, Fl, offset, tipl=()):
    P0, UV, BW = blend(b, recipe, 0.0)
    P0s, _, _ = blend(b, recipe, 0.0, b.smooth())
    return drape(P0, Fl, offset, tipl, P0s=P0s), UV, BW, Fl


# ================================================================== the linen
def linen_tex(edges_uv):
    """undyed linen: a fine plain weave, slubs in the thread, a soft mottle; a turned hem along every edge of the cloth (its
    uv edges drawn in) with a row of stitches beside it"""
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    wx = 0.5 + 0.5 * np.sin(u * S * math.pi / 3.0); wy = 0.5 + 0.5 * np.sin(v * S * math.pi / 3.0)
    # (Noah: "give it texture instead of a flat color") a soft cotton knit: fine diagonal ribs (a twill, reading the same whichever
    # way the body's uv runs), each rib's loops picked out, a heathered fleck through the black
    # (no direction to it: the body's uv islands lie every which way, and a rib or a twill would turn at each seam)
    knit = tileable(S, S, 0.35, 7105); knit2 = tileable(S, S, 0.6, 7106)
    z = lambda x: (x - x.mean()) / (x.std() + 1e-9)
    weave = 0.5 + 0.16 * np.clip(z(knit), -2.5, 2.5) + 0.1 * np.clip(z(knit2), -2.5, 2.5)
    slub = tileable(S, S, 0.9, 7101, aniso=(1.0, 0.04)); slub2 = tileable(S, S, 0.9, 7102, aniso=(0.04, 1.0))
    mottle = tileable(S, S, 2.0, 7103)
    fleck = np.clip((z(tileable(S, S, 0.2, 7104)) - 1.6) * 1.5, 0, 1)   # (a heather: a grey thread here and there through the black)
    lum = 0.86 + 0.6 * (weave - 0.5) + 0.08 * (slub - 0.5) + 0.06 * (slub2 - 0.5) + 0.08 * (mottle - 0.5) + 0.9 * fleck
    col = np.array([34, 32, 36], np.float32)[None, None, :] * lum[..., None]   # (Noah: "make the underwear black")
    # the hem: the uv edges of the cloth, drawn wide (a folded band) and a stitch line just inside
    hem = Image.new('L', (S, S), 0); dh = ImageDraw.Draw(hem)
    stitch = Image.new('L', (S, S), 0); ds = ImageDraw.Draw(stitch)
    for (a, b) in edges_uv:
        p0 = (a[0] * S, (1 - a[1]) * S) if False else (a[0] * S, a[1] * S)
        p1 = (b[0] * S, b[1] * S)
        dh.line([p0, p1], fill=255, width=46)   # (the elastic band: wide, ribbed across)
        ds.line([p0, p1], fill=255, width=3)
    hemA = gaussian_filter(np.asarray(hem, np.float32) / 255, 2.0)
    stA = np.asarray(stitch, np.float32) / 255
    dash = (np.sin((u + v) * S * math.pi / 7.0) > 0.2).astype(np.float32)
    rib = 0.5 + 0.5 * np.sin((u - v) * S * math.pi / 4.0)
    col = col * (1 + (0.18 + 0.25 * rib[..., None]) * hemA[..., None]) + np.array([26, 25, 28]) * (stA * dash)[..., None] * 0.6   # (on black: the band a shade lighter and ribbed, its stitches grey)
    D = g.save(col, 'linen_d.dds')
    h = 0.6 * weave + 0.2 * slub + 0.15 * slub2 + hemA * (1.0 + 0.6 * rib) - 0.6 * stA * dash
    nrm = height_to_normal(gaussian_filter(h, 0.6), 3.0)
    spec = np.full((S, S), 18.0, np.float32)
    N = g.save(np.dstack([nrm, spec]), 'linen_n.dds', True)
    return D, N


def boundary_uv(Fl, UV, P):
    """the cloth's open edges (used by one triangle only), as uv segments"""
    # (the edges of the cloth itself - not the uv seams inside it, where one place is two corners)
    _, first, inv = np.unique(np.round(P, 4), axis=0, return_index=True, return_inverse=True)
    rep = first[inv.ravel()]
    E = np.concatenate([Fl[:, [0, 1]], Fl[:, [1, 2]], Fl[:, [2, 0]]])
    Er = np.sort(rep[E], 1)
    uniq, inv2, cnt = np.unique(Er, axis=0, return_inverse=True, return_counts=True)
    return [(UV[a], UV[b]) for (a, b), c in zip(E, cnt[inv2.ravel()]) if c == 1]


# ================================================================== written
# ================================================================== the sliders
# (Noah: "make sure the underwear properly fits the characters model even with changes made from the race menu like player
# weight and size") weight is the _0/_1 pair, size is the skeleton; the body's own sliders (RaceMenu's body morphs, BodyGen,
# BodySlide's) move her skin by the morphs in femalebody.tri - and RaceMenu moves anything worn the same way when it has a .tri
# of its own (named by a BODYTRI string on its root) whose shapes carry the same morph names. Ours are made from hers: every
# corner of the cloth is a blend of the body's own vertices, so each of its morphs is that same blend of hers.
TRI_SRC = GAME / 'femalebody.tri'
TRI_OUT = 'CurseOfAlduin\\underwear\\female_underwear.tri'


def read_tri(path, shape):
    """BodySlide's .tri: each morph of one shape, as {name: {vertex: (dx, dy, dz)}}"""
    d = open(path, 'rb').read(); assert d[:4] == b'PIRT'
    p = 4; ns, = struct.unpack_from('<H', d, p); p += 2
    for _ in range(ns):
        L = d[p]; p += 1; name = d[p:p + L].decode(); p += L
        nm, = struct.unpack_from('<H', d, p); p += 2
        morphs = {}
        for _ in range(nm):
            L = d[p]; p += 1; mn = d[p:p + L].decode(); p += L
            mult, cnt = struct.unpack_from('<fH', d, p); p += 6
            a = np.frombuffer(d, '<i2', cnt * 4, p).reshape(cnt, 4); p += 8 * cnt
            morphs[mn] = (a[:, 0].astype(np.uint16).astype(int), a[:, 1:].astype(float) * mult)
        if name == shape:
            return morphs
    raise SystemExit(f'{path}: no shape {shape}')


def cloth_morphs(b, P, Fl, morphs, k=40):
    """the body's morphs carried onto the cloth: each corner moved as the skin round it moves (its 40 nearest body vertices,
    the nearest counting most - its own recipe alone is not enough: a corner held up over a nipple sits on the skin beside it,
    and a slider that swells the nipple more than that skin would bring the nipple through), and never less far out than the
    furthest-out of them. The nipple and areola sliders only ever push it out: it is padded over them, it does not follow them
    in. (Checked over all 154 of the body's sliders at full: no corner of the cloth goes under the skin.)"""
    from scipy.spatial import cKDTree
    dd, ii = cKDTree(b.V).query(P, k=k)
    Nc = normals(P, Fl)
    # (only the skin it lies on: skin facing the same way - not the other thigh across the gap at the crotch, which a thigh
    # slider swells towards it)
    face = np.clip((b.N[ii] * Nc[:, None, :]).sum(2), 0, 1) ** 2
    face[face.sum(1) < 1e-6] = 1
    w = face / np.maximum(dd, 0.05) ** 2; w /= w.sum(1, keepdims=True)
    out = {}
    for mn, (ids, dl) in morphs.items():
        D = np.zeros_like(b.V); D[ids] = dl
        Dn = np.where(face > 0.05, (D[ii] * Nc[:, None, :]).sum(2), -1e9).max(1)
        Dn = np.where(Dn < -1e8, 0, Dn)
        C = (D[ii] * w[..., None]).sum(1)
        C = C + Nc * np.maximum(Dn - (C * Nc).sum(1), 0)[:, None]
        if 'Nipple' in mn or 'Areola' in mn:
            C = Nc * np.maximum(Dn, 0)[:, None]
        if np.abs(C).max() > 1e-4: out[mn] = C
    return out


def write_tri(path, shapes):
    """shapes: {shape name: {morph name: (n, 3) deltas}}"""
    b = bytearray(b'PIRT') + struct.pack('<H', len(shapes))
    for sn, ms in shapes.items():
        b += bytes([len(sn)]) + sn.encode() + struct.pack('<H', len(ms))
        for mn, C in ms.items():
            keep = np.flatnonzero(np.abs(C).max(1) > 1e-4)
            mult = float(np.abs(C).max()) / 32767.0
            q = np.round(C[keep] / mult).astype('<i2')
            b += bytes([len(mn)]) + mn.encode() + struct.pack('<fH', mult, len(keep))
            rec = np.zeros((len(keep), 4), '<i2'); rec[:, 0] = keep.astype(np.uint16).view(np.int16); rec[:, 1:] = q
            b += rec.tobytes()
    b += struct.pack('<H', 0)   # (no uv morphs)
    path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(bytes(b))


def write(name, body_nif, pieces, mat):
    lines = [f'template {body_nif}', f'out {MESHDIR / (name + ".nif")}', f'partition {PARTITION}', f'bodytri {TRI_OUT}']
    for sname, (P, UV, BW, Fl) in pieces.items():
        N = normals(P, Fl)
        lines.append(f'shape {sname} {mat}')
        for k in range(len(P)):
            ws = ' '.join(f'{bb} {x:.4f}' for bb, x in BW[k] if x > 0.001)
            lines.append(f'v {P[k][0]:.4f} {P[k][1]:.4f} {P[k][2]:.4f} {UV[k][0]:.5f} {UV[k][1]:.5f} {N[k][0]:.4f} {N[k][1]:.4f} {N[k][2]:.4f} {ws}')
        lines += [f't {a} {c} {d}' for a, c, d in Fl]
    jf = WORK / f'{name}.job'; jf.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([tool('skinglove'), str(jf)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('skinglove failed')
    print(name, ':', ', '.join(f'{k} {len(v[3])} triangles' for k, v in pieces.items()))


def render(name, b, pieces):
    """a look over it: the body in plain skin, the cloth over it, front, side and back (the cloth's texture not drawn - its shape)"""
    out = WORK / f'{name}_preview.png'
    frames = []
    for yaw in (0, 90, 180):
        a = math.radians(yaw)
        Vs = [b.V]; Fs = [b.F]; Cs = [np.tile([0.85, 0.68, 0.58], (len(b.V), 1))]; Us = [np.zeros((len(b.V), 2))]; Ts = [np.zeros(len(b.F), bool)]
        o = len(b.V)
        for P, UV, BW, Fl in pieces.values():
            Vs.append(P); Fs.append(Fl + o); Cs.append(np.tile([0.16, 0.15, 0.17], (len(P), 1))); Us.append(UV); Ts.append(np.ones(len(Fl), bool)); o += len(P)
        V = np.vstack(Vs); F = np.vstack(Fs); C = np.vstack(Cs)
        frames.append(raster(V, F, C, a, np.vstack(Us), np.concatenate(Ts), TEX))
        if yaw == 0:   # (and close: the knit and the band can be seen)
            close = [raster(V, F, C, math.radians(y), np.vstack(Us), np.concatenate(Ts), TEX, W=800, H=1000, span=48.0, low=56.0) for y in (20, 160)]
    Image.fromarray(np.hstack(frames)).save(out)
    Image.fromarray(np.hstack(close)).save(WORK / f'{name}_close.png')
    return out


TEX = None


def raster(V, F, C, yaw, UV=None, textured=None, tex=None, W=520, H=900, span=110.0, low=8.0):
    R = np.array([[math.cos(yaw), -math.sin(yaw), 0], [math.sin(yaw), math.cos(yaw), 0], [0, 0, 1]])
    P = V @ R.T
    N = normals(P, F)
    sc = H / span
    sx = W / 2 + P[:, 0] * sc; sy = H - (P[:, 2] - low) * sc; dep = P[:, 1]
    img = np.full((H, W, 3), 30, np.uint8); zb = np.full((H, W), 1e9)
    L = np.array([0.3, -0.6, 0.75]); L /= np.linalg.norm(L)
    for fi, f in enumerate(F):
        x, y, d = sx[f], sy[f], dep[f]
        x0, x1 = int(max(0, x.min())), int(min(W - 1, x.max() + 1)); y0, y1 = int(max(0, y.min())), int(min(H - 1, y.max() + 1))
        if x0 > x1 or y0 > y1: continue
        den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(den) < 1e-9: continue
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1] + 0.5
        a = ((y[1] - y[2]) * (xx - x[2]) + (x[2] - x[1]) * (yy - y[2])) / den
        c2 = ((y[2] - y[0]) * (xx - x[2]) + (x[0] - x[2]) * (yy - y[2])) / den
        c3 = 1 - a - c2
        ins = (a >= 0) & (c2 >= 0) & (c3 >= 0)
        if not ins.any(): continue
        z = a * d[0] + c2 * d[1] + c3 * d[2]
        n = a[..., None] * N[f[0]] + c2[..., None] * N[f[1]] + c3[..., None] * N[f[2]]
        n /= np.linalg.norm(n, axis=-1, keepdims=True) + 1e-9
        lit = 0.35 + 0.65 * np.clip(np.abs(n @ L), 0, 1)
        if tex is not None and textured is not None and textured[fi]:
            uu = a * UV[f[0], 0] + c2 * UV[f[1], 0] + c3 * UV[f[2], 0]; vv = a * UV[f[0], 1] + c2 * UV[f[1], 1] + c3 * UV[f[2], 1]
            th, tw_ = tex.shape[:2]
            base = tex[np.clip((vv * th).astype(int), 0, th - 1), np.clip((uu * tw_).astype(int), 0, tw_ - 1)] * 1.6
            col = base * lit[..., None]
        else:
            col = (a[..., None] * C[f[0]] + c2[..., None] * C[f[1]] + c3[..., None] * C[f[2]]) * lit[..., None]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m = ins & (z < sub)
        sub[m] = z[m]
        img[y0:y1 + 1, x0:x1 + 1][m] = (np.clip(col[m], 0, 1) * 255).astype(np.uint8)
    return img


def build(do_render=False):
    WORK.mkdir(parents=True, exist_ok=True); MESHDIR.mkdir(parents=True, exist_ok=True)
    tool('nifexport'); tool('skinglove')
    made = {}
    edges = []
    # (him: the male bodies in use already wear theirs - HIMBO its boxers, Skyrim's own its loincloth - so only she is dressed here)
    for sex, shape in (('female', '3BA'),):
        bodies = {w: Body(GAME / f'{sex}body_{w}.nif', shape) for w in (0, 1)}
        b1 = bodies[1]
        r, f = clip(b1.F, briefs(b1, sex == 'female'))
        r, f, nh = fill_holes(b1, r, f)
        print(f'{sex}: {nh} openings in the body closed under the briefs')
        parts = {'briefs': ((r, f), 0.35)}
        if sex == 'female':
            parts['bandeau'] = (clip(b1.F, bandeau(b1)), 0.55)
        made[sex] = (bodies, parts)
        for (recipe, Fl), off in parts.values():
            _, UV, _, _ = piece(b1, recipe, Fl, off)
            edges += boundary_uv(Fl, UV, blend(b1, recipe, 0.0)[0])
    D, N = linen_tex(edges)
    global TEX
    TEX = np.asarray(Image.open(WORK / 'linen_d.dds.png').convert('RGB'), np.float32) / 255
    mat = f'diffuse={D} normal={N} spec=0.12 gloss=8'
    for sex, (bodies, parts) in made.items():
        for w, b in bodies.items():
            tipl = ()   # (the nipple mound tried: it showed more, not less - left out)
            pieces = {f'{sex}_{k}': piece(b, recipe, Fl, off, tipl if k == 'bandeau' else ()) for k, ((recipe, Fl), off) in parts.items()}
            write(f'{sex}_underwear_{w}', GAME / f'{sex}body_{w}.nif', pieces, mat)
            if w == 1: worn = pieces
            if do_render and w == 1:
                print('preview', render(f'{sex}_underwear', b, pieces))
        morphs = read_tri(TRI_SRC, '3BA')
        b1 = bodies[1]
        assert max(ids.max() for ids, _ in morphs.values()) < len(b1.V), 'femalebody.tri is not this body'
        shapes = {sn: cloth_morphs(b1, P, Fl, morphs) for sn, (P, UV, BW, Fl) in worn.items()}
        write_tri(MESHDIR / f'{sex}_underwear.tri', shapes)
        print('morphs', {k: len(v) for k, v in shapes.items()})


if __name__ == '__main__':
    build('--render' in sys.argv)
