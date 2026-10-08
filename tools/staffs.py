"""Undying Nights staffs: eight elemental staffs built from the four staffs in the 'fantasy magic staffs collection'
(Latido de Afrodita, Ojos del vacio, Semilla Ancestral, Sexto cielo). Each base model becomes two staffs with their own
palette, glow and a small element add-on (halo, accretion disk, soul wisps...).

    python tools/staffs.py <staffs dir from objexp> <out Data dir>

The staff dir holds <base>.obj (world space, metres, Y up), tex<N>.* and mats.txt as written by tools/objexp.
Output: Meshes/UndyingNights/staff/<id>.nif (Prn WeaponStaff, grip at the origin, head toward +Y) and textures.
Writes tools/staff_tips.json: the head centre of each staff in model units (bolts leave from there)."""
import json, math, pathlib, subprocess, sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import skymat
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
SRC = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/home/claude/work/staffs')
OUT = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / 'build' / 'assets' / 'Data')
CONV = '/home/claude/tools/modelconv'
LENGTH = 132.0          # Skyrim units, foot to crown (vanilla staves run 115-125)
GRIP = 0.40             # grip point, share of the height from the foot
BUDGET = 9000           # triangles per material group at most

# material roles per base model (material index in mats.txt -> role)
SRC2 = pathlib.Path('/home/claude/work/staffs2')   # 'magic staffs basic bundle' (objexp of MagicalStaffsSet1.fbx)
BASES = {
    'heart': ('Latido_de_Adrodita', {4: 'particle', 5: 'accent', 6: 'wood', 7: 'gem'}),
    'void': ('Ojos_del_vacio', {8: 'metal', 9: 'gem', 10: 'dark'}),
    'seed': ('Semilla_Ancestral', {11: 'leaf', 12: 'leaf', 13: 'gem', 14: 'wood', 15: 'accent', 16: 'leaf', 17: 'leaf'}),
    'heaven': ('Sexto_cielo.003', {18: 'gem', 19: 'wood', 20: 'wings', 21: 'particle'}),
    'oak': ('Oak_Staff', {13: 'wood', 14: 'band', 15: 'wrap', 16: 'gem'}, SRC2),
    'spire': ('Staff3', {2: 'shaft', 3: 'band', 4: 'coil', 5: 'gem'}, SRC2),
    'crookgem': ('Staff2', {2: 'band', 5: 'gem', 6: 'wood', 7: 'wood', 8: 'wood', 9: 'leaf'}, SRC2),
    'crook': ('Staff1', {10: 'wood', 11: 'wood', 12: 'wood', 3: 'band'}, SRC2),
    'vine': ('Staff', {0: 'wood', 1: 'leaf'}, SRC2),
}

# id -> (base, roles colour table, glow colour, add-on)
# colours are RGB 0-255; 'tint' roles multiply the source texture, others are flat
# role -> ('tex', tint): the model's own texture recoloured; (kind, tint): a skymat material; RGB: a glowing part
STAFFS = {
    'sidearm':    ('heaven', {'wood': ('tex', (96, 78, 118)), 'wings': ('silver', (186, 188, 200)), 'gem': (170, 90, 255), 'particle': (200, 150, 255)}, (0.67, 0.35, 1.0), 'runes'),
    'aetherium':  ('heaven', {'wood': ('tex', (230, 214, 182)), 'wings': ('gold', (200, 160, 78)), 'gem': (255, 240, 196), 'particle': (255, 226, 150)}, (1.0, 0.85, 0.45), 'halo'),
    'mammothlmg': ('void', {'dark': ('iron', (40, 42, 50)), 'metal': ('steel', (128, 134, 146)), 'gem': (60, 240, 230)}, (0.25, 0.95, 0.9), 'disk'),
    'triad':      ('heart', {'accent': ('silver', (150, 178, 196)), 'wood': ('wood_dark', (46, 50, 62)), 'gem': (130, 255, 255), 'particle': (170, 255, 255)}, (0.45, 1.0, 1.0), 'wisps'),
    'animunculi': ('oak', {'wood': ('wood_dark', (46, 30, 40)), 'band': ('bronze', None), 'wrap': ('leather', (66, 38, 76)), 'gem': (205, 60, 255)}, (0.8, 0.25, 1.0), 'hexring'),
    'longshot':   ('spire', {'shaft': ('steel', (118, 124, 140)), 'band': ('gold', None), 'coil': (120, 180, 255), 'gem': (210, 235, 255)}, (0.55, 0.75, 1.0), 'stars'),
    'centurion':  ('crookgem', {'wood': ('wood_dark', (60, 64, 36)), 'band': ('iron', (74, 76, 62)), 'gem': (130, 255, 60), 'leaf': ('vines', (64, 84, 32))}, (0.5, 1.0, 0.2), 'drops'),
    'kagrenac':   ('crook', {'wood': ('wood', (86, 60, 74)), 'band': ('silver', None)}, (1.0, 0.35, 0.8), 'mindeye'),
    # city relics (rewards of the Undying Dirge): grander finishes, two add-ons each
    'hearth':     ('seed', {'wood': ('wood', (112, 72, 44)), 'accent': ('gold', None), 'leaf': ('tex', (190, 80, 34)), 'gem': (255, 130, 40)}, (1.0, 0.5, 0.15), ('wisps', 'halo')),
    'gildergreen': ('vine', {'wood': ('tex', (170, 140, 96)), 'leaf': ('vines', (96, 160, 64))}, (0.65, 1.0, 0.4), ('spores', 'halo')),
    'arkay':      ('heaven', {'wood': ('bone', (226, 220, 204)), 'wings': ('silver', (212, 214, 228)), 'gem': (236, 232, 255), 'particle': (222, 216, 255)}, (0.9, 0.9, 1.0), ('halo', 'runes')),
    'mirestaff':  ('void', {'dark': ('wood_dark', (32, 42, 26)), 'metal': ('bronze', (96, 112, 62)), 'gem': (130, 222, 60)}, (0.5, 0.9, 0.25), ('disk', 'drops')),
}
GLOWING = {'gem', 'particle', 'coil'}
TEXTURED = {'wood', 'leaf', 'wings', 'accent', 'metal', 'dark'}

def load_mats(src=None):
    SRC_ = src or SRC
    m = {}
    for line in open(SRC_ / 'mats.txt'):
        p = line.split(); i = int(p[0]); tex = nrm = None; col = (1, 1, 1)
        for q in p[2:]:
            if q.startswith(('1=*', '12=*')): tex = next(SRC_.glob(f'tex{q.split("*")[1]}.*'))
            if q.startswith('6=*'): nrm = next(SRC_.glob(f'tex{q.split("*")[1]}.*'))
            if q.startswith('col='): col = tuple(float(v) for v in q[4:].split(','))
        m[i] = (tex, nrm, col)
    return m

def load_obj(path):
    V, T, groups = [], [], {}
    mat = 0
    for l in open(path):
        if l.startswith('v '): V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt '): T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('usemtl'): mat = int(l.split()[1][1:])
        elif l.startswith('f '): groups.setdefault(mat, []).append([int(x.split('/')[0]) - 1 for x in l.split()[1:4]])
    return np.array(V), np.array(T) if T else np.zeros((len(V), 2)), {k: np.array(v) for k, v in groups.items()}

def compact(V, T, F):
    used = np.unique(F); remap = -np.ones(len(V), int); remap[used] = np.arange(len(used))
    return V[used], T[used], remap[F]

def decimate(V, T, F, budget):
    """vertex clustering: snap to a grid, merge, drop collapsed triangles; grid refined until under budget"""
    if len(F) <= budget:
        return V, T, F
    diag = np.linalg.norm(V.max(0) - V.min(0))
    best = (V, T, F)
    for div in (400, 300, 220, 160, 120, 90, 70, 50):
        cell = diag / div
        key = np.floor((V - V.min(0)) / cell).astype(np.int64)
        k = key[:, 0] * 73856093 ^ key[:, 1] * 19349663 ^ key[:, 2] * 83492791
        uniq, inv = np.unique(k, return_inverse=True)
        cnt = np.bincount(inv)
        NV = np.stack([np.bincount(inv, V[:, j]) / cnt for j in range(3)], 1)
        NT = np.stack([np.bincount(inv, T[:, j]) / cnt for j in range(2)], 1)
        NF = inv[F]
        ok = (NF[:, 0] != NF[:, 1]) & (NF[:, 1] != NF[:, 2]) & (NF[:, 0] != NF[:, 2])
        NF = NF[ok]
        best = (NV, NT, NF)
        if len(NF) <= budget:
            break
    return compact(*best)

def save_dds(im, rel, alpha=False, size=None):
    if size:
        im = im.resize((size, size), Image.LANCZOS)
    p = OUT / 'Textures' / 'UndyingNights' / 'staff' / rel; p.parent.mkdir(parents=True, exist_ok=True)
    im.convert('RGBA' if alpha else 'RGB').save(p, pixel_format='DXT5' if alpha else 'DXT1')
    return f'UndyingNights\\staff\\{rel}'

def tinted(tex, col, keep=0.35):
    """recolour a texture toward col, keeping its light/dark detail"""
    im = np.asarray(Image.open(tex).convert('RGB').resize((1024, 1024), Image.LANCZOS), np.float32) / 255
    lum = im @ np.array([0.3, 0.59, 0.11], np.float32)
    lum = lum / max(1e-3, lum.mean())
    c = np.array(col, np.float32) / 255
    out = np.clip(c[None, None, :] * lum[..., None] * (1 - keep) + im * keep * c[None, None, :] * 1.6, 0, 1)
    return Image.fromarray((out * 255).astype(np.uint8), 'RGB')

def solid(col, size=64, noise=0.08):
    rng = np.random.default_rng(int(sum(col)))
    n = rng.random((size, size)).astype(np.float32)
    n = np.asarray(Image.fromarray((n * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5)), np.float32) / 255
    a = np.clip(np.array(col, np.float32)[None, None, :] * (1 - noise + noise * 2 * n[..., None]), 0, 255)
    return Image.fromarray(a.astype(np.uint8), 'RGB')

# ---- add-ons (built in final staff space: Y up along the staff, units = game units)
def ring(cx, cy, cz, r0, r1, axis='y', seg=72, tilt=0.0):
    V, T, F = [], [], []
    for i in range(seg + 1):
        a = 2 * math.pi * i / seg
        for r, v in ((r0, 0), (r1, 1)):
            x, z = r * math.cos(a), r * math.sin(a)
            if axis == 'z':      # ring facing forward (in the X-Y plane)
                p = (cx + x, cy + z * math.cos(tilt), cz + z * math.sin(tilt))
            else:                # ring around the staff (in the X-Z plane)
                p = (cx + x, cy + z * math.sin(tilt), cz + z * math.cos(tilt))
            V.append(p); T.append((i / seg * 6, v))
    for i in range(seg):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        F += [(a, b, d), (a, d, c), (a, d, b), (a, c, d)]  # both sides
    return np.array(V), np.array(T), np.array(F)

def crystal(cx, cy, cz, h, r, sides=6, rot=0.0, tilt=(0, 0)):
    V = [(0, 0, 0), (0, h, 0)]; T = [(0.5, 0), (0.5, 1)]; F = []
    for i in range(sides):
        a = 2 * math.pi * i / sides + rot
        V.append((r * math.cos(a), h * 0.4, r * math.sin(a))); T.append((i / sides, 0.4))
    for i in range(sides):
        a, b = 2 + i, 2 + (i + 1) % sides
        F += [(0, a, b), (1, b, a)]
    V = np.array(V, float)
    tx, tz = tilt
    Rx = np.array([[1, 0, 0], [0, math.cos(tx), -math.sin(tx)], [0, math.sin(tx), math.cos(tx)]])
    Rz = np.array([[math.cos(tz), -math.sin(tz), 0], [math.sin(tz), math.cos(tz), 0], [0, 0, 1]])
    V = V @ (Rz @ Rx).T + np.array([cx, cy, cz])
    return V, np.array(T, float), np.array(F)

def sphere(cx, cy, cz, r, seg=16):
    V, T, F = [], [], []
    for i in range(seg + 1):
        th = math.pi * i / seg
        for j in range(seg + 1):
            ph = 2 * math.pi * j / seg
            V.append((cx + r * math.sin(th) * math.cos(ph), cy + r * math.cos(th), cz + r * math.sin(th) * math.sin(ph))); T.append((j / seg, i / seg))
    for i in range(seg):
        for j in range(seg):
            a = i * (seg + 1) + j; b = a + seg + 1
            F += [(a, b, a + 1), (a + 1, b, b + 1)]
    return np.array(V), np.array(T), np.array(F)

def merge(parts):
    V, T, F = [], [], []; o = 0
    for v, t, f in parts:
        V.append(v); T.append(t); F.append(f + o); o += len(v)
    return np.concatenate(V), np.concatenate(T), np.concatenate(F)

def addon(kind, head, top, rng):
    hx, hy, hz = head
    if kind == 'halo':
        return {'glow_add': merge([ring(0, top + 9, 0, 9, 11, tilt=0.25), ring(0, hy, 0, 15.5, 16.5, axis='z')])}
    if kind == 'runes':
        return {'glow_add': merge([ring(0, hy - 13, 0, 3.6, 4.4), ring(0, hy - 17, 0, 3.0, 3.6)])}
    if kind == 'disk':   # accretion disk around the void crescent, and a dark core
        return {'glow_add': merge([ring(0, hy, 0, 9, 17, axis='z', tilt=0.35), ring(0, hy, 0, 18, 19.5, axis='z', tilt=0.35)]),
                'core': sphere(0, hy, 0, 4.2)}
    if kind == 'shards':
        parts = [crystal(math.cos(a) * 9, hy + math.sin(a) * 9, 0, 6, 1.4, rot=a, tilt=(0, a - math.pi / 2)) for a in np.linspace(0, 2 * math.pi, 7)[:-1]]
        return {'glow': merge(parts)}
    if kind == 'wisps':
        parts = [sphere(math.cos(a) * 8, hy + 6 + math.sin(a * 2) * 4, math.sin(a) * 8, 1.3, 8) for a in np.linspace(0, 2 * math.pi, 6)[:-1]]
        return {'glow_add': merge(parts)}
    if kind == 'hexring':  # hex sigils turning around the crystal
        parts = [ring(0, hy + 2, 0, 9.5, 10.5, axis='z', tilt=0.2), ring(0, hy + 2, 0, 12.5, 13.0, tilt=0.5)]
        parts += [crystal(math.cos(a) * 11, hy + 2 + math.sin(a) * 11, 0, 2.4, 0.9, sides=3, rot=a) for a in np.linspace(0, 2 * math.pi, 7)[:-1]]
        return {'glow_add': merge(parts)}
    if kind == 'stars':    # little stars caught in the spire's orbit
        parts = [crystal(math.cos(a) * 8, hy + 6 + 3 * math.sin(2 * a), math.sin(a) * 8, 2.2, 0.8, sides=4, rot=a) for a in np.linspace(0, 2 * math.pi, 6)[:-1]]
        return {'glow_add': merge(parts + [ring(0, hy + 6, 0, 7.5, 8.0, tilt=0.3)])}
    if kind == 'mindeye':  # a glowing eye hanging in the crook, ringed by a turning halo
        return {'glow': sphere(0, top - 9, 0, 3.4, 18), 'glow_add': merge([ring(0, top - 9, 0, 5.2, 5.8, axis='z', tilt=0.4), ring(0, top - 9, 0, 6.6, 7.0, tilt=1.1)])}
    if kind == 'drops':
        parts = [crystal(rng.uniform(-3, 3), hy - 14 - k * 9, rng.uniform(-3, 3), -4.5, 1.1, 5) for k in range(4)]
        return {'glow': merge(parts)}
    if kind == 'spores':
        parts = [sphere(rng.uniform(-7, 7), hy + rng.uniform(-6, 10), rng.uniform(-7, 7), rng.uniform(0.7, 1.4), 6) for _ in range(9)]
        return {'glow_add': merge(parts)}
    return {}

def build(sid):
    base, cols, glow, extra = STAFFS[sid]
    objname, roles = BASES[base][:2]
    src = BASES[base][2] if len(BASES[base]) > 2 else SRC
    mats = load_mats(src)
    V, T, groups = load_obj(src / f'{objname}.obj')
    allF = np.concatenate(list(groups.values()))
    used = np.unique(allF)
    lo, hi = V[used].min(0), V[used].max(0)
    height = hi[1] - lo[1]
    S = LENGTH / height
    # centre the shaft: x/z median of the lower half
    low = V[used][V[used][:, 1] < lo[1] + height * 0.5]
    cx, cz = np.median(low[:, 0]), np.median(low[:, 2])
    grip = lo[1] + height * GRIP
    P = np.stack([(V[:, 0] - cx) * S, (V[:, 1] - grip) * S, (V[:, 2] - cz) * S], 1)
    top = (hi[1] - grip) * S
    head = (0.0, top - 0.13 * LENGTH, 0.0)
    rng = np.random.default_rng(len(sid))
    out_groups = {}
    for mi, F in groups.items():
        role = roles.get(mi, 'wood')
        cen = P[F].mean(1)
        F = F[(np.abs(cen[:, 0]) < 28) & (np.abs(cen[:, 2]) < 28)]   # stray particles far off the staff
        if not len(F):
            continue
        v, t, f = compact(P, T, F)
        v, t, f = decimate(v, t, f, BUDGET)
        out_groups[f'{role}_{mi}'] = (role, mi, v, t, f)
    for xi, ex in enumerate(extra if isinstance(extra, tuple) else (extra,)):
        for name, (v, t, f) in addon(ex, head, top, rng).items():
            out_groups[f'{name}_x{xi}'] = (name, -1, v, t, f)
    # ---- textures and job
    work = OUT.parent / 'staffs' / sid; work.mkdir(parents=True, exist_ok=True)
    obj = work / f'{sid}.obj'
    lines = [f'mtllib {sid}.mtl']; vo = 1; mtl = []
    flat_n = save_dds(Image.new('RGBA', (64, 64), (128, 128, 255, 140)), 'flat_n.dds', alpha=True)
    glow_hex = tuple(int(c * 255) for c in glow)
    job_mats = []
    for gname, (role, mi, v, t, f) in out_groups.items():
        lines += [f'o {gname}', f'usemtl {gname}']
        lines += [f'v {x:.4f} {y:.4f} {z:.4f}' for x, y, z in v]
        lines += [f'vt {a:.5f} {b:.5f}' for a, b in t]
        lines += [f'f {a+vo}/{a+vo} {b+vo}/{b+vo} {c+vo}/{c+vo}' for a, b, c in f]
        vo += len(v)
        mtl.append(f'newmtl {gname}\nKd 1 1 1\n')
        tex, nrm, _ = mats.get(mi, (None, None, None))
        if role in ('glow', 'glow_add', 'core') or role in GLOWING:
            col = cols.get(role, glow_hex) if role in GLOWING else glow_hex
            if role == 'core':
                d = save_dds(solid((4, 4, 8)), f'{sid}_core.dds')
                job_mats.append(f'mat {gname} diffuse={d} normal={flat_n} spec=1.5 gloss=200')
                continue
            d = save_dds(solid(col, noise=0.0), f'{sid}_{gname}_d.dds')
            g = save_dds(solid(tuple(min(255, int(c * 1.0 + 40)) for c in col), noise=0.0), f'{sid}_{gname}_g.dds')
            ec = ','.join(f'{c / 255:.3f}' for c in col)
            mult = 3.0 if role == 'gem' else 2.2
            alpha = ' alpha=add' if role in ('glow_add', 'particle') else ''
            job_mats.append(f'mat {gname} diffuse={d} normal={flat_n} glow={g} emit={ec} emitmult={mult} spec=1.2 gloss=120 doublesided=1{alpha}')
        else:
            spec_ = cols.get(role, ('wood', None))
            kind, tint = spec_ if isinstance(spec_[0], str) else ('tex', spec_)
            if kind == 'tex' and tex is not None:
                im = tinted(tex, tint)
                d = save_dds(im, f'{sid}_{gname}_d.dds', size=1024)
                n = save_dds(Image.open(nrm).convert('RGBA'), f'{base}_{mi}_n.dds', alpha=True, size=1024) if nrm is not None else \
                    save_dds(skymat.make('wood', tint, seed=mi)[1], f'{sid}_{gname}_n.dds', alpha=True)
                spec, gloss = 0.4, 40
            else:
                kind = 'wood' if kind == 'tex' else kind
                dif, nm = skymat.make(kind, tint, seed=len(sid) * 31 + max(mi, 0))
                d = save_dds(dif, f'{sid}_{gname}_d.dds'); n = save_dds(nm, f'{sid}_{gname}_n.dds', alpha=True)
                spec, gloss = {'iron': (1.2, 60), 'steel': (1.6, 90), 'bronze': (1.4, 70), 'gold': (1.8, 90), 'silver': (1.8, 100),
                               'leather': (0.4, 30), 'wood': (0.4, 30), 'wood_dark': (0.45, 35), 'vines': (0.3, 25), 'bone': (0.5, 30)}.get(kind, (0.5, 40))
            job_mats.append(f'mat {gname} diffuse={d} normal={n} spec={spec} gloss={gloss} doublesided=1')
    obj.write_text('\n'.join(lines) + '\n')
    (work / f'{sid}.mtl').write_text(''.join(mtl))
    nif = OUT / 'Meshes' / 'UndyingNights' / 'staff' / f'{sid}.nif'; nif.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {nif}', 'origin 1', 'prn WeaponStaff', *job_mats]
    jf = work / f'{sid}.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([CONV, str(jf)], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); raise SystemExit(f'modelconv failed for {sid}')
    tris = sum(len(g[4]) for g in out_groups.values())
    print(f'{sid}: {base} {tris} tris, head y {head[1]:.1f}, top {top:.1f}')
    return head[1]

if __name__ == '__main__':
    tips = {sid: round(build(sid), 1) for sid in STAFFS}
    (ROOT / 'tools' / 'staff_tips.json').write_text(json.dumps(tips, indent=1))
