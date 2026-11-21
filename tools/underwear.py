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
import math, pathlib, subprocess, sys
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
    top = z0 + (11.0 if female else 12.5) + 1.2 * (1 - fr)
    side = np.maximum(np.abs(X) - 2.0, 0.0)
    cut = z0 - 0.6 + (0.32 + 0.23 * fr) * side - 1.8 * (1 - fr)
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
    upper = under + 4.5 + (over - under - 4.5) * fr
    band = np.minimum.reduce([Z - (under - 1.6), upper - Z, 18.5 - np.abs(X), (0.25 - arms) * 20.0])
    cover = np.minimum((breast - 0.2) * 20.0, over + 0.5 - Z)
    return np.maximum(band, cover)


def clip(F, f):
    """the triangles where the field is positive, cut along its 0 line: kept corners, and new corners on the cut edges (each
    a blend of an edge's two ends, shared by the triangles either side) - the recipe, applied alike to the _0 and _1 bodies"""
    new = {}                       # (i, j) -> index among the new corners, with its blend t toward j
    recipe = []                    # each corner: (i, j, t)
    def kept(i):
        k = ('v', i)
        if k not in new: new[k] = len(recipe); recipe.append((i, i, 0.0))
        return new[k]
    def cut(i, j):
        a, c = (i, j) if i < j else (j, i)
        k = ('e', a, c)
        if k not in new:
            t = f[a] / (f[a] - f[c]); new[k] = len(recipe); recipe.append((a, c, float(np.clip(t, 0, 1))))
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


def blend(b, recipe, offset):
    """the recipe's corners on this body: position (stood off the skin), uv, and bones (each end's weights blended, the four
    strongest kept)"""
    P, UV, BW = [], [], []
    for i, j, t in recipe:
        P.append(b.V[i] * (1 - t) + b.V[j] * t + (b.N[i] * (1 - t) + b.N[j] * t) * offset)
        UV.append(b.UV[i] * (1 - t) + b.UV[j] * t)
        w = {}
        for k in range(4):
            if b.W[i][k] > 0: w[b.B[i][k]] = w.get(b.B[i][k], 0) + b.W[i][k] * (1 - t)
            if b.W[j][k] > 0: w[b.B[j][k]] = w.get(b.B[j][k], 0) + b.W[j][k] * t
        top = sorted(w.items(), key=lambda kv: -kv[1])[:4]
        tot = sum(x for _, x in top) or 1
        BW.append([(bb, x / tot) for bb, x in top])
    return np.array(P), np.array(UV), BW


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


def drape(P0, Fl, offset, tipl=(), it=220, most=4.0, r=2.6):
    """cloth is not paint: it bridges the hollows (the crotch, the small of the back) instead of following every dip of the
    skin, and spreads over a small rise (a nipple) as a sheet would - a gentle swell, not a print of it. Stood off the skin by
    its thickness; lifted, a little at a time and only ever outward, wherever its neighbours lie higher; and round each nipple
    raised to a soft mound as high as the nipple itself (the nipple under it not raised at all: the cloth just touches it)"""
    avg = graph(Fl, len(P0), P0)
    N = normals(P0, Fl)
    P = P0 + N * offset
    extra = np.zeros(len(P0))
    for c, h in tipl:
        d = np.linalg.norm(P0 - c, axis=1)
        env = h * np.clip(1 - (d / r) ** 2, 0, 1) ** 2
        own = h * np.clip(1 - (d / 0.9) ** 2, 0, 1)        # (the nipple's own rise near its tip)
        extra = np.maximum(extra, env - own)
    P = P + N * np.clip(extra, 0, None)[:, None]
    h = np.zeros(len(P0))
    for _ in range(it):
        S = P + N * h[:, None]
        h = np.minimum(h + np.maximum(((avg(S) - S) * N).sum(1), 0) * 0.6, most)
    return P + N * h[:, None]


def piece(b, recipe, Fl, offset, tipl=()):
    P0, UV, BW = blend(b, recipe, 0.0)
    return drape(P0, Fl, offset, tipl), UV, BW, Fl


# ================================================================== the linen
def linen_tex(edges_uv):
    """undyed linen: a fine plain weave, slubs in the thread, a soft mottle; a turned hem along every edge of the cloth (its
    uv edges drawn in) with a row of stitches beside it"""
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    wx = 0.5 + 0.5 * np.sin(u * S * math.pi / 3.0); wy = 0.5 + 0.5 * np.sin(v * S * math.pi / 3.0)
    checker = (np.sin(u * S * math.pi / 3.0) * np.sin(v * S * math.pi / 3.0))
    weave = 0.5 + 0.5 * np.sign(checker) * np.abs(checker) ** 0.5
    slub = tileable(S, S, 0.9, 7101, aniso=(1.0, 0.04)); slub2 = tileable(S, S, 0.9, 7102, aniso=(0.04, 1.0))
    mottle = tileable(S, S, 2.0, 7103)
    lum = 0.88 + 0.07 * (weave - 0.5) + 0.06 * (slub - 0.5) + 0.05 * (slub2 - 0.5) + 0.05 * (mottle - 0.5)
    col = np.array([206, 194, 170], np.float32)[None, None, :] * lum[..., None]
    # the hem: the uv edges of the cloth, drawn wide (a folded band) and a stitch line just inside
    hem = Image.new('L', (S, S), 0); dh = ImageDraw.Draw(hem)
    stitch = Image.new('L', (S, S), 0); ds = ImageDraw.Draw(stitch)
    for (a, b) in edges_uv:
        p0 = (a[0] * S, (1 - a[1]) * S) if False else (a[0] * S, a[1] * S)
        p1 = (b[0] * S, b[1] * S)
        dh.line([p0, p1], fill=255, width=22)
        ds.line([p0, p1], fill=255, width=3)
    hemA = gaussian_filter(np.asarray(hem, np.float32) / 255, 2.0)
    stA = np.asarray(stitch, np.float32) / 255
    dash = (np.sin((u + v) * S * math.pi / 7.0) > 0.2).astype(np.float32)
    col = col * (1 - 0.1 * hemA[..., None]) - np.array([40, 38, 34]) * (stA * dash)[..., None] * 0.5
    D = g.save(col, 'linen_d.dds')
    h = 0.6 * weave + 0.2 * slub + 0.15 * slub2 + 1.2 * hemA - 0.6 * stA * dash
    nrm = height_to_normal(gaussian_filter(h, 0.6), 3.0)
    spec = np.full((S, S), 18.0, np.float32)
    N = g.save(np.dstack([nrm, spec]), 'linen_n.dds', True)
    return D, N


def boundary_uv(Fl, UV):
    """the cloth's open edges (used by one triangle only), as uv segments"""
    E = np.sort(np.concatenate([Fl[:, [0, 1]], Fl[:, [1, 2]], Fl[:, [2, 0]]]), 1)
    uniq, cnt = np.unique(E, axis=0, return_counts=True)
    return [(UV[a], UV[b]) for a, b in uniq[cnt == 1]]


# ================================================================== written
def write(name, body_nif, pieces, mat):
    lines = [f'template {body_nif}', f'out {MESHDIR / (name + ".nif")}', f'partition {PARTITION}']
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
        Vs = [b.V]; Fs = [b.F]; Cs = [np.tile([0.85, 0.68, 0.58], (len(b.V), 1))]
        o = len(b.V)
        for P, UV, BW, Fl in pieces.values():
            Vs.append(P); Fs.append(Fl + o); Cs.append(np.tile([0.92, 0.88, 0.78], (len(P), 1))); o += len(P)
        V = np.vstack(Vs); F = np.vstack(Fs); C = np.vstack(Cs)
        frames.append(raster(V, F, C, a))
    Image.fromarray(np.hstack(frames)).save(out)
    return out


def raster(V, F, C, yaw, W=520, H=900):
    R = np.array([[math.cos(yaw), -math.sin(yaw), 0], [math.sin(yaw), math.cos(yaw), 0], [0, 0, 1]])
    P = V @ R.T
    N = normals(P, F)
    sc = H / 110.0
    sx = W / 2 + P[:, 0] * sc; sy = H - (P[:, 2] - 8) * sc; dep = P[:, 1]
    img = np.full((H, W, 3), 30, np.uint8); zb = np.full((H, W), 1e9)
    L = np.array([0.3, -0.6, 0.75]); L /= np.linalg.norm(L)
    for f in F:
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
        parts = {'briefs': (clip(b1.F, briefs(b1, sex == 'female')), 0.22)}
        if sex == 'female':
            parts['bandeau'] = (clip(b1.F, bandeau(b1)), 0.28)
        made[sex] = (bodies, parts)
        for (recipe, Fl), off in parts.values():
            _, UV, _, _ = piece(b1, recipe, Fl, off)
            edges += boundary_uv(Fl, UV)
    D, N = linen_tex(edges)
    mat = f'diffuse={D} normal={N} spec=0.12 gloss=8'
    for sex, (bodies, parts) in made.items():
        for w, b in bodies.items():
            tipl = ()   # (the nipple mound tried: it showed more, not less - left out)
            pieces = {f'{sex}_{k}': piece(b, recipe, Fl, off, tipl if k == 'bandeau' else ()) for k, ((recipe, Fl), off) in parts.items()}
            write(f'{sex}_underwear_{w}', GAME / f'{sex}body_{w}.nif', pieces, mat)
            if do_render and w == 1:
                print('preview', render(f'{sex}_underwear', b, pieces))


if __name__ == '__main__':
    build('--render' in sys.argv)
