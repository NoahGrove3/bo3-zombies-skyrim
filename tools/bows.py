"""The Curse of Alduin elemental bows: the four 'Bow Basemeshes' put on Skyrim's bow rig (tools/bowconv: Bow_* bones and
the iron bow's behavior graph), so the limbs bend and the string draws with Skyrim's own bow animation.
Each bow gets an element finish: dark lacquered limbs tinted to the element, glowing trim on its blades and spurs,
and an energy string.

    python tools/bows.py <bows dir from objexp> <template ironbow.nif> <out Data dir>"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
SRC = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/home/claude/work/bows')
TEMPLATE = sys.argv[2] if len(sys.argv) > 2 else '/home/claude/work/vanilla/ironbow.nif'
OUT = pathlib.Path(sys.argv[3] if len(sys.argv) > 3 else ROOT / 'assets' / 'fx')   # shipped: CI copies assets/fx into Data
CONV = '/home/claude/tools/bowconv'

# rig (from ironbow.nif): grip bone at x 1.31, string ends at x -13.7, y +-54.8
GRIP_X, STRING_X, STRING_Y = 1.31, -13.69, 54.8
MID, LO1, LO2, SB1, UP1, UP2, SB2 = range(7)

# id -> (source object, body colour, trim/string glow colour)
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import skymat
# the Echo Altar's bows (sheets/bows.json): source basemesh, limb finish (skymat kind), body colour, glow colour
import json
_ROWS = json.load(open(ROOT / 'sheets' / 'bows.json'))['rows']
BODY_KIND = {r['id']: r['body'] for r in _ROWS}
BOWS = {r['id']: (r['mesh'], tuple(r['bodyColor']), tuple(r['glow'])) for r in _ROWS}

def load_obj(path):
    V, T, F = [], [], []
    for l in open(path):
        if l.startswith('v '): V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt '): T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('f '): F.append([int(x.split('/')[0]) - 1 for x in l.split()[1:4]])
    return np.array(V), np.array(T), np.array(F)

def components(nv, F):
    parent = np.arange(nv)
    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a
    for a, b, c in F:
        ra, rb, rc = find(a), find(b), find(c)
        parent[rb] = ra; parent[find(rc)] = ra
    return np.array([find(i) for i in range(nv)])

def normals(V, F):
    N = np.zeros_like(V)
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    for k in range(3):
        np.add.at(N, F[:, k], fn)
    return N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-9)

def limb_weights(y):
    """grip bone near the middle, then the two limb bones toward each tip (as Skyrim's bows are weighted)"""
    t = abs(y); b1, b2 = (LO1, LO2) if y < 0 else (UP1, UP2)
    if t < 5: return [(MID, 1.0)]
    if t < 14: a = (t - 5) / 9; return [(MID, 1 - a), (b1, a)]
    if t < 24: return [(b1, 1.0)]
    if t < 32: a = (t - 24) / 8; return [(b1, 1 - a), (b2, a)]
    return [(b2, 1.0)]

def save_dds(im, rel, alpha=False):
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'bow' / rel; p.parent.mkdir(parents=True, exist_ok=True)
    im.convert('RGBA' if alpha else 'RGB').save(p, pixel_format='DXT5' if alpha else 'DXT1')
    return f'CurseOfAlduin\\bow\\{rel}'

def lacquer(col, size=512, seed=1):
    """layered lacquer/metal: soft grain, darker edges of each streak"""
    rng = np.random.default_rng(seed)
    n = rng.random((size, size)).astype(np.float32)
    grain = np.asarray(Image.fromarray((n * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3)).resize((size, size)), np.float32) / 255
    streak = np.asarray(Image.fromarray((rng.random((size // 32, size)) * 255).astype(np.uint8)).resize((size, size), Image.BICUBIC), np.float32) / 255
    k = 0.7 + 0.25 * grain + 0.2 * streak
    rgb = np.clip(np.array(col, np.float32)[None, None, :] * k[..., None], 0, 255).astype(np.uint8)
    nrm = np.full((size, size, 4), (128, 128, 255, 120), np.uint8)
    gx = np.gradient(k, axis=1); gy = np.gradient(k, axis=0)
    nrm[..., 0] = np.clip(128 - gx * 500, 0, 255); nrm[..., 1] = np.clip(128 + gy * 500, 0, 255)
    return Image.fromarray(rgb, 'RGB'), Image.fromarray(nrm, 'RGBA')

def build(bid):
    src, body_col, glow = BOWS[bid]
    V, T, F = load_obj(SRC / f'{src}.obj')
    used = np.unique(F)
    comp = components(len(V), F)
    # the string: a thin straight component spanning most of the height
    H = np.ptp(V[used, 1])
    string_verts = np.zeros(len(V), bool)
    for c in np.unique(comp[used]):
        idx = used[comp[used] == c]
        ext = np.ptp(V[idx], 0)
        if ext[0] < 2.0 and ext[2] < 2.0 and ext[1] > 0.2 * H:   # (strings come in pieces)
            string_verts[idx] = True
    if not string_verts.any():
        raise SystemExit(f'{bid}: no string found')
    cy = (V[used, 1].max() + V[used, 1].min()) / 2
    sx = V[string_verts, 0].mean()
    limb = used[~string_verts[used]]
    flip = sx > V[limb, 0].mean()
    P = V.copy(); P[:, 1] -= cy
    if flip:
        P[:, 0] = -P[:, 0]; sx = -sx
        F = F[:, ::-1]
    S = min(STRING_Y / (np.ptp(V[string_verts, 1]) / 2), 64.0 / (H / 2))   # string ends on the rig, bow never taller than ~128
    P[:, 1] *= S; P[:, 2] = (P[:, 2] - P[used, 2].mean()) * S
    near = limb[np.abs(P[limb, 1]) < 4]
    gx = P[near, 0].mean()
    k = max(0.55, (GRIP_X - STRING_X) / (gx - sx))   # depth fitted to the rig (seen edge-on in first person), never flattened too far
    P[:, 0] = GRIP_X + (P[:, 0] - gx) * k
    # split: string (energy), trim (spurs/blades away from the limb line, glowing), body
    yb = np.round(P[limb, 1] / 4).astype(int)
    centre = {}
    for b in np.unique(yb):
        centre[b] = np.median(P[limb[yb == b], 0])
    cen = P[F].mean(1)
    fs = string_verts[F].all(1)
    off = np.array([abs(c[0] - centre.get(int(round(c[1] / 4)), c[0])) for c in cen])
    trim = (~fs) & ((off > 3.2) | (np.abs(cen[:, 2]) > 2.6))
    groups = {'string': F[fs], 'trim': F[trim], 'body': F[(~fs) & (~trim)]}
    N = normals(P, F)
    kind = BODY_KIND[bid]
    d, n = skymat.make(kind, body_col, seed=len(bid), size=2048)   # (Noah: 2K, like the spells)
    tex = {'d': save_dds(d, f'{bid}_d.dds'), 'n': save_dds(n, f'{bid}_n.dds', True),
           'flat': save_dds(Image.new('RGBA', (32, 32), (128, 128, 255, 200)), 'flat_n.dds', True)}
    gcol = tuple(int(c * 255) for c in glow)
    tex['glow_d'] = save_dds(Image.new('RGB', (32, 32), gcol), f'{bid}_glow_d.dds')
    tex['glow_g'] = save_dds(Image.new('RGB', (32, 32), tuple(min(255, c + 60) for c in gcol)), f'{bid}_glow_g.dds')
    ec = ','.join(f'{c:.3f}' for c in glow)
    lines = [f'template {TEMPLATE}', f'out {OUT / "Meshes/CurseOfAlduin/bow" / (bid + ".nif")}']
    for g, GF in groups.items():
        if not len(GF):
            continue
        vid = np.unique(GF); remap = -np.ones(len(P), int); remap[vid] = np.arange(len(vid))
        if g == 'string':
            lines.append(f'shape {bid}_string diffuse={tex["glow_d"]} normal={tex["flat"]} glow={tex["glow_g"]} emit={ec} emitmult=4 alpha=add spec=1 gloss=200')
        elif g == 'trim':
            lines.append(f'shape {bid}_trim diffuse={tex["glow_d"]} normal={tex["flat"]} glow={tex["glow_g"]} emit={ec} emitmult=2.4 spec=1.2 gloss=150')
        else:
            lines.append(f'shape {bid}_body diffuse={tex["d"]} normal={tex["n"]} spec={ {"wood": 0.4, "wood_dark": 0.5, "iron": 1.3, "bone": 0.6, "steel": 1.6, "bronze": 1.4}[kind] } gloss=60')
        for v in vid:
            x, y, z = P[v]; u, w = (T[v] if len(T) > v else (0, 0))
            if g == 'string':
                ws = [(SB1 if y < 0 else SB2, 1.0)]
            else:
                ws = limb_weights(y)
            nx, ny, nz = N[v]
            lines.append(f'v {x:.4f} {y:.4f} {z:.4f} {u:.5f} {w:.5f} {nx:.4f} {ny:.4f} {nz:.4f} ' + ' '.join(f'{b} {wt:.4f}' for b, wt in ws))
        lines += [f't {a} {b} {c}' for a, b, c in remap[GF]]
    work = ROOT / 'build' / 'bows'; work.mkdir(parents=True, exist_ok=True)
    (OUT / 'Meshes/CurseOfAlduin/bow').mkdir(parents=True, exist_ok=True)
    jf = work / f'{bid}.job'; jf.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([CONV, str(jf)], capture_output=True, text=True)
    print(r.stdout.strip().split('\n')[-1] if r.stdout else '', r.stderr.strip())
    if r.returncode:
        raise SystemExit(f'bowconv failed for {bid}')
    print(f'{bid}: {src} flip={flip} depth x{k:.2f} string {len(groups["string"])} trim {len(groups["trim"])} body {len(groups["body"])} tris')

if __name__ == '__main__':
    for b in BOWS:
        build(b)
