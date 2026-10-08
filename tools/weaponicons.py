"""HUD icons for every Coffer/relic weapon: each model rendered side-on (orthographic, textured, emissive parts at full
glow), laid on a slight rise like a weapon on a rack, with a soft element-coloured halo; transparent PNG, 512x192.

    python tools/weaponicons.py <out dir> <job> [<job> ...]     (jobs from tools/staffs.py, staffs_proc.py, bows.py)
The weapon id is the job file's stem; its element colour comes from sheets/weapons.json."""
import json, math, pathlib, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
OUT = pathlib.Path(sys.argv[1])
JOBS = [pathlib.Path(p) for p in sys.argv[2:]]
EL = {r['id']: r['element'] for r in json.load(open(ROOT / 'sheets/weapons.json'))['rows']}
W, H, SS = 512, 192, 3   # icon size, supersampling

def load_job(job):
    lines = job.read_text().split('\n')
    obj = pathlib.Path(next(l[3:].strip() for l in lines if l.startswith('in ')))
    texroot = None
    mats = {}
    for l in lines:
        if l.startswith('mat '):
            p = l.split(); kv = dict(x.split('=', 1) for x in p[2:] if '=' in x)
            mats[p[1]] = kv
    return obj, mats

def find_tex(job, rel):
    """the DDS a job references, under the Data dir next to the job's output"""
    if not rel:
        return None
    name = rel.replace('\\', '/').split('/')[-1]
    for base in (job.parent.parent.parent / 'Data' / 'Textures', job.parent.parent.parent.parent / 'Data' / 'Textures'):
        hits = list(base.rglob(name)) if base.exists() else []
        if hits:
            return hits[0]
    return None

def load_obj(p):
    V, T, groups, cur = [], [], {}, None
    for l in open(p):
        if l.startswith('v '): V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt '): T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('usemtl'): cur = l.split()[1]; groups.setdefault(cur, [])
        elif l.startswith('f '):
            groups[cur].append([tuple(int(k) - 1 for k in x.split('/')[:2]) for x in l.split()[1:4]])
    return np.array(V), (np.array(T) if T else np.zeros((1, 2))), groups

def load_shapes(job):
    """bows.py jobs: 'shape <name> k=v...' then inline 'v x y z u v ...' and 't a b c' (indices within the shape)"""
    V, T, groups, mats = [], [], {}, {}
    cur, base = None, 0
    for l in job.read_text().split('\n'):
        if l.startswith('shape '):
            p = l.split(); cur = p[1]; base = len(V); groups[cur] = []
            mats[cur] = dict(x.split('=', 1) for x in p[2:] if '=' in x)
        elif l.startswith('v ') and cur:
            q = l.split(); V.append([float(x) for x in q[1:4]]); T.append([float(q[4]), float(q[5])])
        elif l.startswith('t ') and cur:
            i = [int(x) + base for x in l.split()[1:4]]; groups[cur].append([(k, k) for k in i])
    return np.array(V), np.array(T), groups, mats

def render(job):
    if any(l.startswith('template ') for l in job.read_text().split('\n')[:3]):
        V, T, groups, mats = load_shapes(job)
    else:
        obj, mats = load_job(job)
        V, T, groups = load_obj(obj)
    used = np.unique(np.array([i for F in groups.values() for tri in F for i, _ in tri]))
    P = V[used]; c = P.mean(0)
    _, _, vt = np.linalg.svd(P - c, full_matrices=False)
    e1, e2, e3 = vt[0], vt[1], vt[2]
    if e1[1] < 0: e1 = -e1                         # staff head (+Y) to the right
    if np.cross(e1, e2) @ e3 < 0: e3 = -e3
    ang = math.radians(-14)                        # a slight rise to the right
    s1, s2 = math.cos(ang) * e1 + math.sin(ang) * e2, -math.sin(ang) * e1 + math.cos(ang) * e2
    q = (P - c) @ np.stack([s1, s2], 1)
    span = max((q[:, 0].max() - q[:, 0].min()) / (W * 0.88), (q[:, 1].max() - q[:, 1].min()) / (H * 0.76))
    c = c + s1 * (q[:, 0].max() + q[:, 0].min()) / 2 + s2 * (q[:, 1].max() + q[:, 1].min()) / 2   # centre the bounds
    w, h = W * SS, H * SS
    zb = np.full((h, w), -np.inf, np.float32); col = np.zeros((h, w, 3), np.float32); emis = np.zeros((h, w), np.float32)
    light = (0.35 * s1 + 0.55 * s2 + 0.75 * e3); light /= np.linalg.norm(light)
    texcache = {}
    for g, F in groups.items():
        kv = mats.get(g, {})
        emit = 'emit' in kv
        tex = None
        tp = find_tex(job, kv.get('diffuse'))
        if tp is not None:
            if tp not in texcache:
                try:
                    texcache[tp] = np.asarray(Image.open(tp).convert('RGB').resize((512, 512)), np.float32)
                except Exception:
                    texcache[tp] = None
            tex = texcache[tp]
        ec = np.array([float(x) for x in kv['emit'].split(',')], np.float32) * 255 if emit else None
        add = kv.get('alpha') == 'add'
        for tri in F:
            vi = [a for a, _ in tri]; ti = [b for _, b in tri]
            A = (V[vi] - c)
            sx = (A @ s1) / span * SS + w / 2; sy = h / 2 - (A @ s2) / span * SS; z = A @ e3
            n = np.cross(V[vi[1]] - V[vi[0]], V[vi[2]] - V[vi[0]]); nl = np.linalg.norm(n)
            if nl < 1e-12: continue
            n /= nl
            shade = 1.0 if emit else 0.35 + 0.75 * abs(n @ light)
            x0, x1 = int(max(0, math.floor(sx.min()))), int(min(w - 1, math.ceil(sx.max())))
            y0, y1 = int(max(0, math.floor(sy.min()))), int(min(h - 1, math.ceil(sy.max())))
            if x0 > x1 or y0 > y1: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            d = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
            if abs(d) < 1e-9: continue
            w0 = ((sy[1] - sy[2]) * (gx - sx[2]) + (sx[2] - sx[1]) * (gy - sy[2])) / d
            w1 = ((sy[2] - sy[0]) * (gx - sx[2]) + (sx[0] - sx[2]) * (gy - sy[2])) / d
            w2 = 1 - w0 - w1
            m = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
            if not m.any(): continue
            depth = w0 * z[0] + w1 * z[1] + w2 * z[2]
            zr = zb[y0:y1 + 1, x0:x1 + 1]
            m &= depth > zr
            if not m.any(): continue
            if emit:
                cpx = np.broadcast_to(np.minimum(255, ec * 1.15 + 40), gx.shape + (3,))
            elif tex is not None and len(T) > 1:
                uv = T[ti]
                u = w0 * uv[0, 0] + w1 * uv[1, 0] + w2 * uv[2, 0]; v = w0 * uv[0, 1] + w1 * uv[1, 1] + w2 * uv[2, 1]
                cpx = tex[(((1 - v) % 1) * 511).astype(int), ((u % 1) * 511).astype(int)]
            else:
                cpx = np.broadcast_to(np.array([150, 140, 130], np.float32), gx.shape + (3,))
            if add:   # additive glow parts: brighten what is behind instead of covering it
                col[y0:y1 + 1, x0:x1 + 1][m] = np.minimum(255, col[y0:y1 + 1, x0:x1 + 1][m] + cpx[m] * 0.8)
                emis[y0:y1 + 1, x0:x1 + 1][m] = 1
                continue
            zr[m] = depth[m]
            col[y0:y1 + 1, x0:x1 + 1][m] = cpx[m] * shade
            if emit:
                emis[y0:y1 + 1, x0:x1 + 1][m] = 1
    alpha = np.isfinite(zb) | (emis > 0)
    img = np.dstack([np.clip(col, 0, 255), alpha * 255]).astype(np.uint8)
    im = Image.fromarray(img, 'RGBA').resize((W, H), Image.LANCZOS)
    # element halo behind, a dark rim round the silhouette, and the glowing parts bloomed
    el = EL.get(job.stem, 'FFFFFF'); ec = tuple(int(el[i:i + 2], 16) for i in (0, 2, 4))
    a = im.getchannel('A')
    halo = Image.new('RGBA', (W, H), ec + (0,)); halo.putalpha(a.filter(ImageFilter.GaussianBlur(14)).point(lambda v: int(v * 0.55)))
    rim = Image.new('RGBA', (W, H), (0, 0, 0, 0)); rim.putalpha(a.filter(ImageFilter.MaxFilter(5)).point(lambda v: int(v * 0.85)))
    em = Image.fromarray((emis * 255).astype(np.uint8)).resize((W, H), Image.LANCZOS).filter(ImageFilter.GaussianBlur(6))
    bloom = Image.new('RGBA', (W, H), ec + (0,)); bloom.putalpha(em.point(lambda v: int(min(255, v * 1.6))))
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for layer in (halo, rim, im, bloom):
        out = Image.alpha_composite(out, layer)
    OUT.mkdir(parents=True, exist_ok=True)
    out.save(OUT / f'w_{job.stem}.png')
    print('icon', job.stem)

for j in JOBS:
    render(j)
