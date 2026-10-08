"""Small mesh helpers shared by the asset tools: fast OBJ load, vertex-clustering decimation, OBJ write."""
import numpy as np

def load_obj(path):
    """-> V (n,3), T (n,2), F (m,3) (single group; uv index = vertex index as objexp writes them)"""
    v, t, f = [], [], []
    with open(path) as fh:
        for l in fh:
            c = l[:2]
            if c == 'v ': v.append(l[2:])
            elif c == 'vt': t.append(l[3:])
            elif c == 'f ': f.append(l[2:])
    V = np.loadtxt(v, dtype=np.float32) if v else np.zeros((0, 3), np.float32)
    T = np.loadtxt(t, dtype=np.float32)[:, :2] if t else np.zeros((len(V), 2), np.float32)
    F = np.array([[int(x.split('/')[0]) - 1 for x in s.split()[:3]] for s in f], np.int64)
    return V, T, F

def compact(V, T, F):
    used = np.unique(F); remap = -np.ones(len(V), np.int64); remap[used] = np.arange(len(used))
    return V[used], T[used], remap[F]

def decimate(V, T, F, budget):
    """vertex clustering: snap to a grid, average, drop collapsed triangles; grid coarsened until under budget"""
    if len(F) <= budget:
        return V, T, F
    lo = V.min(0); diag = float(np.linalg.norm(V.max(0) - lo))
    best = None
    for div in (900, 700, 520, 400, 300, 230, 170, 130, 100, 75, 55):
        key = np.floor((V - lo) / (diag / div)).astype(np.int64)
        k = (key[:, 0] * 73856093) ^ (key[:, 1] * 19349663) ^ (key[:, 2] * 83492791)
        _, inv = np.unique(k, return_inverse=True)
        cnt = np.bincount(inv).astype(np.float32)
        NV = np.stack([np.bincount(inv, V[:, j]) / cnt for j in range(3)], 1)
        NT = np.stack([np.bincount(inv, T[:, j]) / cnt for j in range(2)], 1)
        NF = inv[F]
        ok = (NF[:, 0] != NF[:, 1]) & (NF[:, 1] != NF[:, 2]) & (NF[:, 0] != NF[:, 2])
        NF = NF[ok]
        # drop duplicate triangles
        s = np.sort(NF, 1); _, ui = np.unique(s, axis=0, return_index=True); NF = NF[np.sort(ui)]
        best = (NV, NT, NF)
        if len(NF) <= budget:
            break
    return compact(*best)

def write_obj(path, V, T, F, mtl=None, name='mesh'):
    with open(path, 'w') as fh:
        if mtl: fh.write(f'mtllib {mtl}\n')
        fh.write(f'o {name}\nusemtl {name}\n')
        np.savetxt(fh, V, fmt='v %.5f %.5f %.5f')
        np.savetxt(fh, T, fmt='vt %.5f %.5f')
        np.savetxt(fh, F + 1, fmt='f %d/%d %d/%d %d/%d'.replace('%d/%d %d/%d %d/%d', '%d %d %d'))
