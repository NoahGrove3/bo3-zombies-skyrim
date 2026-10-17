"""Skyrim's dragon as data (tools/modelconv/dragonexport writes the text): shapes with vertices, normals, uvs,
triangles, skin bones and weights, and the skeleton's hierarchy; plus a quick textured preview (tools/modelconv/meshrender)."""
import numpy as np, subprocess, pathlib, struct
from PIL import Image

MESHRENDER = '/home/claude/tools/meshrender'


def xf(vals):
    """13 numbers (3x3 row-major, t, scale) -> 4x4"""
    v = [float(x) for x in vals]
    M = np.eye(4)
    M[:3, :3] = np.array(v[:9]).reshape(3, 3) * v[12]
    M[:3, 3] = v[9:12]
    return M


class Shape:
    pass


def load(path):
    shapes, cur = [], None
    for line in open(path):
        p = line.split()
        if not p:
            continue
        k = p[0]
        if k == 'shape':
            cur = Shape(); shapes.append(cur)
            cur.name, cur.nv, cur.nt = p[1], int(p[2]), int(p[3])
            cur.V, cur.T, cur.bones, cur.s2b, cur.tex, cur.W = [], [], [], [], {}, []
            cur.xform = np.eye(4); cur.g2s = None
        elif k == 'xform':
            cur.xform = xf(p[1:])
        elif k == 'globaltoskin':
            cur.g2s = xf(p[1:])
        elif k == 'tex':
            cur.tex[int(p[1])] = p[2]
        elif k == 'bone':
            cur.bones.append(p[1]); cur.s2b.append(xf(p[2:]))
        elif k == 'v':
            cur.V.append([float(x) for x in p[1:]])
        elif k == 't':
            cur.T.append([int(x) for x in p[1:]])
        elif k == 'w':
            cur.W.append((int(p[1]), int(p[2]), float(p[3])))
    for s in shapes:
        A = np.array(s.V)
        s.pos, s.nrm, s.uv, s.col = A[:, 0:3], A[:, 3:6], A[:, 6:8], A[:, 8:12]
        s.tri = np.array(s.T, np.int64)
        s.weights = np.zeros((len(A), len(s.bones)))
        for vi, b, w in s.W:
            s.weights[vi, b] = w
        s.s2b = np.array(s.s2b)
        s.bind = np.array([np.linalg.inv(m) for m in s.s2b])  # each bone's place in the mesh's space
        del s.V, s.T, s.W
    return shapes


def load_skeleton(path):
    nodes = {}
    for line in open(path):
        p = line.split()
        if p and p[0] == 'node':
            nodes[p[1]] = dict(parent=None if p[2] == '-' else p[2], local=xf(p[3:16]), world=xf(p[16:29]))
    return nodes


def render(pos, nrm, uv, tri, out, eye, look, tex=None, W=900, H=600, fov=40):
    tmp = pathlib.Path('/tmp/claude-0/meshrender'); tmp.mkdir(parents=True, exist_ok=True)
    mb = tmp / 'm.bin'
    data = np.concatenate([pos, nrm, uv], 1).astype(np.float32)
    with open(mb, 'wb') as f:
        f.write(struct.pack('<ii', len(pos), len(tri)))
        f.write(data.tobytes()); f.write(tri.astype(np.int32).tobytes())
    tw = th = 0
    tp = '-'
    if tex is not None:
        im = Image.open(tex).convert('RGB')
        tw, th = im.size
        tp = str(tmp / 't.rgb'); open(tp, 'wb').write(im.tobytes())
    ppm = tmp / 'o.ppm'
    subprocess.run([MESHRENDER, str(mb), tp, str(tw), str(th), str(ppm), str(W), str(H), *map(str, eye), *map(str, look), str(fov)], check=True)
    Image.open(ppm).save(out)
    return out


def sheet(paths, out, cols=2):
    ims = [Image.open(p) for p in paths]
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    S = Image.new('RGB', (w * cols, h * rows))
    for i, im in enumerate(ims):
        S.paste(im, ((i % cols) * w, (i // cols) * h))
    S.save(out)
    return out
