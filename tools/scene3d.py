"""scene3d: a small offline renderer for Undying Nights art (the menu picture): our own skinned NIF characters posed on
the draugr skeleton by hand (world-axis turns at the joints, linear-blend skinning), procedural meshes (ground, rocks,
stones), a perspective camera, a deferred numpy rasteriser (depth, normal, albedo, emission), normal maps, a moon key
light with rim, a cold sky fill, shadow map, depth and height fog.

Library use (see tools/menuscene.py):
    m = Model('zombie', nif_export_dir, textures_root); verts = m.pose({'NPC Spine1 [Spn1]': ('x', -20), ...})
    sc = Scene(); sc.add(verts, m.tris, m.uv, tex=..., xf=place(x, y, z, yaw, scale)); sc.render(cam, w, h)
Exports come from tools/modelconv/nifexport (built to /home/claude/tools/nifexport).
"""
import math, pathlib, subprocess
import numpy as np
from PIL import Image

NIFEXPORT = '/home/claude/tools/nifexport'
SKEL_NIF = '/home/claude/work/skel/draugr.nif'


# ------------------------------------------------------------------------------------------------------------ math
def rot(axis, deg):
    a = np.asarray(axis, np.float64); a = a / np.linalg.norm(a)
    t = math.radians(deg); c, s = math.cos(t), math.sin(t); x, y, z = a
    return np.array([[c + x * x * (1 - c), x * y * (1 - c) - z * s, x * z * (1 - c) + y * s],
                     [y * x * (1 - c) + z * s, c + y * y * (1 - c), y * z * (1 - c) - x * s],
                     [z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c)]])


AX = {'x': (1, 0, 0), 'y': (0, 1, 0), 'z': (0, 0, 1)}


def place(x, y, z, yaw=0.0, scale=1.0):
    """model -> world: scale, turn about Z (degrees, 0 = facing +Y), move"""
    M = np.eye(4); M[:3, :3] = rot((0, 0, 1), yaw) * scale; M[:3, 3] = (x, y, z)
    return M


def load_tex(path, size=None, mode='RGBA'):
    im = Image.open(path).convert(mode)
    if size:
        im = im.resize((size, size), Image.LANCZOS)
    return np.asarray(im, np.float32) / 255.0


# -------------------------------------------------------------------------------------------------------- skeleton
class Skeleton:
    def __init__(self, nif=SKEL_NIF, cache='/tmp/claude-0/ex/draugr'):
        if not pathlib.Path(cache, 'nodes.txt').exists():
            subprocess.run([NIFEXPORT, nif, cache], check=True, capture_output=True)
        self.name, self.parent, self.pos = [], [], []
        for l in open(pathlib.Path(cache, 'nodes.txt')):
            i, n, p, t, r = l.rstrip('\n').split('\t')
            self.name.append(n); self.parent.append(int(p)); self.pos.append([float(v) for v in t.split()])
        self.pos = np.array(self.pos)
        self.index = {n: i for i, n in enumerate(self.name)}

    def transforms(self, turns):
        """turns: {bone: [(axis, degrees), ...]} in world axes, applied at each bone's joint, children follow.
        returns {bone: 4x4 world->world deformation}"""
        T = [None] * len(self.name)
        for i in range(len(self.name)):  # parents come before children in a NIF
            p = self.parent[i]
            M = T[p].copy() if p >= 0 and T[p] is not None else np.eye(4)
            spec = turns.get(self.name[i])
            if spec:
                if isinstance(spec, tuple):
                    spec = [spec]
                j = M[:3, :3] @ self.pos[i] + M[:3, 3]          # the joint where the parent chain has put it
                R = np.eye(3)
                for ax, deg in spec:
                    R = rot(AX[ax] if isinstance(ax, str) else ax, deg) @ R
                A = np.eye(4); A[:3, :3] = R; A[:3, 3] = j - R @ j
                M = A @ M
            T[i] = M
        return {self.name[i]: T[i] for i in range(len(self.name))}


SKEL = None


# ----------------------------------------------------------------------------------------------------------- model
class Shape:
    pass


class Model:
    def __init__(self, nif, tex_root, cache_dir, tex_size=1024):
        global SKEL
        if SKEL is None:
            SKEL = Skeleton()
        cache = pathlib.Path(cache_dir)
        if not (cache / 'nodes.txt').exists():
            subprocess.run([NIFEXPORT, str(nif), str(cache)], check=True, capture_output=True)
        self.shapes = []
        for f in sorted(cache.glob('shape_*.txt')):
            s = Shape(); s.bones = []; v = []; tri = []
            s.tex = [None, None, None]; s.alpha = 0
            for l in open(f):
                if l.startswith('v '):
                    v.append([float(x) for x in l.split()[1:]])
                elif l.startswith('f '):
                    tri.append([int(x) for x in l.split()[1:]])
                elif l.startswith('tex'):
                    k, p = l.rstrip('\n').split('\t'); s.tex[int(k[3])] = p
                elif l.startswith('bone\t'):
                    s.bones.append(l.split('\t')[1])
                elif l.startswith('alpha'):
                    s.alpha = int(l.split()[1])
            v = np.array(v); s.pos = v[:, 0:3]; s.uv = v[:, 3:5]; s.wb = v[:, 5:9].astype(int); s.ww = v[:, 9:13]
            s.tri = np.array(tri, int)
            s.maps = []
            for k, p in enumerate(s.tex):
                img = None
                if p:
                    fp = pathlib.Path(tex_root) / p.replace('\\', '/').replace('textures/', 'Textures/', 1)
                    if not fp.exists():
                        alt = list(pathlib.Path(tex_root).glob('**/' + pathlib.Path(p.replace('\\', '/')).name))
                        fp = alt[0] if alt else None
                    if fp:
                        img = load_tex(fp, tex_size)
                s.maps.append(img)
            self.shapes.append(s)

    def pose(self, turns):
        """posed vertex positions per shape (model space)"""
        T = SKEL.transforms(turns)
        out = []
        for s in self.shapes:
            mats = np.stack([T.get(b, np.eye(4)) for b in s.bones])          # bones x 4 x 4
            P = np.c_[s.pos, np.ones(len(s.pos))]
            acc = np.zeros((len(s.pos), 3))
            for k in range(4):
                M = mats[s.wb[:, k]]                                          # n x 4 x 4
                acc += s.ww[:, k:k + 1] * np.einsum('nij,nj->ni', M[:, :3, :], P)
            tot = s.ww.sum(1, keepdims=True); tot[tot == 0] = 1
            out.append(acc / tot)
        return out


# ------------------------------------------------------------------------------------------------- procedural meshes
def grid_mesh(x0, x1, y0, y1, nx, ny, hfun):
    xs = np.linspace(x0, x1, nx); ys = np.linspace(y0, y1, ny)
    X, Y = np.meshgrid(xs, ys); Z = hfun(X, Y)
    V = np.c_[X.ravel(), Y.ravel(), Z.ravel()]
    UV = np.c_[(X.ravel() - x0) / 200.0, (Y.ravel() - y0) / 200.0]
    i = np.arange(nx * ny).reshape(ny, nx)
    a, b, c, d = i[:-1, :-1].ravel(), i[:-1, 1:].ravel(), i[1:, 1:].ravel(), i[1:, :-1].ravel()
    T = np.r_[np.c_[a, b, c], np.c_[a, c, d]]
    return V, T, UV


def rock_mesh(seed, rx, ry, rz, sub=3, rough=0.35):
    """a displaced icosphere (a boulder or a standing slab), flat-ish bottom"""
    t = (1 + 5 ** 0.5) / 2
    V = [[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t], [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]]
    F = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
         [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]]
    V = [np.array(v, float) / np.linalg.norm(v) for v in V]
    for _ in range(sub):
        cache = {}; nf = []
        def mid(a, b):
            k = (min(a, b), max(a, b))
            if k not in cache:
                m = V[a] + V[b]; V.append(m / np.linalg.norm(m)); cache[k] = len(V) - 1
            return cache[k]
        for a, b, c in F:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [[a, ab, ca], [b, bc, ab], [c, ca, bc], [ab, bc, ca]]
        F = nf
    V = np.array(V); r = np.random.default_rng(seed)
    # displacement: a few random planes cut facets (stone breaks flat), plus small noise
    d = np.ones(len(V))
    for _ in range(9):
        n = r.normal(size=3); n /= np.linalg.norm(n); off = r.uniform(0.55, 0.85)
        d = np.minimum(d, off / np.maximum(V @ n, 1e-3))
    d = np.clip(d, 0.45, 1.0) * (1 + r.normal(0, rough * 0.06, len(V)))
    V = V * d[:, None] * np.array([rx, ry, rz])
    V[:, 2] = np.maximum(V[:, 2], -rz * 0.25)
    UV = np.c_[np.arctan2(V[:, 1], V[:, 0]) / math.pi, V[:, 2] / max(rz, 1)]
    return V, np.array(F), UV


# ------------------------------------------------------------------------------------------------------- rendering
class Item:
    pass


class Scene:
    def __init__(self):
        self.items = []

    def add(self, V, T, UV, albedo=None, normal=None, glow=None, color=(0.5, 0.5, 0.5), xf=None, emit=1.0, rough_tex=None,
            two_sided=True, alpha_test=False, tint=(1, 1, 1), proc=None):
        it = Item(); V = np.asarray(V, float)
        if xf is not None:
            V = (np.c_[V, np.ones(len(V))] @ xf.T)[:, :3]
        it.V, it.T, it.UV = V, np.asarray(T, int), np.asarray(UV, float)
        it.albedo, it.normal, it.glow, it.color, it.emit, it.two, it.alpha_test = albedo, normal, glow, np.array(color), emit, two_sided, alpha_test
        it.tint = np.array(tint); it.proc = proc
        # smooth vertex normals
        fn = np.cross(V[it.T[:, 1]] - V[it.T[:, 0]], V[it.T[:, 2]] - V[it.T[:, 0]])
        vn = np.zeros_like(V)
        for k in range(3):
            np.add.at(vn, it.T[:, k], fn)
        it.N = vn / (np.linalg.norm(vn, axis=1, keepdims=True) + 1e-9)
        self.items.append(it)
        return it

    def add_model(self, model, posed, xf, emit=1.0, tint=(1, 1, 1)):
        for s, P in zip(model.shapes, posed):
            self.add(P, s.tri, s.uv, albedo=s.maps[0], normal=s.maps[1], glow=s.maps[2], xf=xf, emit=emit, alpha_test=bool(s.alpha), tint=tint)

    # -- raster: returns per-pixel depth, world position, normal, albedo, emission
    def raster(self, cam, w, h, want_shading=True):
        eye, look, up, fov = np.array(cam['eye'], float), np.array(cam['look'], float), np.array(cam.get('up', (0, 0, 1)), float), cam['fov']
        f = look - eye; f /= np.linalg.norm(f); r = np.cross(f, up); r /= np.linalg.norm(r); u = np.cross(r, f)
        fl = 0.5 * h / math.tan(math.radians(fov) / 2)
        D = np.full((h, w), np.inf, np.float32)
        G = {k: np.zeros((h, w, 3), np.float32) for k in ('P', 'N', 'A', 'E')} if want_shading else None
        for it in self.items:
            rel = it.V - eye
            cz = rel @ f; cx = rel @ r; cy = rel @ u
            sx = w / 2 + fl * cx / np.maximum(cz, 1e-3); sy = h / 2 - fl * cy / np.maximum(cz, 1e-3)
            for ti, (a, b, c) in enumerate(it.T):
                if cz[a] < 1 or cz[b] < 1 or cz[c] < 1:
                    continue
                x0, x1 = int(max(0, min(sx[a], sx[b], sx[c]))), int(min(w - 1, max(sx[a], sx[b], sx[c]) + 1))
                y0, y1 = int(max(0, min(sy[a], sy[b], sy[c]))), int(min(h - 1, max(sy[a], sy[b], sy[c]) + 1))
                if x0 > x1 or y0 > y1:
                    continue
                d = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a])
                if abs(d) < 1e-9 or (not it.two and d > 0):
                    continue
                yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1].astype(np.float32) + 0.5
                l0 = ((sx[b] - xx) * (sy[c] - yy) - (sx[c] - xx) * (sy[b] - yy)) / d
                l1 = ((sx[c] - xx) * (sy[a] - yy) - (sx[a] - xx) * (sy[c] - yy)) / d
                l2 = 1 - l0 - l1
                m = (l0 >= -1e-4) & (l1 >= -1e-4) & (l2 >= -1e-4)
                if not m.any():
                    continue
                # perspective-correct weights
                q0, q1, q2 = l0 / cz[a], l1 / cz[b], l2 / cz[c]; qs = q0 + q1 + q2
                z = 1.0 / qs
                p0, p1, p2 = q0 * z, q1 * z, q2 * z
                sub = D[y0:y1 + 1, x0:x1 + 1]
                m &= z < sub
                if not m.any():
                    continue
                if want_shading:
                    uv = (p0[..., None] * it.UV[a] + p1[..., None] * it.UV[b] + p2[..., None] * it.UV[c])
                    alb = None; nrm = None
                    if it.albedo is not None:
                        th, tw_ = it.albedo.shape[:2]
                        tx = ((uv[..., 0] % 1) * tw_).astype(int).clip(0, tw_ - 1); ty = ((uv[..., 1] % 1) * th).astype(int).clip(0, th - 1)
                        texel = it.albedo[ty, tx]
                        if it.alpha_test:
                            m &= texel[..., 3] > 0.45
                            if not m.any():
                                continue
                        alb = texel[..., :3] * it.tint
                    elif it.proc is not None:
                        pw = (p0[..., None] * it.V[a] + p1[..., None] * it.V[b] + p2[..., None] * it.V[c])
                        alb = it.proc(pw)
                    else:
                        alb = np.broadcast_to(it.color, uv.shape[:2] + (3,))
                    if getattr(it, 'nfun', None) is not None:
                        nv = it.nfun(p0[..., None] * it.V[a] + p1[..., None] * it.V[b] + p2[..., None] * it.V[c])
                    else:
                        nv = p0[..., None] * it.N[a] + p1[..., None] * it.N[b] + p2[..., None] * it.N[c]
                        nv /= np.linalg.norm(nv, axis=-1, keepdims=True) + 1e-9
                    if it.normal is not None:
                        # tangent frame of this triangle from its uv derivatives
                        e1, e2 = it.V[b] - it.V[a], it.V[c] - it.V[a]
                        du1, dv1 = it.UV[b] - it.UV[a]; du2, dv2 = it.UV[c] - it.UV[a]
                        det = du1 * dv2 - du2 * dv1
                        if abs(det) > 1e-12:
                            T_ = (e1 * dv2 - e2 * dv1) / det; B_ = (e2 * du1 - e1 * du2) / det
                            T_ /= np.linalg.norm(T_) + 1e-9; B_ /= np.linalg.norm(B_) + 1e-9
                            th, tw_ = it.normal.shape[:2]
                            tx = ((uv[..., 0] % 1) * tw_).astype(int).clip(0, tw_ - 1); ty = ((uv[..., 1] % 1) * th).astype(int).clip(0, th - 1)
                            tn = it.normal[ty, tx, :3] * 2 - 1
                            nv = tn[..., 0:1] * T_ - tn[..., 1:2] * B_ + tn[..., 2:3] * nv
                            nv /= np.linalg.norm(nv, axis=-1, keepdims=True) + 1e-9
                    if getattr(it, 'detail', None) is not None:  # fine surface relief and grime laid over (menuscene)
                        nv, alb = it.detail(p0[..., None] * it.V[a] + p1[..., None] * it.V[b] + p2[..., None] * it.V[c], nv, alb)
                    em = np.zeros_like(alb)
                    if it.glow is not None:
                        th, tw_ = it.glow.shape[:2]
                        tx = ((uv[..., 0] % 1) * tw_).astype(int).clip(0, tw_ - 1); ty = ((uv[..., 1] % 1) * th).astype(int).clip(0, th - 1)
                        em = it.glow[ty, tx, :3] * it.emit
                    pw = (p0[..., None] * it.V[a] + p1[..., None] * it.V[b] + p2[..., None] * it.V[c])
                    # face the camera: two-sided normals flip toward the eye
                    fn_ = np.cross(it.V[b] - it.V[a], it.V[c] - it.V[a])     # the triangle's own facing decides the flip
                    if np.dot(fn_, eye - it.V[a]) * np.dot(fn_, nv.reshape(-1, 3).mean(0)) < 0:
                        nv = -nv
                    G['P'][y0:y1 + 1, x0:x1 + 1][m] = pw[m]; G['N'][y0:y1 + 1, x0:x1 + 1][m] = nv[m]
                    G['A'][y0:y1 + 1, x0:x1 + 1][m] = alb[m]; G['E'][y0:y1 + 1, x0:x1 + 1][m] = em[m]
                sub[m] = z[m]
        return D, G, (eye, f, r, u, fl)

    def shadow_map(self, light_dir, center, extent, res=1024):
        """orthographic depth from the light (for the moon's shadows)"""
        L = np.asarray(light_dir, float); L /= np.linalg.norm(L)
        up = np.array([0, 0, 1.0]) if abs(L[2]) < 0.95 else np.array([0, 1.0, 0])
        r = np.cross(L, up); r /= np.linalg.norm(r); u = np.cross(r, L)
        S = np.full((res, res), np.inf, np.float32)
        for it in self.items:
            rel = it.V - center
            lz = rel @ L; lx = rel @ r; ly = rel @ u
            sx = (lx / extent + 0.5) * res; sy = (0.5 - ly / extent) * res
            for a, b, c in it.T:
                x0, x1 = int(max(0, min(sx[a], sx[b], sx[c]))), int(min(res - 1, max(sx[a], sx[b], sx[c]) + 1))
                y0, y1 = int(max(0, min(sy[a], sy[b], sy[c]))), int(min(res - 1, max(sy[a], sy[b], sy[c]) + 1))
                if x0 > x1 or y0 > y1:
                    continue
                d = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a])
                if abs(d) < 1e-9:
                    continue
                yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1].astype(np.float32) + 0.5
                l0 = ((sx[b] - xx) * (sy[c] - yy) - (sx[c] - xx) * (sy[b] - yy)) / d
                l1 = ((sx[c] - xx) * (sy[a] - yy) - (sx[a] - xx) * (sy[c] - yy)) / d
                l2 = 1 - l0 - l1
                m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
                z = l0 * lz[a] + l1 * lz[b] + l2 * lz[c]
                sub = S[y0:y1 + 1, x0:x1 + 1]
                m &= z < sub
                sub[m] = z[m]
        return S, (L, r, u, center, extent, res)

    @staticmethod
    def shadowed(P, sm, bias=6.0):
        S, (L, r, u, center, extent, res) = sm
        rel = P - center
        lz = rel @ L; sx = ((rel @ r) / extent + 0.5) * res; sy = (0.5 - (rel @ u) / extent) * res
        ix = sx.astype(int).clip(0, res - 1); iy = sy.astype(int).clip(0, res - 1)
        # 3x3 percentage-closer filter
        lit = np.zeros(P.shape[:-1], np.float32)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                lit += (lz <= S[(iy + dy).clip(0, res - 1), (ix + dx).clip(0, res - 1)] + bias)
        return lit / 9.0
