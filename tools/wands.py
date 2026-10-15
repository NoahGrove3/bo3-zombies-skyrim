"""The Curse of Alduin wands: procedural meshes + textures, one per wand row in sheets/weapons.json (kind = wand).
Each wand: leather-wrapped grip, metal guard, tapered wood shaft and an element tip (style column), glowing in the
element colour. Written as OBJ (already in Skyrim axes: +Y = tip, +Z = up, grip at the origin) and converted to NIF
by modelconv with Prn WeaponStaff so the engine hangs it in the hand.
    python tools/wands.py <out Data dir> [wand ids...]"""
import json, math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build' / 'assets' / 'Data')
ONLY = set(sys.argv[2:])
CONV = '/home/claude/tools/modelconv'
TEX = 'CurseOfAlduin\\wand'
rng = np.random.default_rng(11)

# ------------------------------------------------------------------ geometry
class Mesh:
    def __init__(self): self.parts = {}  # material -> (verts, uvs, faces)
    def add(self, mat, V, UV, F):
        v, u, f = self.parts.setdefault(mat, ([], [], []))
        base = len(v); v.extend(V); u.extend(UV); f.extend([(a + base, b + base, c + base) for a, b, c in F])

def lathe(mesh, mat, profile, seg=16, twist=0.0, xform=None, vscale=1.0):
    """profile: [(y, r)] along +Y; returns a closed-ish surface of revolution."""
    V, UV, F = [], [], []
    ys = [p[0] for p in profile]; y0, y1 = min(ys), max(ys)
    for i, (y, r) in enumerate(profile):
        for j in range(seg + 1):
            a = 2 * math.pi * j / seg + twist * (y - y0)
            p = np.array([r * math.cos(a), y, r * math.sin(a)])
            if xform is not None: p = xform(p)
            V.append(tuple(p)); UV.append((j / seg, (y - y0) / max(1e-6, y1 - y0) * vscale))
    for i in range(len(profile) - 1):
        for j in range(seg):
            a, b = i * (seg + 1) + j, i * (seg + 1) + j + 1
            c, d = a + seg + 1, b + seg + 1
            F += [(a, c, b), (b, c, d)]
    mesh.add(mat, V, UV, F)

def sphere(mesh, mat, c, r, seg=14, squash=(1, 1, 1)):
    prof = [(math.cos(math.pi * (1 - k / seg)) * r, math.sin(math.pi * k / seg) * r + 1e-4) for k in range(seg + 1)]
    lathe(mesh, mat, prof, seg, xform=lambda p: np.array([p[0] * squash[0], p[1] * squash[1], p[2] * squash[2]]) + np.array(c))

def cone(mesh, mat, base, tip, r, seg=8):
    base, tip = np.array(base, float), np.array(tip, float)
    ax = tip - base; L = np.linalg.norm(ax); ax /= L
    t = np.cross(ax, [0, 0, 1]) if abs(ax[2]) < 0.9 else np.cross(ax, [1, 0, 0]); t /= np.linalg.norm(t); b = np.cross(ax, t)
    V = [tuple(tip)]; UV = [(0.5, 1.0)]; F = []
    for j in range(seg + 1):
        a = 2 * math.pi * j / seg
        V.append(tuple(base + (t * math.cos(a) + b * math.sin(a)) * r)); UV.append((j / seg, 0.0))
    for j in range(seg): F.append((0, j + 1, j + 2))  # CCW seen from outside
    mesh.add(mat, V, UV, F)

def gem(mesh, mat, y0, length, r, sides=6, mid=0.35):
    """bipyramid crystal along +Y"""
    yb, ym, yt = y0, y0 + length * mid, y0 + length
    lathe(mesh, mat, [(yb, 1e-3), (ym, r), (yt, 1e-3)], sides)

def ring(mesh, mat, y, r, thick, seg=20):
    lathe(mesh, mat, [(y - thick, r * 0.85), (y - thick * 0.5, r), (y + thick * 0.5, r), (y + thick, r * 0.85)], seg)

# ------------------------------------------------------------------ wand assembly
def build(style, mesh):
    # grip (leather), pommel and guard (metal), shaft (wood)
    lathe(mesh, 'leather', [(-9, 1.0), (-8, 1.15), (-4, 1.1), (0, 1.15), (3, 1.05)], 14, vscale=4)
    sphere(mesh, 'metal', (0, -9.6, 0), 1.35, 10)
    ring(mesh, 'metal', 3.4, 1.45, 0.55)
    tw = 0.35 if style == 'spiral' else 0.0
    lathe(mesh, 'wood', [(3.6, 1.0), (10, 0.85), (18, 0.7), (25, 0.55), (26.5, 0.6)], 10, twist=tw, vscale=3)
    ring(mesh, 'metal', 26.4, 0.95, 0.4)
    top = 26.8
    if style == 'crystal':
        gem(mesh, 'gem', top, 7.5, 1.25, 6, 0.3)
        for k in range(3):  # three claws gripping the crystal
            a = 2 * math.pi * k / 3
            cone(mesh, 'metal', (math.cos(a) * 0.7, top - 0.2, math.sin(a) * 0.7), (math.cos(a) * 1.45, top + 2.6, math.sin(a) * 1.45), 0.28, 6)
    elif style == 'orb':
        sphere(mesh, 'gem', (0, top + 2.3, 0), 1.9)
        for k in range(4):
            a = 2 * math.pi * k / 4 + 0.4
            cone(mesh, 'metal', (math.cos(a) * 0.6, top, math.sin(a) * 0.6), (math.cos(a) * 1.9, top + 3.4, math.sin(a) * 1.9), 0.3, 6)
    elif style == 'fang':
        for k in range(2):  # two curved fangs + a drop of venom/blood between
            s = 1 if k else -1
            cone(mesh, 'bone', (s * 0.5, top, 0), (s * 1.6, top + 3.0, 0.4), 0.55, 8)
            cone(mesh, 'bone', (s * 1.6, top + 3.0, 0.4), (s * 0.9, top + 5.6, 0.9), 0.38, 8)
        gem(mesh, 'gem', top + 0.8, 4.0, 0.9, 8, 0.6)
    elif style == 'bloom':
        sphere(mesh, 'gem', (0, top + 1.6, 0), 1.0)
        for k in range(5):
            a = 2 * math.pi * k / 5
            cone(mesh, 'wood', (math.cos(a) * 0.5, top + 0.6, math.sin(a) * 0.5), (math.cos(a) * 2.4, top + 3.6, math.sin(a) * 2.4), 0.75, 6)
    elif style == 'skull':
        sphere(mesh, 'bone', (0, top + 2.4, 0.2), 2.1, 12, squash=(0.9, 1.0, 1.05))
        sphere(mesh, 'bone', (0, top + 1.1, -0.9), 1.2, 8, squash=(0.8, 0.7, 0.9))   # jaw
        for s in (-1, 1): sphere(mesh, 'gem', (s * 0.75, top + 2.6, -1.55), 0.45, 6)  # glowing eye sockets
    elif style == 'thorn':
        for k in range(7):
            y = 8 + k * 2.6; a = k * 2.2
            cone(mesh, 'wood', (math.cos(a) * 0.6, y, math.sin(a) * 0.6), (math.cos(a) * 1.9, y + 1.0, math.sin(a) * 1.9), 0.25, 5)
        gem(mesh, 'gem', top, 6.5, 1.0, 5, 0.25)
    elif style == 'spiral':
        sphere(mesh, 'gem', (0, top + 3.0, 0), 1.25)
        for k in range(36):  # a ribbon of wind curling round the orb
            t = k / 35; a = t * 4 * math.pi; r = 1.9 - 0.5 * t; y = top + t * 5.5
            sphere(mesh, 'metal', (math.cos(a) * r, y, math.sin(a) * r), 0.3, 5)
    elif style == 'star':
        gem(mesh, 'gem', top + 0.5, 4.5, 1.0, 4, 0.5)
        for k in range(6):  # a six-pointed star around the core
            a = 2 * math.pi * k / 6
            cone(mesh, 'gem', (0, top + 2.6, 0), (math.cos(a) * 3.3, top + 2.6 + math.sin(a) * 3.3, 0), 0.55, 4)
        ring(mesh, 'metal', top + 0.3, 1.0, 0.3)
    elif style == 'sun':
        sphere(mesh, 'gem', (0, top + 3.0, 0), 1.5)
        lathe(mesh, 'metal', [(top + 2.75, 2.4), (top + 3.25, 2.4), (top + 3.25, 2.0), (top + 2.75, 2.0), (top + 2.75, 2.4)], 24)
        for k in range(12):
            a = 2 * math.pi * k / 12
            cone(mesh, 'metal', (math.cos(a) * 2.2, top + 3.0 + math.sin(a) * 2.2 * 0, math.sin(a) * 2.2), (math.cos(a) * 3.8, top + 3.0, math.sin(a) * 3.8), 0.32, 4)
    else:
        gem(mesh, 'gem', top, 6, 1.1)

def write_obj(mesh, path, mtl):
    lines = [f'mtllib {mtl.name}']; vo = 1
    for mat, (V, UV, F) in mesh.parts.items():
        lines.append(f'o {mat}'); lines.append(f'usemtl {mat}')
        lines += [f'v {x:.4f} {y:.4f} {z:.4f}' for x, y, z in V]
        lines += [f'vt {u:.4f} {v:.4f}' for u, v in UV]
        lines += [f'f {a+vo}/{a+vo} {b+vo}/{b+vo} {c+vo}/{c+vo}' for a, b, c in F]
        vo += len(V)
    path.write_text('\n'.join(lines) + '\n')
    mtl.write_text(''.join(f'newmtl {m}\nKd 1 1 1\n' for m in mesh.parts))

# ------------------------------------------------------------------ textures
def hexrgb(h): return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
def save(im, rel, alpha=False):
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'wand' / rel; p.parent.mkdir(parents=True, exist_ok=True)
    im.convert('RGBA' if alpha else 'RGB').save(p, pixel_format='DXT5' if alpha else 'DXT1')
    return f'{TEX}\\{rel}'
def flat_normal(spec, size=64): return Image.new('RGBA', (size, size), (128, 128, 255, spec))
def wood(base, size=256):
    y = np.linspace(0, 1, size)[:, None]; x = np.linspace(0, 1, size)[None, :]
    grain = np.sin((x * 9 + np.sin(y * 40 + x * 5) * 0.15) * 2 * math.pi * 3) * 0.5 + 0.5
    n = rng.random((size, size)); n = np.asarray(Image.fromarray((n * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1)), np.float32) / 255
    k = (0.65 + 0.25 * grain + 0.15 * n)[..., None]
    return Image.fromarray(np.clip(np.array(base, np.float32) * k, 0, 255).astype(np.uint8), 'RGB')
def leather(base, size=128):
    y = np.linspace(0, 1, size)[:, None]
    wrap = (np.sin(y * 2 * math.pi * 10) * 0.5 + 0.5) ** 0.6
    n = rng.random((size, size)) * 0.15
    k = (0.55 + 0.35 * wrap + n)[..., None]
    return Image.fromarray(np.clip(np.array(base, np.float32) * k, 0, 255).astype(np.uint8), 'RGB')
def metal(base, size=128):
    x = rng.random((size, 1)) * 0.25 + 0.75
    k = np.repeat(x, size, 1)[..., None]
    return Image.fromarray(np.clip(np.array(base, np.float32) * k, 0, 255).astype(np.uint8), 'RGB')
def gemtex(col, size=128):
    y = np.linspace(0, 1, size)[:, None]; x = np.linspace(0, 1, size)[None, :]
    facet = 0.7 + 0.3 * np.abs(np.sin(x * 6 * math.pi)) * (0.6 + 0.4 * y)
    c = np.array(col, np.float32)
    rgb = np.clip(c[None, None, :] * facet[..., None] + 40 * (y ** 3)[..., None], 0, 255)
    return Image.fromarray(rgb.astype(np.uint8), 'RGB')

STYLE_LOOK = {  # wood, metal, leather
    'crystal': ((120, 84, 52), (200, 200, 210), (70, 45, 30)), 'fang': ((60, 50, 40), (110, 110, 100), (40, 55, 30)),
    'orb': ((95, 70, 55), (205, 170, 90), (50, 40, 60)), 'bloom': ((85, 95, 50), (140, 120, 80), (60, 70, 35)),
    'skull': ((60, 55, 50), (90, 85, 80), (35, 30, 28)), 'thorn': ((45, 35, 50), (80, 70, 95), (30, 25, 35)),
    'spiral': ((150, 140, 125), (210, 225, 230), (90, 100, 110)), 'star': ((40, 45, 80), (190, 200, 240), (30, 35, 60)),
    'sun': ((165, 120, 60), (235, 195, 90), (120, 70, 35)),
}

def make(w):
    wid, style, col = w['id'], w['wandStyle'], hexrgb(w['element'])
    wood_c, metal_c, leather_c = STYLE_LOOK.get(style, STYLE_LOOK['crystal'])
    if wid == 'drill': wood_c, metal_c = (70, 25, 30), (120, 30, 40)  # blood: dark red wood
    if wid == 'centurion': wood_c = (40, 60, 35)
    t = {
        'wood': save(wood(wood_c), f'{wid}_wood_d.dds'), 'metal': save(metal(metal_c), f'{wid}_metal_d.dds'),
        'leather': save(leather(leather_c), f'{wid}_leather_d.dds'), 'bone': save(wood((215, 205, 180)), f'{wid}_bone_d.dds'),
        'gem': save(gemtex(col), f'{wid}_gem_d.dds'), 'gem_g': save(Image.new('RGB', (64, 64), col), f'{wid}_gem_g.dds'),
        'n_dull': save(flat_normal(30), 'flat_dull_n.dds', True), 'n_shiny': save(flat_normal(170), 'flat_shiny_n.dds', True),
    }
    mesh = Mesh(); build(style, mesh)
    work = OUT.parent / 'wands'; work.mkdir(parents=True, exist_ok=True)
    obj, mtl = work / f'{wid}.obj', work / f'{wid}.mtl'
    write_obj(mesh, obj, mtl)
    e = f'{col[0]/255:.2f},{col[1]/255:.2f},{col[2]/255:.2f}'
    out = OUT / 'Meshes' / 'CurseOfAlduin' / 'wand' / f'wand_{wid}.nif'; out.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {out}', 'prn WeaponStaff', 'origin 1',
           f'mat wood diffuse={t["wood"]} normal={t["n_dull"]} spec=0.2 gloss=20',
           f'mat leather diffuse={t["leather"]} normal={t["n_dull"]} spec=0.1 gloss=10',
           f'mat bone diffuse={t["bone"]} normal={t["n_dull"]} spec=0.3 gloss=25',
           f'mat metal diffuse={t["metal"]} normal={t["n_shiny"]} spec=1.2 gloss=90',
           f'mat gem diffuse={t["gem"]} normal={t["n_shiny"]} glow={t["gem_g"]} emit={e} emitmult=2.5 spec=1.5 gloss=120']
    jf = work / f'{wid}.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([CONV, str(jf)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode: raise SystemExit('modelconv failed for ' + wid)

if __name__ == '__main__':
    W = [w for w in json.load(open(ROOT / 'sheets' / 'weapons.json'))['rows'] if w.get('kind') == 'wand']
    for w in W:
        if not ONLY or w['id'] in ONLY: make(w)
