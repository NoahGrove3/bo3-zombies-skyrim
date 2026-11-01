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
import bowart
# the Echo Altar's bows (sheets/bows.json): source basemesh, limb finish (skymat kind), body colour, glow colour
import json
_ROWS = json.load(open(ROOT / 'sheets' / 'bows.json'))['rows']
BODY_KIND = {r['id']: r['body'] for r in _ROWS}
BOWS = {r['id']: (r['mesh'], tuple(r['bodyColor']), tuple(r['glow'])) for r in _ROWS}
ELEMENT = {r['id']: r['element'] for r in _ROWS}
# (Noah: the Ranger's wonder) the Subsuming Vortex: not on the altar's wheel, made the same way - black-violet bone, violet light
WONDERS = {'vortex': ('Bow-03', 'steel', (34, 20, 52), (0.62, 0.22, 1.0), 'void')}
for _id, (_m, _k, _b, _g, _e) in WONDERS.items():
    BOWS[_id] = (_m, _b, _g); BODY_KIND[_id] = _k; ELEMENT[_id] = _e

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

def save_mips(im, rel, alpha=False):
    """DDS with its full mip chain (ImageMagick): without one a 2K texture on a bow in your hands crawls and shimmers"""
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'bow' / rel; p.parent.mkdir(parents=True, exist_ok=True)
    work = ROOT / 'build' / 'bows'; work.mkdir(parents=True, exist_ok=True)
    tmp = work / (rel + '.png'); im.convert('RGBA' if alpha else 'RGB').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', f'dds:compression={"dxt5" if alpha else "dxt1"}', '-define', 'dds:mipmaps=12', str(p)], check=True)
    return f'CurseOfAlduin\\bow\\{rel}'

GLYPHS = [[(0, -1, 0, 1), (0, -0.3, 0.6, -0.9)], [(-0.5, 1, 0, -1), (0.5, 1, 0, -1), (-0.3, 0.3, 0.3, 0.3)], [(0, -1, 0, 1), (-0.6, 0, 0.6, 0)],
          [(-0.6, -1, 0.6, 1), (0.6, -1, -0.6, 1)], [(0, -1, 0, 1), (0, 0.1, 0.6, 0.9)], [(-0.6, -0.8, 0.6, -0.8), (0, -0.8, 0, 1)],
          [(-0.5, -1, -0.5, 1), (-0.5, 0, 0.5, -0.6), (-0.5, 0, 0.5, 0.6)], [(0, -1, 0.6, 0), (0.6, 0, 0, 1), (0, 1, -0.6, 0), (-0.6, 0, 0, -1)],
          [(-0.5, -1, 0.5, -1), (0, -1, 0, 1), (-0.5, 1, 0.5, 1)], [(0, -1, 0, 1), (0, -1, 0.55, -0.45), (0, -0.2, 0.55, 0.35)]]

def bow_maps(kind, body_col, glow, seed, W=2048, H=1024):
    """the bow's own finish, laid out on its limb (u: bottom tip to top tip, v: round the limb, the seam on its back):
    the limb's material with its grain running the length, a wrapped leather grip between two collars, metal-capped tips,
    and a channel carved down each flank of both limbs with the Curse's runes cut into it, glowing in the bow's element"""
    from PIL import ImageDraw
    d0, n0 = skymat.make(kind, body_col, seed=seed, size=W)
    mat = np.asarray(d0.rotate(90, expand=True).resize((W, W)), np.float32)[:H]          # grain along the limb (u)
    mh = np.asarray(n0.rotate(90, expand=True).resize((W, W)).convert('L'), np.float32)[:H] / 255
    lth_d, lth_n = skymat.make('leather', (58, 38, 26), seed=seed + 3, size=W)
    lth = np.asarray(lth_d.resize((W, W)), np.float32)[:H]
    metal_kind = {'wood': 'bronze', 'wood_dark': 'iron', 'iron': 'steel', 'bone': 'bronze', 'steel': 'silver', 'bronze': 'gold'}.get(kind, 'steel')
    mt_d, _ = skymat.make(metal_kind, None, seed=seed + 5, size=W)
    met = np.asarray(mt_d.rotate(90, expand=True).resize((W, W)), np.float32)[:H]
    u = np.linspace(0, 1, W)[None, :].repeat(H, 0); v = np.linspace(0, 1, H)[:, None].repeat(W, 1)
    grip = (np.abs(u - 0.5) < 0.065).astype(np.float32)
    collar = ((np.abs(np.abs(u - 0.5) - 0.072) < 0.008) | (u < 0.035) | (u > 0.965)).astype(np.float32)
    # the rune channels: down both flanks (v 0.25, 0.75) of both limbs
    ch = np.zeros((H, W), np.float32)
    for vc in (0.25, 0.75):
        ch = np.maximum(ch, np.clip(1 - np.abs(v - vc) / 0.06, 0, 1) ** 0.35 * (((u > 0.09) & (u < 0.41)) | ((u > 0.59) & (u < 0.91))))
    gm = Image.new('L', (W, H), 0); gd = ImageDraw.Draw(gm)
    rnd = np.random.default_rng(seed)
    for vc in (0.25, 0.75):
        for u0, u1 in ((0.1, 0.4), (0.6, 0.9)):
            n = 11
            for k in range(n):
                cx = (u0 + (k + 0.5) / n * (u1 - u0)) * W; cy = vc * H; hs = H * 0.038
                for x0, y0, x1, y1 in GLYPHS[int(rnd.integers(len(GLYPHS)))]:
                    gd.line([(cx + y0 * hs * 0.8, cy + x0 * hs), (cx + y1 * hs * 0.8, cy + x1 * hs)], fill=255, width=max(3, int(hs * 0.24)))
    gly = np.asarray(gm.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
    # albedo: the limb, the grip's leather, the metal; the channel darker, the runes holding their colour even unlit
    col = mat * (1 - grip[..., None]) + lth * grip[..., None]
    limbs = (1 - grip) * (1 - collar)
    # a composite bow's layers: pale horn on the belly (toward you), dark sinew on the back, inlaid metal pinstripes
    # bounding each rune channel
    belly = np.clip(1 - np.abs(v - 0.5) / 0.09, 0, 1) ** 0.5 * limbs
    back = np.clip(1 - np.minimum(v, 1 - v) / 0.07, 0, 1) ** 0.5 * limbs
    pin = np.zeros((H, W), np.float32)
    for vc in (0.185, 0.315, 0.685, 0.815):
        pin = np.maximum(pin, np.clip(1 - np.abs(v - vc) * H / 2.5, 0, 1))
    pin *= (((u > 0.08) & (u < 0.42)) | ((u > 0.58) & (u < 0.92)))
    horn = np.array([186, 168, 138], np.float32) * 0.6 + np.array(body_col, np.float32) * 0.4
    col = col * (1 - 0.55 * belly[..., None]) + horn * (0.55 * belly[..., None]) * (0.85 + 0.3 * mh[..., None])
    col *= (1 - 0.35 * back[..., None])
    col = col * (1 - collar[..., None]) + met * collar[..., None]
    col = col * (1 - pin[..., None]) + met * pin[..., None] * 1.15
    col *= (1 - 0.35 * ch[..., None])
    gc = np.array(glow, np.float32) * 255
    col = col * (1 - gly[..., None]) + (gc * 0.55)[None, None] * gly[..., None]
    # edge wear on the metal and the limb's arrises (round the back seam and along v 0/0.5)
    height = mh * 0.5 * (1 - collar) + collar * 0.75 + 0.2 * pin + grip * (0.55 + 0.25 * np.asarray(lth_n.resize((W, W)).convert('L'), np.float32)[:H] / 255) - 0.25 * ch - 0.35 * gly
    gy, gx = np.gradient(height * 7.0)
    nrm = np.dstack([-gx, gy, np.ones_like(gx)]); nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    spec = np.clip(0.18 + 0.55 * collar + 0.5 * pin + 0.15 * belly + 0.15 * (1 - grip) * (1 - collar) + 0.3 * gly, 0, 1)
    nimg = Image.fromarray(np.dstack([(nrm * 0.5 + 0.5) * 255, spec[..., None] * 255]).astype(np.uint8), 'RGBA')
    halo = np.asarray(Image.fromarray((gly * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(5)), np.float32) / 255
    g = np.clip(gly + 0.45 * halo + 0.08 * ch, 0, 1)
    gimg = Image.fromarray((g[..., None] * gc[None, None]).clip(0, 255).astype(np.uint8), 'RGB')
    return Image.fromarray(col.clip(0, 255).astype(np.uint8), 'RGB'), nimg, gimg


def limb_uv(P, centre, idx):
    """our own UVs for the limb: u from bottom tip to top tip, v round its section (0 = its back, toward the target)"""
    x, y, z = P[idx, 0], P[idx, 1], P[idx, 2]
    cx = np.array([centre.get(int(round(yy / 4)), xx) for xx, yy in zip(x, y)])
    uu = np.clip((y + 64.0) / 128.0, 0, 1)
    vv = (np.arctan2(z, cx - x) / (2 * np.pi) + 0.5) % 1.0
    return uu, vv


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
    # (Noah: the string read as a floating glowing bar) a thin cord: the string pieces drawn in to a fraction of their girth
    si = np.where(string_verts)[0]
    yb_s = np.round(P[si, 1] / 3).astype(int)
    for b in np.unique(yb_s):
        idx = si[yb_s == b]
        cx_, cz_ = P[idx, 0].mean(), P[idx, 2].mean()
        P[idx, 0] = cx_ + (P[idx, 0] - cx_) * 0.28
        P[idx, 2] = cz_ + (P[idx, 2] - cz_) * 0.28
    N = normals(P, F)
    kind = BODY_KIND[bid]
    if bid in WONDERS:   # (Noah: the Vortex needs higher-quality textures) 4K x 2K limb, 2K x 2K blades, its own void pattern
        d, n, gl, td, tn, tg = void_maps(kind, body_col, glow, seed=len(bid))
    else:
        d, n, gl = bowart.body_maps(kind, body_col, glow, seed=len(bid), element=ELEMENT[bid])   # (Noah: 2K, hand-worked: runes, knotwork, scrollwork, leather)
        td, tn, tg = bowart.trim_maps(ELEMENT[bid], glow, seed=len(bid) + 40)
    tex = {'d': save_mips(d, f'{bid}_d.dds'), 'n': save_mips(n, f'{bid}_n.dds', True), 'g': save_mips(gl, f'{bid}_g.dds'),
           'td': save_mips(td, f'{bid}_trim_d.dds'), 'tn': save_mips(tn, f'{bid}_trim_n.dds', True), 'tg': save_mips(tg, f'{bid}_trim_g.dds'),
           'flat': save_dds(Image.new('RGBA', (32, 32), (128, 128, 255, 200)), 'flat_n.dds', True)}
    gcol = tuple(int(c * 255) for c in glow)
    tex['cord'] = save_dds(Image.new('RGB', (32, 32), (46, 40, 36)), 'cord_d.dds')   # waxed sinew
    tex['glow_d'] = save_dds(Image.new('RGB', (32, 32), gcol), f'{bid}_glow_d.dds')
    tex['glow_g'] = save_dds(Image.new('RGB', (32, 32), tuple(min(255, c + 60) for c in gcol)), f'{bid}_glow_g.dds')
    ec = ','.join(f'{c:.3f}' for c in glow)
    lines = [f'template {TEMPLATE}', f'out {OUT / "Meshes/CurseOfAlduin/bow" / (bid + ".nif")}']
    for g, GF in groups.items():
        if not len(GF):
            continue
        vid = np.unique(GF); remap = -np.ones(len(P), int); remap[vid] = np.arange(len(vid))
        if g == 'string':
            lines.append(f'shape {bid}_string diffuse={tex["cord"]} normal={tex["flat"]} glow={tex["glow_g"]} emit={ec} emitmult=0.35 spec=0.3 gloss=30')
        elif g == 'trim':
            lines.append(f'shape {bid}_trim diffuse={tex["td"]} normal={tex["tn"]} glow={tex["tg"]} emit={ec} emitmult=2.2 spec=1.4 gloss=120')
        else:
            lines.append(f'shape {bid}_body diffuse={tex["d"]} normal={tex["n"]} glow={tex["g"]} emit={ec} emitmult=3.0 spec={ {"wood": 0.4, "wood_dark": 0.5, "iron": 1.3, "bone": 0.6, "steel": 1.6, "bronze": 1.4}[kind] } gloss=60')
        if g in ('body', 'trim'):
            # its own UVs (limb_uv), the triangles that cross the seam on its back given their own copies of those corners
            uu, vv = limb_uv(P, centre, np.arange(len(P)))
            corners, newF = {}, []
            for tri in GF:
                vs = vv[tri]; wrap = vs.max() - vs.min() > 0.5
                ids = []
                for a in tri:
                    key = (int(a), bool(wrap and vv[a] < 0.5))
                    if key not in corners:
                        corners[key] = len(corners)
                    ids.append(corners[key])
                newF.append(ids)
            for (a, up) in corners:
                x, y, z = P[a]; nx, ny, nz = N[a]
                lines.append(f'v {x:.4f} {y:.4f} {z:.4f} {uu[a]:.5f} {vv[a] + (1.0 if up else 0.0):.5f} {nx:.4f} {ny:.4f} {nz:.4f} ' + ' '.join(f'{b} {wt:.4f}' for b, wt in limb_weights(y)))
            lines += [f't {a} {b} {c}' for a, b, c in newF]
            continue
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
    if bid in WONDERS:
        void_fx(bid, glow)


# ---- the Subsuming Vortex's finish and living effects (Noah: "needs higher quality textures and animated FX like the
# other elemental bows do") - the same treatment tools/bowmodels.py gives the altar's bows, in the void's black-violet
SPECTRAL = '/home/claude/work/dl/Spectral Bow - Fire/data/meshes/weapons/boundweapons/wte_specbow.nif'


def void_pattern(element, W, H, seed):
    """the void alive in the limb, drawn to the map's own size (bowart's is laid out for 2K x 1K): a nebula of violet
    seams with whirlpools along it, each curling into a black heart, and pin-prick stars"""
    if element != 'void':
        return _element_pattern(element, W, H, seed)
    from PIL import ImageDraw
    rng = np.random.default_rng(seed)
    k = H / 1024.0
    im = Image.new('L', (W, H), 0); dr = ImageDraw.Draw(im)
    for _ in range(int(60 * k * k)):                     # whirlpools, 3 arms each
        cx, cy = rng.uniform(0, W), rng.uniform(0, H); s = rng.uniform(26, 60) * k; turn = rng.choice([-1, 1])
        for arm in range(3):
            pts = []
            for t in np.linspace(0, 1, 70):
                a = arm * 2.094 + turn * t * 2.6 * np.pi
                r = s * (1 - t) + 4 * k
                pts.append((cx + math.cos(a) * r * 1.6, cy + math.sin(a) * r * 0.75))
            dr.line(pts, fill=255, width=max(3, int(4 * k)))
        dr.ellipse([cx - 6 * k, cy - 4 * k, cx + 6 * k, cy + 4 * k], fill=0)
    m = np.asarray(im.filter(ImageFilter.GaussianBlur(1.5 * k)), np.float32) / 255
    neb = bowart.fbm(W, H, 10, seed + 1, 5, stretch=(4, 1))                 # nebula seams, long along the limb
    seam = np.clip(1 - np.abs(neb - 0.5) / 0.035, 0, 1) ** 1.5 * 0.7
    stars = (rng.random((H, W)) > 0.9993).astype(np.float32)
    stars = np.clip(np.asarray(Image.fromarray((stars * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.0 * k)), np.float32) / 255 * 6, 0, 1)
    return np.clip(np.maximum(m, seam) + stars, 0, 1)


_element_pattern = bowart.element_pattern


def void_maps(kind, body_col, glow, seed):
    bowart.element_pattern = void_pattern
    try:
        d, n, g = bowart.body_maps(kind, body_col, glow, seed=seed, W=4096, H=2048, element='void')
        td, tn, tg = bowart.trim_maps('void', glow, seed=seed + 40, W=2048, H=2048)
    finally:
        bowart.element_pattern = _element_pattern
    return d, n, g, td, tn, tg


def void_palette(bid, glow):
    """the flowing skin's colour ramp: black-violet at the bottom, the void's violet, a pale lilac at the hot top"""
    x = np.linspace(0, 1, 256)
    g = np.array(glow, np.float32); deep = g * 0.25; hot = g * 0.35 + 0.65
    col = np.where(x[:, None] < 0.6, deep + (g - deep) * (x[:, None] / 0.6), g + (hot - g) * ((x[:, None] - 0.6) / 0.4))
    alpha = np.clip((x - 0.08) / 0.92, 0, 1) ** 0.9; alpha[x < 0.08] = 0
    img = np.repeat(np.hstack([col * 255, alpha[:, None] * 255])[None], 64, 0).clip(0, 255).astype(np.uint8)
    work = ROOT / 'build' / 'bows'; work.mkdir(parents=True, exist_ok=True)
    tmp = work / f'{bid}_flamegrad.png'; Image.fromarray(img, 'RGBA').save(tmp)
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'bow' / f'{bid}_flamegrad.dds'
    subprocess.run(['convert', str(tmp), '-define', 'dds:compression=none', '-define', 'dds:mipmaps=1', str(p)], check=True)
    return f'textures\\CurseOfAlduin\\bow\\{bid}_flamegrad.dds'


def void_fx(bid, glow):
    """1. as the altar's bows (bowmodels.py): the Spectral Bow's flowing swirl shader on a skin a hair outside the limb and
    blades, its palette the void's (fxshell) - the dark swirls course along the bow
    2. a little black hole turning at each limb tip, hung on the limb's outer bone so it rides the draw, and one on the
    back of the riser: the void flipbook (tools/bowarrows.py fx_void, 8x8 at 24 fps) on three crossed cards each"""
    import bowarrows
    bowarrows.OUT = OUT   # (bowarrows reads its out dir from argv - ours differ)
    nif = OUT / 'Meshes/CurseOfAlduin/bow' / (bid + '.nif')
    r = subprocess.run(['/home/claude/tools/fxshell', str(nif), SPECTRAL, '6', str(nif), void_palette(bid, glow), '1', '1', '1', '0.42', '0.12', '0.025', '0.08'], capture_output=True, text=True)
    print('  ', r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit(f'fxshell failed for {bid}')
    fxtex = bowarrows.save_dds(bowarrows.atlas(bowarrows.fx_void), f'{bid}_arrowfx.dds')
    g = ','.join(f'{c:.3f}' for c in glow)
    job = [f'base {nif}', f'out {nif}']
    C = bowarrows.COLS
    # (bone, centre in its own space, half size, phase): the limb tips sit 31 along the outer limb bones' +X (the nocks)
    spots = [('Bow_UpBone2', (31.5, 0.0, 0.0), 3.2, 0.0), ('Bow_LoBone2', (31.5, 0.0, 0.0), 3.2, 1.3), ('Bow_MidBone', (6.4, 4.6, 0.0), 2.4, 0.6)]
    for i, (bone, (cx, cy, cz), s, ph) in enumerate(spots):
        for j, (ax, ay) in enumerate((((1, 0, 0), (0, 1, 0)), ((1, 0, 0), (0, 0, 1)), ((0, 1, 0), (0, 0, 1)))):
            job.append(f'shape {bid}_void{i}{j}_flame tex={fxtex} color={g},1 scale=1.6 blend=add parent={bone} flip={C}x{bowarrows.ROWS} fps=24 phase={ph + 0.4 * j:.2f}')
            for du, dv, u, v in ((-1, 1, 0, 0), (1, 1, 1 / C, 0), (1, -1, 1 / C, 1 / bowarrows.ROWS), (-1, -1, 0, 1 / bowarrows.ROWS)):
                x = cx + s * (du * ax[0] + dv * ay[0]); y = cy + s * (du * ax[1] + dv * ay[1]); z = cz + s * (du * ax[2] + dv * ay[2])
                job.append(f'v {x:.3f} {y:.3f} {z:.3f} {u} {v} 1 1 1 1')
            job += ['t 0 1 2', 't 0 2 3']
    work = ROOT / 'build' / 'bows'
    jf = work / f'{bid}_void.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print('  ', r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit(f'fxwrite failed for {bid}')

if __name__ == '__main__':
    only = sys.argv[4:]   # (optional: just these bows)
    for b in BOWS:
        if not only or b in only:
            build(b)
