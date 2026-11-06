"""A software render of tools/gauntlet.py's model (its OBJ and textures, kept with --keep) for looking it over: lit,
textured, normal-mapped, the glass blended over, optionally with the hand inside it (an OBJ of the hand in the same bone
frame) to check the fit.

    python3 tools/gauntletpreview.py <obj> <texdir(png)> <out.png> [hand.obj] [yaw pitch ...]"""
import sys, math, pathlib
import numpy as np
from PIL import Image

def load_obj(p):
    V, T, Nn, groups = [], [], [], []
    cur = None
    for l in open(p):
        if l.startswith('v '): V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt '): T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('vn '): Nn.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('usemtl'): cur = [l.split()[1], []]; groups.append(cur)
        elif l.startswith('f '):
            cur[1].append([[int(y) - 1 if y else 0 for y in x.split('/')] for x in l.split()[1:4]])
    return np.array(V), np.array(T) if T else None, np.array(Nn) if Nn else None, [(m, np.array(f)) for m, f in groups]

MAT = {  # diffuse png, normal png, spec, gloss, alpha, emit
    'leather': ('gauntlet_leather_d', 'gauntlet_leather_n', 0.25, 20, False, None),
    'brass': ('gauntlet_brass_d', 'gauntlet_brass_n', 0.9, 70, False, None),
    'plate': ('gauntlet_brass_d', 'gauntlet_brass_n', 0.9, 70, False, None),
    'glass': ('gauntlet_glass_d', None, 1.2, 160, True, None),
    'orbglass': ('gauntlet_glass_d', None, 1.4, 180, True, None),
    'tubeglass': ('gauntlet_tubeglass_d', None, 1.2, 160, True, None),
    'liquid': ('gauntlet_liquid_d', None, 0.6, 80, False, 'gauntlet_liquid_g'),
    'skin': (None, None, 0.1, 10, False, None),
}

def rot(yaw, pitch):
    y, p = math.radians(yaw), math.radians(pitch)
    Ry = np.array([[math.cos(y), -math.sin(y), 0], [math.sin(y), math.cos(y), 0], [0, 0, 1]])
    Rx = np.array([[1, 0, 0], [0, math.cos(p), -math.sin(p)], [0, math.sin(p), math.cos(p)]])
    return Rx @ Ry

def render(objs, texdir, yaw, pitch, W=900, H=900, span=16.0, centre=(0.5, -0.5, 1.4)):
    R = rot(yaw, pitch)
    sc = H / span
    zb = np.full((H, W), -1e9); img = np.zeros((H, W, 3))
    glass = []
    Ld = np.array([0.4, 0.7, 0.6]); Ld /= np.linalg.norm(Ld)
    tex = {}
    def T(name):
        if name is None: return None
        if name not in tex:
            p = pathlib.Path(texdir) / (name + '.dds.png')
            tex[name] = np.asarray(Image.open(p).convert('RGBA'), np.float32) / 255 if p.exists() else None
        return tex[name]
    for V, TT, NN, groups in objs:
        # view space: x right, y up, z toward the viewer.  model: rotate, then the bone's Z up the screen
        P = (V - centre) @ R.T
        sx = W / 2 + P[:, 0] * sc; sy = H / 2 - P[:, 2] * sc; sz = -P[:, 1]
        Nv = NN @ R.T if NN is not None else None
        for m, F in groups:
            d, n, spk, gl, alpha, emit = MAT.get(m, MAT['skin'])
            Dt, Gt = T(d), T(emit)
            for f in F:
                vi = f[:, 0]
                x = sx[vi]; y = sy[vi]; z = sz[vi]
                x0, x1 = int(max(0, np.floor(x.min()))), int(min(W - 1, np.ceil(x.max())))
                y0, y1 = int(max(0, np.floor(y.min()))), int(min(H - 1, np.ceil(y.max())))
                if x0 > x1 or y0 > y1: continue
                den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
                if abs(den) < 1e-9: continue
                yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1] + 0.5
                a = ((y[1] - y[2]) * (xx - x[2]) + (x[2] - x[1]) * (yy - y[2])) / den
                b = ((y[2] - y[0]) * (xx - x[2]) + (x[0] - x[2]) * (yy - y[2])) / den
                c = 1 - a - b
                ins = (a >= -1e-4) & (b >= -1e-4) & (c >= -1e-4)
                if not ins.any(): continue
                zz = a * z[0] + b * z[1] + c * z[2]
                nrm = a[..., None] * Nv[f[0, 2]] + b[..., None] * Nv[f[1, 2]] + c[..., None] * Nv[f[2, 2]]
                nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True) + 1e-9
                # (seen from behind: both faces lit)
                facing = nrm[..., 1] * -1
                nrm = np.where((facing < 0)[..., None] & (m in ('leather', 'glass', 'tubeglass')), -nrm, nrm)
                if Dt is not None and TT is not None:
                    uv = a[..., None] * TT[f[0, 1]] + b[..., None] * TT[f[1, 1]] + c[..., None] * TT[f[2, 1]]
                    hh, ww = Dt.shape[:2]
                    tu = (np.mod(uv[..., 0], 1) * (ww - 1)).astype(int); tv = (np.mod(1 - uv[..., 1], 1) * (hh - 1)).astype(int)
                    col = Dt[tv, tu]
                    g = Gt[tv, tu][..., :3] if Gt is not None else None
                else:
                    col = np.ones(a.shape + (4,)) * np.array([0.78, 0.6, 0.5, 1]); g = None
                Lw = np.array([Ld[0], -Ld[2], Ld[1]])   # light from upper front-left in view space (x, z-up, toward)
                nv = np.stack([nrm[..., 0], nrm[..., 2], -nrm[..., 1]], -1)
                lam = np.clip(nv @ np.array([0.45, 0.6, 0.66]), 0, 1)
                hv = np.array([0.45, 0.6, 1.66]); hv /= np.linalg.norm(hv)
                spec = np.clip(nv @ hv, 0, 1) ** gl * spk
                rgb = col[..., :3] * (0.28 + 0.85 * lam[..., None]) + spec[..., None]
                if g is not None: rgb = rgb + g * 0.9
                Y, X = yy[ins].astype(int), xx[ins].astype(int)
                if alpha:
                    glass.append((Y, X, zz[ins], rgb[ins], np.clip(col[ins][:, 3] + spec[ins] * 0.6, 0, 1)))
                    continue
                upd = zz[ins] > zb[Y, X]
                zb[Y[upd], X[upd]] = zz[ins][upd]; img[Y[upd], X[upd]] = rgb[ins][upd]
    bg = img.copy(); bg[zb < -1e8] = [0.1, 0.1, 0.12]; img = bg
    for Y, X, Z, C, A in glass:
        vis = Z > zb[Y, X]
        img[Y[vis], X[vis]] = img[Y[vis], X[vis]] * (1 - A[vis, None]) + C[vis] * A[vis, None]
    return np.clip(img, 0, 1)

if __name__ == '__main__':
    obj, texdir, out = sys.argv[1:4]
    rest = sys.argv[4:]
    hand = None
    if rest and rest[0].endswith('.obj'): hand = rest.pop(0)
    views = [(float(rest[i]), float(rest[i + 1])) for i in range(0, len(rest), 2)] or [(30, 15), (210, 15)]
    objs = [load_obj(obj)]
    if hand: objs.append(load_obj(hand))
    tiles = [render(objs, texdir, y, p) for y, p in views]
    Image.fromarray((np.hstack(tiles) * 255).astype(np.uint8)).save(out)
    print(out)
