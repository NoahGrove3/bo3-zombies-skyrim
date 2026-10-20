"""The Curse of Alduin elemental bows from Noah's chosen models (his Downloads: Sketchfab FBX / DAE / glTF / .blend,
turned into OBJ parts by tools/modelconv/objexp and work/tools/blend2obj.py into models_in/newbows/<Model>/).

Each model is stood on Skyrim's bow rig (tools/bowconv: the iron bow's Bow_* bones and behavior graph) so the limbs bend
and the string draws with Skyrim's own bow animations:
  - its long axis up the rig's y, the string toward -x, the grip on the rig's grip, scaled so its nocks sit on the rig's
    string ends; the model's own string (if any) gives the nocks, else the limb tips do
  - its own string replaced by a thin cord on the string bones (the draw pulls it), everything else on the limb bones
  - its own textures (base colour, normal + specular from roughness/metal, glow from its emissive) tinted to the
    element where the model's colours don't already say it, or (models without UVs) our hand-worked runic finish
  - then the Spectral Bow's flame wings (Noah's download, by its author) grafted on, recoloured to the element (fxgraft)

    python tools/bowmodels.py [bow id ...]"""
import json, math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

ROOT = pathlib.Path(__file__).parent.parent
SRC = pathlib.Path('/home/claude/models_in/newbows')
TEMPLATE = '/home/claude/work/vanilla/ironbow.nif'
SPECTRAL = '/home/claude/work/dl/Spectral Bow - Fire/data/meshes/weapons/boundweapons/wte_specbow.nif'
OUT = ROOT / 'assets' / 'fx'
CONV, GRAFT, SIMPLIFY = '/home/claude/tools/bowconv', '/home/claude/tools/fxgraft', '/home/claude/tools/simplify'
WORK = ROOT / 'build' / 'bowmodels'
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import bowart, bows as oldbows
oldbows.OUT = OUT   # (bows.py reads its out dir from argv - ours are bow ids)

GRIP_X, STRING_X, STRING_Y = 1.31, -13.69, 54.8
MID, LO1, LO2, SB1, UP1, UP2, SB2 = range(7)
ROWS = {r['id']: r for r in json.load(open(ROOT / 'sheets' / 'bows.json'))['rows']}

# per bow: source folder, parts (file stem -> role), material -> textures, element tint, flips
#   role: body | glow (fully lit in the element) | string (only gives the nocks) | drop
MODELS = {
    'dawnspire': dict(src='Dawnspire', up=2, parts={'Cylinder.002': 'string'},
                      mats={'m0': ('ARCODIO2_ARCH_BaseColor.png', 'ARCODIO2_ARCH_Normal.png', None, 0.35),
                            'm1': ('ARCODIO2_DARK_BaseColor.png', 'ARCODIO2_DARK_Normal.png', None, 0.25),
                            'm2': ('ARCODIO2_GOLD_BaseColor.png', 'ARCODIO2_GOLD_Normal.png', 'bright', 0.9)}),
    'rimehorn': dict(src='Rimehorn', up=1, drop_x_above=0.145,
                     mats={'m0': ('BowArrowMarbleBlue_Base_color.png', 'BowArrowMarbleBlue_Normal_DirectX.png', 'bright', 0.7,
                                  'BowArrowMarbleBlue_Roughness.png')}, dx_normal=True),
    'skyhowl': dict(src='Skyhowl', up=1, parts={'8_low': 'drop'}, drop_mats={'m1'}, string_comp=True,
                    mats={'m0': ('bow_lp_Circle_0010_BaseColor.png', 'bow_lp_Circle_0010_Normal.png', 'bow_lp_Circle_0010_Emissive.png', 0.5,
                                 'bow_lp_Circle_0010_Roughness.png')}),
    'thirstbow': dict(src='Thirstbow', up=1, string_comp=True, tint=(120, 14, 22), tint_amt=0.45,
                      mats={'m0': ('lambert1_albedo.jpg', 'lambert1_normal.png', 'lambert1_emissive.jpg', 0.8, 'lambert1_roughness.jpg')}),
    'stormreach': dict(src='Stormreach', up=1, parts={'Rope': 'string', 'Rope_Circle': 'string', 'Rope_Part1': 'string', 'Sphere': 'glow',
                                                       'Glowy': 'glow', 'Glowy2': 'glow', 'Glowy3': 'glow'},
                       simplify={'Bow_Main_': 70000},
                       mats={'m0': ('holographic_hd_jpg.jpg', None, 'self', 0.6), 'm1': ('lightgold_albedo.png', 'lightgold_normal-ogl.png', None, 0.9),
                             'm2': ('Titanium-Scuffed_basecolor.png', 'Titanium-Scuffed_normal.png', None, 0.8), 'm3': (None, None, 'self', 0.6)}),
    'emberstring': dict(src='elf-bow', up=2, parts={'Cylinder.002': 'string', 'Plane': 'drop', 'FLASH3': 'trim'},
                        simplify={'Cylinder.001': 36000, 'FLASH3': 16000, 'handel': 16000}, runic=True,
                        # the elf bow's own colour scheme, turned to fire: a charred limb with the runes (our finish),
                        # ember-gold tendrils, black leather grip, flame-leaf blades
                        # (Noah: the Emberstring looked smooth, not detailed) the limb wears the full runic finish, the
                        # flame-leaf blades forged metal etched with filigree and veins of fire; the flame skin over it
                        # dimmed so the work shows through
                        matcolors={'Material.002': ((26, 20, 18), 0.0), 'Material.005': ((60, 50, 44), 0.0)},
                        trim_mats={'Material.001'}, shell=0.4),
}


def load_obj(path):
    V, T, F, M = [], [], [], []
    mat = ''
    tv = []
    for l in open(path):
        if l.startswith('v '): V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt '): T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('usemtl'): mat = l.split()[1]
        elif l.startswith('f '):
            ids = [x.split('/') for x in l.split()[1:]]
            vi = [int(x[0]) - 1 for x in ids]; ti = [int(x[1]) - 1 if len(x) > 1 and x[1] else int(x[0]) - 1 for x in ids]
            for i in range(1, len(vi) - 1):
                F.append((vi[0], vi[i], vi[i + 1])); tv.append((ti[0], ti[i], ti[i + 1])); M.append(mat)
    return np.array(V, float), np.array(T, float) if T else None, np.array(F, int), np.array(tv, int), np.array(M)


def welded_components(V, F, eps):
    key = np.round(V / eps).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
    parent = np.arange(inv.max() + 1)
    def find(a):
        r = a
        while parent[r] != r: r = parent[r]
        while parent[a] != r: parent[a], a = r, parent[a]
        return r
    for a, b, c in inv[F]:
        ra, rb, rc = find(a), find(b), find(c); parent[rb] = ra; parent[find(rc)] = ra
    return np.array([find(i) for i in inv])


def simplified(path, target):
    out = WORK / (path.stem + f'_s{target}.obj')
    if not out.exists():
        r = subprocess.run([SIMPLIFY, str(path), str(out), str(target)], capture_output=True, text=True)
        if r.returncode: raise SystemExit(r.stdout + r.stderr)
    return out


DIM = 0.45   # (Noah: the bows were far too bright) every glow, lit part and flowing flame skin at under half


def detail(da, na, seed, amt=1.0):
    """(Noah: more detail, more real) a worn, handled finish over a model's own texture: cavities darkened from its
    normal map's curvature (grime in the crevices), edges caught a little lighter, fine grain and scratches in both the
    colour and the normal map"""
    h, w = da.shape[:2]
    n = (na[..., :3].astype(np.float32) - 128) / 127
    curv = np.gradient(n[..., 0], axis=1) + np.gradient(n[..., 1], axis=0)
    curv = np.asarray(Image.fromarray(((curv.clip(-0.5, 0.5) + 0.5) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5)), np.float32) / 255 - 0.5
    rng = np.random.default_rng(seed)
    grain = rng.normal(0, 1, (h // 2, w // 2)).astype(np.float32)
    grain = np.asarray(Image.fromarray(((grain * 0.25 + 0.5).clip(0, 1) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC), np.float32) / 255 - 0.5
    smudge = np.asarray(Image.fromarray(((rng.normal(0, 1, (h // 32, w // 32)) * 0.25 + 0.5).clip(0, 1) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC), np.float32) / 255 - 0.5
    scr = Image.new('L', (w, h), 0); dr = ImageDraw.Draw(scr)
    for _ in range(int(w * h / 9000)):
        x, y = rng.uniform(0, w), rng.uniform(0, h); a = rng.uniform(0, 2 * math.pi); L = rng.uniform(4, 30)
        dr.line([(x, y), (x + math.cos(a) * L, y + math.sin(a) * L)], fill=int(rng.uniform(60, 160)), width=1)
    scr = np.asarray(scr, np.float32) / 255
    shade = 1.0 + amt * (0.9 * curv.clip(-0.5, 0) + 0.35 * curv.clip(0, 0.5) + 0.10 * grain + 0.12 * smudge) + 0.10 * amt * scr
    da = da * shade[..., None]
    na = na.copy()
    bump = (grain * 10 + scr * 18).astype(np.float32)
    na[..., 0] = (na[..., 0].astype(np.float32) + np.gradient(bump, axis=1) * 6).clip(0, 255).astype(np.uint8)
    na[..., 1] = (na[..., 1].astype(np.float32) + np.gradient(bump, axis=0) * 6).clip(0, 255).astype(np.uint8)
    return da, na


def tex_dds(img, rel, alpha=False, size=2048):
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'bow' / rel; p.parent.mkdir(parents=True, exist_ok=True)
    if max(img.size) > size: img = img.resize((size, size), Image.LANCZOS)
    tmp = WORK / (rel.replace('/', '_') + '.png'); img.convert('RGBA' if alpha else 'RGB').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', f'dds:compression={"dxt5" if alpha else "dxt1"}', '-define', 'dds:mipmaps=12', str(p)], check=True)
    return f'CurseOfAlduin\\bow\\{rel}'.replace('/', '\\')


def build(bid):
    cfg = MODELS[bid]; row = ROWS[bid]; glow = tuple(row['glow']); gcol = np.array(glow) * 255
    d = SRC / cfg['src']
    parts = []   # (role, V, T, F, TF, M)
    for p in sorted(d.glob('*.obj')):
        role = cfg.get('parts', {}).get(p.stem, 'body')
        if role == 'drop': continue
        if p.stem in cfg.get('simplify', {}):
            # (the simplifier keeps no materials) each material's faces simplified on their own, then put back together
            V0, T0, F0, TF0, M0 = load_obj(p)
            Vs, Ts, Fs, Ms = [], [], [], []
            off = 0
            for mat in np.unique(M0):
                sel = F0[M0 == mat]; vid = np.unique(sel); remap = -np.ones(len(V0), int); remap[vid] = np.arange(len(vid))
                sub = WORK / f'{p.stem}_{mat}.obj'; WORK.mkdir(parents=True, exist_ok=True)
                with open(sub, 'w') as f:
                    f.writelines(f'v {x} {y} {z}\n' for x, y, z in V0[vid])
                    if T0 is not None: f.writelines(f'vt {u} {v}\n' for u, v in T0[vid])
                    f.writelines(f'f {a+1}/{a+1} {b+1}/{b+1} {c+1}/{c+1}\n' for a, b, c in remap[sel])
                tgt = max(500, int(cfg['simplify'][p.stem] * len(sel) / len(F0)))
                V1, T1, F1, _, _ = load_obj(simplified(sub, tgt) if len(sel) > tgt else sub)
                Vs.append(V1); Ts.append(T1 if T1 is not None else np.zeros((len(V1), 2))); Fs.append(F1 + off); Ms += [mat] * len(F1); off += len(V1)
            V, T, F, M = np.vstack(Vs), np.vstack(Ts), np.vstack(Fs), np.array(Ms); TF = F.copy()
            if T0 is None: T = None
        else:
            V, T, F, TF, M = load_obj(p)
        keep = ~np.isin(M, list(cfg.get('drop_mats', ())))
        F, TF, M = F[keep], TF[keep], M[keep]
        parts.append([role, V, T, F, TF, M, p.stem])
    # model space: principal axes of the whole model (the long one kept pointing the way the source has up)
    allV = np.vstack([q[1][np.unique(q[3])] for q in parts if q[0] != 'string' and len(q[3])])
    c0 = allV.mean(0)
    _, _, Vt = np.linalg.svd(allV[::max(1, len(allV) // 20000)] - c0, full_matrices=False)
    a1, a3 = Vt[0], Vt[2]
    if a1[cfg['up']] < 0: a1 = -a1
    a2 = np.cross(a3, a1)
    PCA = np.stack([a1, a2, a3])
    up, depth, thick = 0, 1, 2
    ext = np.ptp((allV - c0) @ PCA.T, 0)
    L = ext[up]
    if cfg.get('drop_x_above') is not None:
        for q in parts:
            V, F = q[1], q[3]
            if not len(F): continue
            c = welded_components(V, F, L * 1e-4)
            cen = {}
            for k in np.unique(c[F[:, 0]]):
                idx = np.unique(F[c[F[:, 0]] == k]); cen[k] = V[idx, 0].mean()
            keep = np.array([cen[k] <= cfg['drop_x_above'] for k in c[F[:, 0]]])
            q[3], q[4], q[5] = F[keep], q[4][keep], q[5][keep]
    # strings among the body's pieces (welded components long along the bow and thin across)
    if cfg.get('string_comp'):
        for q in parts:
            if q[0] != 'body': continue
            V, F = q[1], q[3]
            Vp = (V - c0) @ PCA.T
            c = welded_components(V, F, L * 1e-4); fc = c[F[:, 0]]
            isstr = np.zeros(len(F), bool)
            for k in np.unique(fc):
                idx = np.unique(F[fc == k]); e = np.ptp(Vp[idx], 0)
                if e[up] > 0.35 * L and e[thick] < 0.03 * L and e[depth] < 0.12 * L and len(idx) < 400:
                    isstr[fc == k] = True
            if isstr.any():
                parts.append(['string', V, q[2], F[isstr], q[4][isstr], q[5][isstr], q[6] + '_string'])
                q[3], q[4], q[5] = F[~isstr], q[4][~isstr], q[5][~isstr]
    body = [q for q in parts if q[0] != 'string' and len(q[3])]
    strings = [q for q in parts if q[0] == 'string' and len(q[3])]
    BV = np.vstack([q[1][np.unique(q[3])] for q in body])
    BP = (BV - c0) @ PCA.T
    lo, hi = BP[:, up].min(), BP[:, up].max(); mid = (lo + hi) / 2
    grip = BV[np.abs(BP[:, up] - mid) < 0.08 * (hi - lo)]
    if strings:
        SV = np.vstack([q[1][np.unique(q[3])] for q in strings]); SP = (SV - c0) @ PCA.T
        top = SV[SP[:, up].argmax()]; bot = SV[SP[:, up].argmin()]
    else:
        # the limb tips: the outermost points at each end
        tt = BV[BP[:, up] > hi - 0.03 * (hi - lo)]; bb = BV[BP[:, up] < lo + 0.03 * (hi - lo)]
        top = tt.mean(0); bot = bb.mean(0)
    # basis: y along the string (nock to nock), x from the string toward the grip, z = x cross y (never mirrored)
    ey = top - bot; ey /= np.linalg.norm(ey)
    g = np.median(grip, 0) - (top + bot) / 2
    ex = g - ey * (g @ ey); ex /= np.linalg.norm(ex)
    ez = np.cross(ex, ey)
    R = np.stack([ex, ey, ez])
    sgn = 1.0
    def tr(P): return P @ R.T
    nt, nb = tr(top[None])[0], tr(bot[None])[0]
    half = (nt[1] - nb[1]) / 2
    allR = tr(BV)
    S = min(STRING_Y / half, 74.0 / max(abs(allR[:, 1] - (nt[1] + nb[1]) / 2).max(), 1e-6))
    cy = (nt[1] + nb[1]) / 2; cz = np.median(tr(grip)[:, 2])
    def place(P):
        Q = tr(P); Q[:, 1] -= cy; Q[:, 2] -= cz; Q *= S
        return Q
    gx = np.median(place(grip)[:, 0]); sx = (place(top[None])[0, 0] + place(bot[None])[0, 0]) / 2
    k = np.clip((GRIP_X - STRING_X) / max(gx - sx, 1e-3), 0.55, 1.6)
    def final(P):
        Q = place(P); Q[:, 0] = GRIP_X + (Q[:, 0] - gx) * k
        return Q
    nockT, nockB = final(top[None])[0], final(bot[None])[0]
    print(f'{bid}: y={np.round(ey, 2)} x={np.round(ex, 2)} scale {S:.4f} depth x{k:.2f} nocks y {nockB[1]:.1f}..{nockT[1]:.1f} x {nockT[0]:.1f}')
    # shapes: one per material (split to stay under 65k vertices)
    WORK.mkdir(parents=True, exist_ok=True)
    lines = [f'template {TEMPLATE}', f'out {OUT / "Meshes/CurseOfAlduin/bow" / (bid + ".nif")}']
    texcache = {}
    ec = ','.join(f'{c:.3f}' for c in glow)
    flat = oldbows.save_dds(Image.new('RGBA', (32, 32), (128, 128, 255, 90)), 'flat_n.dds', True)
    def maps(mat, role):
        key = (mat, role)
        if key in texcache: return texcache[key]
        if cfg.get('matcolors', {}).get(mat):
            col, gl = cfg['matcolors'][mat]
            dmap, nmap = oldbows.lacquer(col, size=512, seed=len(mat))
            na = np.asarray(nmap).copy(); na[..., 3] = 150 if gl == 0 else 200
            ga = np.asarray(dmap, np.float32) / 255 * gcol * (1.0 if role != 'glow' else 1.4)
            r = (tex_dds(dmap, f'{bid}/{bid}_{mat}_d.dds'), tex_dds(Image.fromarray(na, 'RGBA'), f'{bid}/{bid}_{mat}_n.dds', True),
                 tex_dds(Image.fromarray(ga.clip(0, 255).astype(np.uint8)), f'{bid}/{bid}_{mat}_g.dds'), max(gl, 2.0 if role == 'glow' else 0.0) * DIM, 0.6)
            texcache[key] = r; return r
        if mat in cfg.get('trim_mats', ()) or role == 'trim':  # (Noah: the flame-leaf blades were blinding - forged metal now, fire only in its veins)
            dmap, nmap, gmap = bowart.trim_maps(row['element'], glow, seed=len(bid) + 7)
            r = (tex_dds(dmap, f'{bid}/{bid}_{mat}_d.dds'), tex_dds(nmap, f'{bid}/{bid}_{mat}_n.dds', True), tex_dds(gmap, f'{bid}/{bid}_{mat}_g.dds'), 2.0 * DIM, 0.7)
            texcache[key] = r; return r
        if cfg.get('runic'):
            kind = row['body']
            dmap, nmap, gmap = bowart.body_maps(kind, tuple(row['bodyColor']), glow, seed=len(bid), element=row['element'])
            r = (tex_dds(dmap, f'{bid}/{bid}_d.dds'), tex_dds(nmap, f'{bid}/{bid}_n.dds', True), tex_dds(gmap, f'{bid}/{bid}_g.dds'), 3.0 * DIM, 0.6)
            texcache[key] = r; return r
        spec = cfg['mats'].get(mat)
        if not spec:
            spec = (None, None, 'self', 0.6)
        dn, nn, gn, sp = spec[:4]; rough = spec[4] if len(spec) > 4 else None
        tag = f'{bid}/{bid}_{mat}'
        if dn:
            dimg = Image.open(d / 'tex' / dn).convert('RGB')
        else:
            dimg = Image.new('RGB', (64, 64), tuple(int(c) for c in gcol))
        dimg = dimg.resize((min(2048, dimg.size[0]),) * 2, Image.LANCZOS)
        da = np.asarray(dimg, np.float32)
        if cfg.get('tint'):
            lum = da.mean(2, keepdims=True) / 255
            da = da * (1 - cfg['tint_amt']) + np.array(cfg['tint'], np.float32) * (0.35 + 1.3 * lum) * cfg['tint_amt']
        if mat == 'm0' and bid == 'stormreach':     # the holographic sheen leans to the storm's violet
            da = da * 0.55 + gcol * 0.45
        # normal + specular (gloss from 1 - roughness)
        if nn:
            nimg = Image.open(d / 'tex' / nn).convert('RGB').resize(dimg.size, Image.LANCZOS)
            na = np.asarray(nimg, np.uint8).copy()
            if cfg.get('dx_normal'): na[..., 1] = 255 - na[..., 1]
        else:
            na = np.zeros(da.shape, np.uint8); na[...] = (128, 128, 255)
        if role != 'glow' and gn != 'self':
            da, na = detail(da, na, seed=len(tag))
        D = tex_dds(Image.fromarray(da.clip(0, 255).astype(np.uint8)), f'{tag}_d.dds')
        if rough:
            ra = np.asarray(Image.open(d / 'tex' / rough).convert('L').resize(dimg.size, Image.LANCZOS), np.float32) / 255
            sa = ((1 - ra) * 230 + 15)
        else:
            sa = np.full(da.shape[:2], 150.0 * sp + 40)
        N = tex_dds(Image.fromarray(np.dstack([na, sa.clip(0, 255).astype(np.uint8)]), 'RGBA'), f'{tag}_n.dds', True)
        # glow
        if gn == 'self' or role == 'glow':
            g = np.full(da.shape, 1.0, np.float32) * gcol
            mult = 2.4 * DIM
        elif gn == 'bright':
            lum = da.mean(2) / 255
            m = np.clip((lum - 0.62) / 0.3, 0, 1) ** 1.5
            g = m[..., None] * gcol; mult = 1.2 * DIM
        elif gn:
            em = np.asarray(Image.open(d / 'tex' / gn).convert('L').resize(dimg.size, Image.LANCZOS), np.float32) / 255
            em = np.maximum(em, np.asarray(Image.fromarray((em * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(6)), np.float32) / 255 * 0.6)
            g = em[..., None] * gcol; mult = 2.6 * DIM
        else:
            g = np.zeros(da.shape, np.float32); mult = 0.0
        G = tex_dds(Image.fromarray(g.clip(0, 255).astype(np.uint8)), f'{tag}_g.dds')
        r = (D, N, G, mult, sp); texcache[key] = r
        return r
    total = 0
    for q in body:
        role, V, T, F, TF, M, stem = q
        P = final(V)
        for mat in np.unique(M):
            sel = M == mat
            GF, GT = F[sel], TF[sel]
            D, N, G, mult, sp = maps(mat, role)
            # corners: (vertex, uv) pairs, so UV seams stay
            if cfg.get('runic') or T is None:
                # our own limb UVs (u along the bow, v round it), seams split
                cen = {}
                yb = np.round(P[:, 1] / 4).astype(int)
                for b in np.unique(yb[np.unique(GF)]):
                    cen[b] = np.median(P[yb == b, 0])
                uu, vv = oldbows.limb_uv(P, cen, np.arange(len(P)))
                keyuv = None
            # the triangles' corners
            corner = {}; tris = []
            for fi in range(len(GF)):
                tri = []
                if cfg.get('runic') or T is None:
                    vs = vv[GF[fi]]; wrap = vs.max() - vs.min() > 0.5
                for j in range(3):
                    vi = GF[fi, j]
                    if cfg.get('runic') or T is None:
                        key = (vi, bool(wrap and vv[vi] < 0.5))
                    else:
                        key = (vi, GT[fi, j])
                    if key not in corner: corner[key] = len(corner)
                    tri.append(corner[key])
                tris.append(tri)
            tris = np.array(tris)
            keys = list(corner.keys())
            vidx = np.array([kk[0] for kk in keys])
            if cfg.get('runic') or T is None:
                UV = np.stack([uu[vidx], vv[vidx] + np.array([1.0 if kk[1] else 0.0 for kk in keys])], 1)
            else:
                tuv = T[np.array([kk[1] for kk in keys])]
                UV = np.stack([tuv[:, 0], 1.0 - tuv[:, 1]], 1)
            PP = P[vidx]
            # normals: smooth over the welded position, so hard edges stay only where the source split them
            fn = np.cross(PP[tris[:, 1]] - PP[tris[:, 0]], PP[tris[:, 2]] - PP[tris[:, 0]])
            NN = np.zeros_like(PP)
            for j in range(3): np.add.at(NN, tris[:, j], fn)
            NN /= np.linalg.norm(NN, axis=1, keepdims=True) + 1e-12
            # split into shapes of < 60000 vertices
            start = 0; order = np.arange(len(tris)); chunk = 0
            while start < len(tris):
                used = {}; cur = []
                i = start
                while i < len(tris) and len(used) < 59990:
                    for a in tris[i]:
                        if a not in used: used[a] = len(used)
                    cur.append([used[a] for a in tris[i]]); i += 1
                start = i
                name = f'{bid}_{stem}_{mat}_{chunk}'.replace('.', '_'); chunk += 1
                lit = role == 'glow'
                lines.append(f'shape {name} diffuse={D} normal={N} glow={G} emit={ec} emitmult={mult if not lit else 2.4 * DIM:.2f} spec={sp:.2f} gloss={80 if sp > 0.6 else 40}')
                inv = np.empty(len(used), int)
                for a, b in used.items(): inv[b] = a
                for a in inv:
                    x, y, z = PP[a]; nx, ny, nz = NN[a]; u, v = UV[a]
                    lines.append(f'v {x:.4f} {y:.4f} {z:.4f} {u:.5f} {v:.5f} {nx:.4f} {ny:.4f} {nz:.4f} ' + ' '.join(f'{b} {wt:.4f}' for b, wt in oldbows.limb_weights(y)))
                lines += [f't {a} {b} {c}' for a, b, c in cur]
                total += len(cur)
    # the string: a thin waxed cord from nock to nock on the string bones, a faint thread of the element in it
    cord = oldbows.save_dds(Image.new('RGB', (32, 32), (46, 40, 36)), 'cord_d.dds')
    gg = oldbows.save_dds(Image.new('RGB', (32, 32), tuple(int(min(255, c + 60)) for c in gcol)), f'{bid}_glow_g.dds')
    lines.append(f'shape {bid}_string diffuse={cord} normal={flat} glow={gg} emit={ec} emitmult=0.35 spec=0.3 gloss=30')
    R0, SEG, SIDES = 0.24, 24, 6
    sv = []
    for s in range(SEG + 1):
        t = s / SEG; c = nockB * (1 - t) + nockT * t
        for j in range(SIDES):
            a = 2 * math.pi * j / SIDES
            nx, nz = math.cos(a), math.sin(a)
            sv.append((c[0] + R0 * nx, c[1], c[2] + R0 * nz, j / SIDES, t, nx, 0.0, nz))
    for (x, y, z, u, v, nx, ny, nz) in sv:
        lines.append(f'v {x:.4f} {y:.4f} {z:.4f} {u:.4f} {v:.4f} {nx:.4f} {ny:.4f} {nz:.4f} {SB1 if y < 0 else SB2} 1.0')
    for s in range(SEG):
        for j in range(SIDES):
            a = s * SIDES + j; b = s * SIDES + (j + 1) % SIDES; c2 = a + SIDES; d2 = b + SIDES
            lines += [f't {a} {c2} {b}', f't {b} {c2} {d2}']
    (OUT / 'Meshes/CurseOfAlduin/bow').mkdir(parents=True, exist_ok=True)
    jf = WORK / f'{bid}.job'; jf.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([CONV, str(jf)], capture_output=True, text=True)
    print('  ', (r.stdout.strip().split('\n')[-1] if r.stdout else ''), r.stderr.strip()[-300:])
    if r.returncode: raise SystemExit(f'bowconv failed for {bid}')
    # (Noah: the wings were too much and didn't fit the bow) the element coursing through the bow itself: the Spectral
    # Bow's flowing flame shader on a skin a hair outside every part, recoloured to the element, flowing along its length
    pal = palette(bid, glow)
    nif = OUT / 'Meshes/CurseOfAlduin/bow' / (bid + '.nif')
    r = subprocess.run(['/home/claude/tools/fxshell', str(nif), SPECTRAL, '6', str(nif), pal, '1', '1', '1', f"{cfg.get('shell', 0.9) * DIM:.3f}", '0.12', '0.025', '0.08'], capture_output=True, text=True)
    print('  ', r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit(f'fxshell failed for {bid}')
    print(f'   {total} triangles')


def palette(bid, glow):
    """the flame's colour ramp (greyscale to palette): the Spectral Bow's own fire ramp's alpha, our element's colours -
    deep at the bottom, the element in the middle, nearly white at the hot top"""
    W, H = 256, 64
    x = np.linspace(0, 1, W)
    g = np.array(glow, np.float32)
    deep = g * 0.35
    hot = g * 0.4 + 0.6
    col = np.where(x[:, None] < 0.6, deep + (g - deep) * (x[:, None] / 0.6), g + (hot - g) * ((x[:, None] - 0.6) / 0.4))
    alpha = np.clip((x - 0.06) / 0.94, 0, 1) ** 0.8
    alpha[x < 0.06] = 0
    row = np.hstack([col * 255, alpha[:, None] * 255])
    img = np.repeat(row[None], H, 0).clip(0, 255).astype(np.uint8)
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'bow' / f'{bid}_flamegrad.dds'
    tmp = WORK / f'{bid}_flamegrad.png'; Image.fromarray(img, 'RGBA').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', 'dds:compression=none', '-define', 'dds:mipmaps=1', str(p)], check=True)
    return f'textures\\CurseOfAlduin\\bow\\{bid}_flamegrad.dds'


if __name__ == '__main__':
    for b in (sys.argv[1:] or MODELS):
        build(b)
