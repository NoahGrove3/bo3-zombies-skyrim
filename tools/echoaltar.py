"""The new Echo Altar (Noah's download, 'runestone_lp': a round stepped platform with glowing rune rings, six old
columns round it and a standing stone floating over its heart), with:
    shrine/shrine.nif          the platform and the columns (walkable, collision), the stone taken out
    shrine/heartstone.nif      the floating stone at its heart, re-skinned as a rune stone: grey rock cut with rows of
                               runes that glow blue (in the style of Noah's other download, the low-poly rune stone)
    shrine/heartstone_lit.nif  the same stone once the city's curse is broken: blue through and through, glowing
    shrine/pillarrock.nif      Noah's rune stone, small, floating over each column (the dressing sheet places them)
    shrine/risemotes.nif       blue motes rising from the platform's heart into the stone (scrolling, unlit)
    shrine/risemotes_lit.nif   ... thicker and brighter, with a halo of light round the stone, once freed
Sources (not in git): assets/models/echoaltar/altar (runestone_lp.fbx + textures), assets/models/echoaltar/runestone.
Origin = 46 under the ground line (src/Shops.cpp kAltarSink: the altar's own origin under the stand's floor line,
which is 18 over the ground): the platform's foot is set 36 over its origin, so it sinks 10 into the ground.
    python tools/echoaltar.py            (writes into assets/fx: Meshes/ and Textures/CurseOfAlduin/shrine)"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
SRC = ROOT / 'assets' / 'models' / 'echoaltar'
OUT = ROOT / 'assets' / 'fx'
WORK = ROOT / 'build' / 'echoaltar'
WORK.mkdir(parents=True, exist_ok=True)
TOOLS = pathlib.Path('/home/claude/tools')
S = 0.6           # the download is ~700 across: the stand's altar is ~420
LIFT = 36.0       # the platform's foot over the model's origin
TEX = 'CurseOfAlduin\\shrine\\'


def load(p, size=None, mode='RGB'):
    im = Image.open(p).convert(mode)
    return im.resize((size, size), Image.LANCZOS) if size and im.size[0] != size else im


def dds(im, name, alpha=False):
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'shrine' / name
    p.parent.mkdir(parents=True, exist_ok=True)
    im.convert('RGBA' if alpha else 'RGB').save(p, pixel_format='DXT5' if alpha else 'DXT1')
    return TEX + name


def normal(npath, rpath=None, size=1024, flip_green=True):
    n = np.asarray(load(npath, size), np.uint8).copy()
    if flip_green:
        n[..., 1] = 255 - n[..., 1]   # (OpenGL normal maps: Skyrim's are DirectX)
    spec = 255 - np.asarray(load(rpath, size, 'L')) if rpath else np.full(n.shape[:2], 50, np.uint8)
    return Image.fromarray(np.dstack([n, (spec * 0.5).astype(np.uint8)]), 'RGBA')


def run(tool, lines, name):
    jf = WORK / f'{name}.job'
    jf.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([str(TOOLS / tool), str(jf)], capture_output=True, text=True)
    print(name, '|', (r.stdout.strip() or r.stderr.strip())[-300:])
    if r.returncode:
        raise SystemExit(f'{tool} failed for {name}')


def mesh(rel):
    p = OUT / 'Meshes' / 'CurseOfAlduin' / 'shrine' / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    return p


# ---------------------------------------------------------------- the altar's stone, out of the download as an OBJ
def read_obj(path):
    V, T, objs, cur = [], [], {}, None
    for l in open(path):
        if l.startswith('v '):
            V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt '):
            T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('o '):
            cur = l.split()[1]
        elif l.startswith('f '):
            objs.setdefault(cur, []).append([int(x.split('/')[0]) - 1 for x in l.split()[1:]])
    return np.array(V), objs


def stone_obj():
    """the stone (Y-up in the download) as its own OBJ, turned Z-up, its UVs laid flat over its two broad faces (and
    its thin edges stretched from them) so the rune texture reads straight on each face"""
    subprocess.run([str(TOOLS / 'objexp'), str(SRC / 'altar/source/runestone_lp.fbx'), str(WORK)], capture_output=True)
    V, objs = read_obj(WORK / 'runestone_lp.obj')
    F = objs['stone']
    used = sorted({i for f in F for i in f})
    P = V[used]
    lo, hi = P.min(0), P.max(0)
    idx = {o: n for n, o in enumerate(used)}
    lines = ['mtllib heartstone.mtl', 'usemtl heartstone']
    for x, y, z in P:
        lines.append(f'v {x:.4f} {-z:.4f} {y:.4f}')   # Y-up -> Z-up (x, -z, y)
    # per corner: front/back faces project onto x (u) and y (v); edges onto z (u) and y (v)
    uvs = []
    out_f = []
    for f in F:
        a, b, c = (V[f[0]], V[f[1]], V[f[2]])
        nz = abs(np.cross(b - a, c - a)[2]) / (np.linalg.norm(np.cross(b - a, c - a)) + 1e-9)
        corners = []
        for i in f:
            x, y, z = V[i]
            if nz > 0.4:
                u = (x - lo[0]) / (hi[0] - lo[0]); u = u * 0.5 if z > 0 else 0.5 + (1 - u) * 0.5   # front half / back half
            else:
                u = 0.5 + 0.02 * (z - lo[2]) / (hi[2] - lo[2]) - 0.01 if x > 0 else 0.0 + 0.02 * (z - lo[2]) / (hi[2] - lo[2])
            v = 1 - (y - lo[1]) / (hi[1] - lo[1])
            uvs.append((u, v))
            corners.append(f'{idx[i] + 1}/{len(uvs)}')
        out_f.append('f ' + ' '.join(corners))
    # (the stone keeps its own UVs into the platform's atlas - its real rock - and the runes are baked into that space:
    # the planar coordinates worked out above are where on the engraving each corner of each face lies)
    T = []
    for l in open(WORK / 'runestone_lp.obj'):
        if l.startswith('vt '):
            T.append([float(x) for x in l.split()[1:3]])
    tri_uv, tri_pl = [], []
    k = 0
    for l in open(WORK / 'runestone_lp.obj'):
        if l.startswith('o '):
            cur = l.split()[1]
        elif l.startswith('f ') and cur == 'stone':
            ts = [int(x.split('/')[1]) - 1 for x in l.split()[1:]]
            tri_uv.append([T[t] for t in ts]); tri_pl.append(uvs[k:k + len(ts)]); k += len(ts)
    global BAKE
    BAKE = (tri_uv, tri_pl)
    lines += [f'vt {T[t][0]:.5f} {T[t][1]:.5f}' for f_uv in tri_uv for t in range(0)]  # (placeholder)
    lines = lines[:2 + len(P)]
    vt_lines, f_lines, n = [], [], 0
    for f_uv, f in zip(tri_uv, F):
        cs = []
        for (u, v), i in zip(f_uv, f):
            vt_lines.append(f'vt {u:.5f} {v:.5f}'); n += 1
            cs.append(f'{idx[i] + 1}/{n}')
        f_lines.append('f ' + ' '.join(cs))
    lines += vt_lines + f_lines
    (WORK / 'heartstone.obj').write_text('\n'.join(lines) + '\n')
    (WORK / 'heartstone.mtl').write_text('newmtl heartstone\nKd 0.8 0.8 0.8\n')
    print('stone', len(used), 'verts, bounds', lo.round(1), hi.round(1))
    return lo, hi


# ---------------------------------------------------------------- textures
RUNES = [  # elder-futhark-like strokes in a 0..1 cell: (x0, y0, x1, y1) segments
    [(0.5, 0, 0.5, 1), (0.5, 0.2, 0.85, 0), (0.5, 0.45, 0.85, 0.25)],
    [(0.3, 0, 0.3, 1), (0.3, 0, 0.75, 0.3), (0.75, 0.3, 0.3, 0.6)],
    [(0.5, 0, 0.5, 1), (0.15, 0.25, 0.85, 0.75), (0.85, 0.25, 0.15, 0.75)],
    [(0.3, 0, 0.3, 1), (0.7, 0, 0.7, 1), (0.3, 0.35, 0.7, 0.65)],
    [(0.5, 0, 0.5, 1), (0.5, 0.5, 0.15, 0.15), (0.5, 0.5, 0.85, 0.15)],
    [(0.3, 0, 0.3, 1), (0.3, 0.1, 0.75, 0.35), (0.75, 0.35, 0.3, 0.55), (0.3, 0.55, 0.75, 0.8), (0.75, 0.8, 0.3, 1)],
    [(0.2, 0, 0.8, 1), (0.8, 0, 0.2, 1)],
    [(0.5, 0, 0.5, 1), (0.5, 0.3, 0.2, 0.0), (0.5, 0.3, 0.8, 0.0), (0.5, 0.7, 0.2, 1.0), (0.5, 0.7, 0.8, 1.0)],
    [(0.5, 0, 0.15, 0.5), (0.15, 0.5, 0.5, 1), (0.5, 1, 0.85, 0.5), (0.85, 0.5, 0.5, 0)],
    [(0.3, 0, 0.3, 1), (0.3, 0, 0.75, 0.25), (0.75, 0.25, 0.3, 0.5), (0.3, 0.5, 0.75, 1)],
    [(0.5, 1, 0.5, 0), (0.5, 0, 0.2, 0.3)],
    [(0.2, 0, 0.2, 1), (0.8, 0, 0.8, 1), (0.2, 0, 0.8, 1), (0.8, 0, 0.2, 1)],
]


def engravings(w, h, seed):
    """two columns of runes down each broad face (the left and right halves of the texture), a carved border line
    round them, and a ring sigil near the top: a mask 0..1"""
    m = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(m)
    rng = np.random.default_rng(seed)
    for half in (0, 1):
        x0 = half * w / 2
        bw = w / 2
        lw = max(3, int(w / 170))
        # border
        d.rectangle([x0 + bw * 0.12, h * 0.08, x0 + bw * 0.88, h * 0.92], outline=150, width=lw)
        # sigil: a ring with a cross
        cx, cy, r = x0 + bw * 0.5, h * 0.19, bw * 0.22
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=255, width=lw + 1)
        d.ellipse([cx - r * 0.55, cy - r * 0.55, cx + r * 0.55, cy + r * 0.55], outline=255, width=lw)
        for a in range(0, 360, 45):
            t = math.radians(a)
            d.line([cx + math.cos(t) * r * 0.55, cy + math.sin(t) * r * 0.55, cx + math.cos(t) * r * 1.15, cy + math.sin(t) * r * 1.15], fill=255, width=lw)
        # two columns of runes
        cell = bw * 0.24
        for col in (0, 1):
            ccx = x0 + bw * (0.33 if col == 0 else 0.67)
            y = h * 0.34
            while y + cell * 1.3 < h * 0.88:
                rune = RUNES[int(rng.integers(len(RUNES)))]
                rw, rh = cell * 0.6, cell * 1.1
                for (a, b, c, e) in rune:
                    d.line([ccx - rw / 2 + a * rw, y + b * rh, ccx - rw / 2 + c * rw, y + e * rh], fill=255, width=lw + 1)
                y += rh * 1.35
    return np.asarray(m, np.float32) / 255


BAKE = None


def bake_mask(mask, size):
    """the engraving (planar space, 0..1) carried into the stone's own UV space at size x size"""
    out = np.zeros((size, size), np.float32)
    mh, mw = mask.shape
    for uv, pl in zip(*BAKE):
        for j in range(1, len(uv) - 1):
            A = np.array([uv[0], uv[j], uv[j + 1]]) * [size, size]; A[:, 1] = size - A[:, 1]
            Q = np.array([pl[0], pl[j], pl[j + 1]])
            x0, y0 = np.floor(A.min(0)).astype(int); x1, y1 = np.ceil(A.max(0)).astype(int)
            x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, size - 1), min(y1, size - 1)
            if x1 < x0 or y1 < y0:
                continue
            yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1].astype(np.float32) + 0.5
            (ax, ay), (bx, by), (cx, cy) = A
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9:
                continue
            l0 = ((by - cy) * (xx - cx) + (cx - bx) * (yy - cy)) / den
            l1 = ((cy - ay) * (xx - cx) + (ax - cx) * (yy - cy)) / den
            l2 = 1 - l0 - l1
            ins = (l0 >= -0.02) & (l1 >= -0.02) & (l2 >= -0.02)
            pu = l0 * Q[0, 0] + l1 * Q[1, 0] + l2 * Q[2, 0]
            pv = l0 * Q[0, 1] + l1 * Q[1, 1] + l2 * Q[2, 1]
            mx = np.clip((pu * mw).astype(int), 0, mw - 1); my = np.clip((pv * mh).astype(int), 0, mh - 1)
            val = mask[my, mx]
            sub = out[y0:y1 + 1, x0:x1 + 1]
            sub[ins] = np.maximum(sub[ins], val[ins])
    return out


def stone_textures():
    A = SRC / 'altar/textures'
    N = 2048
    m = bake_mask(engravings(1830, 2048, 5), N)  # (each face 134 x 300: drawn true to it)
    b = np.asarray(load(A / 'runestone_lp1_platform_BaseColor.png', N), np.float32) / 255
    glow_m = np.asarray(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3)), np.float32) / 255
    carved = b * (1 - 0.5 * m[..., None])
    lit = carved + np.array([0.25, 0.6, 1.0]) * np.clip(m * 1.2, 0, 1)[..., None] * 0.55
    dif = dds(Image.fromarray((np.clip(lit, 0, 1) * 255).astype(np.uint8)), 'heartstone_d.dds')
    g = np.clip(np.maximum(m, glow_m * 0.8), 0, 1)[..., None] * np.array([70, 170, 255])
    glow = dds(Image.fromarray(np.clip(g, 0, 255).astype(np.uint8)).resize((1024, 1024), Image.LANCZOS), 'heartstone_g.dds')
    nrm = 'CurseOfAlduin\\shrine\\altar_platform_n.dds'
    blue = (b.mean(-1, keepdims=True) * np.array([0.35, 0.65, 1.0]) * 1.4 + np.array([0.05, 0.18, 0.4]))
    blue = blue * (1 - m[..., None]) + np.array([0.85, 0.95, 1.0]) * m[..., None]
    dif2 = dds(Image.fromarray((np.clip(blue, 0, 1) * 255).astype(np.uint8)), 'heartstone_lit_d.dds')
    veins = np.clip(0.45 + 0.35 * (b.mean(-1) - 0.35) * 3, 0.25, 0.8)
    g2 = np.clip(veins[..., None] * np.array([60, 150, 255]) + np.maximum(m, glow_m)[..., None] * np.array([200, 230, 255]), 0, 255)
    glow2 = dds(Image.fromarray(g2.astype(np.uint8)).resize((1024, 1024), Image.LANCZOS), 'heartstone_lit_g.dds')
    Image.fromarray((m * 255).astype(np.uint8)).resize((512, 512)).save(WORK / 'bake_preview.png')
    return dif, nrm, glow, dif2, glow2


def stone_textures_old():
    w, h = 1024, 2048
    # rock: the rune stone's own grey rock, a patch of it stretched over the face, with a little noise
    rock = load(SRC / 'runestone/textures/rune_stone_low_DefaultMaterial_BaseColor.jpg').crop((600, 900, 1700, 3100)).resize((w // 2, h), Image.LANCZOS)
    base = Image.new('RGB', (w, h))
    base.paste(rock, (0, 0)); base.paste(rock.transpose(Image.FLIP_LEFT_RIGHT), (w // 2, 0))
    b = np.asarray(base, np.float32) / 255
    b = b * np.array([0.82, 0.86, 0.92]) * 0.85    # a cooler, darker stone
    m = engravings(w, h, 5)
    glow_m = np.asarray(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(4)), np.float32) / 255
    carved = b * (1 - 0.55 * m[..., None])                                       # the cuts read dark by day
    lit = carved + np.array([0.25, 0.6, 1.0]) * np.clip(m * 1.2, 0, 1)[..., None] * 0.6
    dif = dds(Image.fromarray((np.clip(lit, 0, 1) * 255).astype(np.uint8)), 'heartstone_d.dds')
    g = np.clip(np.maximum(m, glow_m * 0.8), 0, 1)[..., None] * np.array([70, 170, 255])
    glow = dds(Image.fromarray(np.clip(g, 0, 255).astype(np.uint8)), 'heartstone_g.dds')
    # the cuts as grooves in the normal map
    gy, gx = np.gradient(np.asarray(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)), np.float32) / 255)
    n = np.dstack([0.5 + gx * 2.5, 0.5 - gy * 2.5, np.ones_like(gx)])
    n = n / np.linalg.norm(n * 2 - 1, axis=-1, keepdims=True) * 0.5
    nm = Image.fromarray(np.dstack([np.clip(n[..., 0] + 0.25, 0, 1) * 255, np.clip(n[..., 1] + 0.25, 0, 1) * 255, np.full_like(gx, 255), np.full_like(gx, 40)]).astype(np.uint8), 'RGBA')
    nrm = dds(nm, 'heartstone_n.dds', alpha=True)
    # freed: the stone turned to blue light - the rock tinted deep blue, the whole of it glowing, the runes white-hot
    blue = (b.mean(-1, keepdims=True) * np.array([0.35, 0.65, 1.0]) * 1.4 + np.array([0.05, 0.18, 0.4]))
    blue = blue * (1 - m[..., None]) + np.array([0.85, 0.95, 1.0]) * m[..., None]
    dif2 = dds(Image.fromarray((np.clip(blue, 0, 1) * 255).astype(np.uint8)), 'heartstone_lit_d.dds')
    veins = np.clip(0.45 + 0.35 * (b.mean(-1) - 0.35) * 3, 0.25, 0.8)
    g2 = np.clip(veins[..., None] * np.array([60, 150, 255]) + np.maximum(m, glow_m)[..., None] * np.array([200, 230, 255]), 0, 255)
    glow2 = dds(Image.fromarray(g2.astype(np.uint8)), 'heartstone_lit_g.dds')
    return dif, nrm, glow, dif2, glow2


def motes_texture():
    """a strip of soft blue motes, seamless top to bottom (it scrolls up the rising sheets)"""
    w, h = 256, 1024
    a = np.zeros((h, w), np.float32)
    rng = np.random.default_rng(9)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    for _ in range(260):
        x, y, r = rng.uniform(10, w - 10), rng.uniform(0, h), rng.uniform(1.5, 5.5)
        for dy in (-h, 0, h):
            dd = ((xx - x) ** 2 + (yy - y - dy) ** 2) / (r * r)
            a += np.exp(-dd) * rng.uniform(0.5, 1.0)
    for _ in range(24):   # a few streaks
        x, y, L = rng.uniform(20, w - 20), rng.uniform(0, h), rng.uniform(30, 90)
        for dy in (-h, 0, h):
            a += np.exp(-((xx - x) ** 2) / 2.0) * np.exp(-(((yy - y - dy) / L) ** 2)) * 0.35
    a = np.clip(a, 0, 1)
    rgb = np.dstack([0.45 + 0.4 * a, 0.75 + 0.2 * a, np.ones_like(a)])
    return dds(Image.fromarray(np.dstack([rgb * 255, a * 255]).astype(np.uint8), 'RGBA'), 'motes_rise.dds', alpha=True)


def halo_texture():
    n = 256
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    r = np.hypot(xx - 0.5, (yy - 0.5) * 0.55) * 2
    a = np.clip(1 - r, 0, 1) ** 2.2
    rgb = np.dstack([0.4 + 0.3 * a, 0.7 + 0.2 * a, np.ones_like(a)])
    return dds(Image.fromarray(np.dstack([rgb * 255, a * 255]).astype(np.uint8), 'RGBA'), 'halo.dds', alpha=True)


def rising(name, tex, halo, n_sheets, alpha, top, extra_halo):
    """sheets standing round the stone's foot, leaning in, their motes sliding up into the stone; faded at both ends"""
    job = [f'out {mesh(name)}']
    bot = LIFT + 63 * S - 2      # the platform's top
    r0, r1 = 70.0, 46.0
    for k in range(n_sheets):
        a = 2 * math.pi * k / n_sheets
        t = np.array([-math.sin(a), math.cos(a)])          # along the sheet
        c0 = np.array([math.cos(a), math.sin(a)]) * r0
        c1 = np.array([math.cos(a), math.sin(a)]) * r1
        half0, half1 = 26.0, 14.0
        job.append(f'shape rise{k} tex={tex} color=0.6,0.85,1.0,1.0 scale=1.6 blend=add vscroll={3.2 + 0.4 * (k % 3)}')
        rows = [(0.0, 0.0), (0.18, 1.0), (0.7, 1.0), (1.0, 0.0)]
        for f, al in rows:
            c = c0 + (c1 - c0) * f
            hw = half0 + (half1 - half0) * f
            z = bot + (top - bot) * f
            for side in (-1, 1):
                p = c + t * hw * side
                job.append(f'v {p[0]:.2f} {p[1]:.2f} {z:.2f} {0.5 + 0.5 * side + 0.13 * k:.3f} {1.2 * f + 0.37 * k:.3f} 1 1 1 {al * alpha:.2f}')
        for r in range(len(rows) - 1):
            i = r * 2
            job += [f't {i} {i + 1} {i + 3}', f't {i} {i + 3} {i + 2}']
    if extra_halo:
        cz = LIFT + 232 * S
        job.append(f'shape halo tex={halo} color=0.45,0.75,1.0,0.5 scale=0.9 blend=add billboard=2 at=0,0,{cz:.1f}')
        hw, hh = 75.0, 120.0
        for x, y, u, v in [(-hw, hh, 0, 0), (hw, hh, 1, 0), (hw, -hh, 1, 1), (-hw, -hh, 0, 1)]:
            job.append(f'v {x} {y} 0 {u} {v}')
        job += ['t 0 1 2', 't 0 2 3']
    run('fxwrite', job, name.replace('.nif', ''))


def main():
    A = SRC / 'altar/textures'
    R = SRC / 'runestone/textures'
    pdif = dds(load(A / 'runestone_lp1_platform_BaseColor.png', 2048), 'altar_platform_d.dds')
    pnrm = dds(normal(A / 'runestone_lp1_platform_Normal.png', A / 'runestone_lp1_platform_Roughness.png', 2048), 'altar_platform_n.dds', alpha=True)
    pglo = dds(Image.fromarray((np.asarray(load(A / 'runestone_lp1_platform_Emissive.png', 1024), np.float32) * np.array([0.75, 1.0, 1.15])).clip(0, 255).astype(np.uint8)), 'altar_platform_g.dds')
    cdif = dds(load(A / 'runestone_lp1_pillars_BaseColor.png', 2048), 'altar_pillars_d.dds')
    cnrm = dds(normal(A / 'runestone_lp1_pillars_Normal.png', A / 'runestone_lp1_pillars_Roughness.png', 2048), 'altar_pillars_n.dds', alpha=True)
    rdif = dds(load(R / 'rune_stone_low_DefaultMaterial_BaseColor.jpg', 2048), 'runerock_d.dds')
    rnrm = dds(normal(R / 'rune_stone_low_DefaultMaterial_Normal.jpg', R / 'rune_stone_low_DefaultMaterial_Roughness.jpg', 2048), 'runerock_n.dds', alpha=True)
    rglo = dds(Image.fromarray((np.asarray(load(R / 'rune_stone_low_DefaultMaterial_Emissive.jpg', 2048), np.float32) * 1.3).clip(0, 255).astype(np.uint8)), 'runerock_g.dds')

    # the platform and the columns (no stone): walkable, the columns solid
    col = [(194, 0, 222), (96.5, -168, 270), (-96.6, -167.5, 340), (-193.4, 0, 270), (-96.6, 167.5, 340), (97.6, 168, 340)]
    lines = [f'in {SRC / "altar/source/runestone_lp.fbx"}', f'out {mesh("shrine.nif")}', f'scale {S}', 'rotate 90 0 0', 'ground 1', f'offset 0 0 {LIFT}',
             'skip stone',
             f'collidefrustum -205 -205 205 205 {LIFT + 63 * S:.1f} {LIFT:.1f} 6',
             f'mat platform diffuse={pdif} normal={pnrm} glow={pglo} emit=0.4,0.8,1.0 emitmult=1.8 spec=0.3 gloss=30',
             f'mat pillars diffuse={cdif} normal={cnrm} spec=0.25 gloss=25']
    for x, y, h in col:
        lines.append(f'collidebox2 {x - 16:.1f} {y - 16:.1f} {LIFT:.1f} {x + 16:.1f} {y + 16:.1f} {LIFT + h * S:.1f}')
    run('modelconv', lines, 'shrine')

    # the heart stone, then lit
    stone_obj()
    sdif, snrm, sglo, ldif, lglo = stone_textures()
    base = [f'in {WORK / "heartstone.obj"}', f'scale {S}', 'ground 0']
    run('modelconv', base + [f'out {mesh("heartstone.nif")}', f'mat heartstone diffuse={sdif} normal={snrm} glow={sglo} emit=0.3,0.7,1.0 emitmult=2.2 spec=0.35 gloss=40'], 'heartstone')
    run('modelconv', base + [f'out {mesh("heartstone_lit.nif")}', f'mat heartstone diffuse={ldif} normal={snrm} glow={lglo} emit=0.45,0.8,1.0 emitmult=2.0 spec=0.6 gloss=60'], 'heartstone_lit')

    # a rune stone floating over each column
    run('modelconv', [f'in {SRC / "runestone/source/rune_stone_low.fbx"}', f'out {mesh("pillarrock.nif")}', 'fit 52', 'rotate 90 0 0', 'ground 0',
                      f'mat DefaultMaterial diffuse={rdif} normal={rnrm} glow={rglo} emit=0.3,0.75,1.0 emitmult=2.0 spec=0.3 gloss=30'], 'pillarrock')

    # the motes rising into the stone
    mt, ht = motes_texture(), halo_texture()
    top = LIFT + 150 * S
    # (Noah: more of them, from the base to a little over the stone)
    rising('risemotes.nif', mt, ht, 12, 0.8, LIFT + 420 * S, False)
    rising('risemotes_lit.nif', mt, ht, 16, 0.9, LIFT + 440 * S, True)
    print('column tops (x, y, z):', [(x, y, round(LIFT + h * S, 1)) for x, y, h in col])


if __name__ == '__main__':
    main()
