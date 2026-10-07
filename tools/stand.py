"""The Undying Nights station stand: one stone platform that carries the Brew Machine, Sheogorath's Coffer, the
Atronach Reforge and the wall-buy cases, so every machine shares one floor height (nothing can sink under a town's
ground). Model frame: origin = centre of the top surface, front (where you walk up) = +Y, up = +Z.
Collision is one box over the slab only (pillars and steps are walk-through decoration).
    python tools/stand.py <out Data dir>"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build' / 'assets' / 'Data')
CONV = '/home/claude/tools/modelconv'
rng = np.random.default_rng(5)

# slab extents (keep in sync with sheets/layout.json: back row y=-70 / front row y=+120 in this frame)
X0, X1, Y0, Y1, TOP, BOTTOM = -330, 330, -150, 185, 0, -100
GROW = 170  # skirt reach: ~30 degree walkable slope

parts = {}
def add(mat, V, UV, F):
    v, u, f = parts.setdefault(mat, ([], [], []))
    b = len(v); v.extend(V); u.extend(UV); f.extend([(a + b, c + b, d + b) for a, c, d in F])

def box(mat, x0, y0, z0, x1, y1, z1, tile=96.0):
    """axis-aligned box, outward faces, planar UVs in world units / tile"""
    faces = [  # (normal axis, sign, quad corners)
        ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), 'z'),   # top
        ((x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0), 'z'),   # bottom
        ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), 'y'),   # -y
        ((x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1), 'y'),   # +y
        ((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), 'x'),   # -x
        ((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1), 'x'),   # +x
    ]
    for *q, ax in faces:
        uv = [((p[0] if ax != 'x' else p[1]) / tile, (p[1] if ax == 'z' else p[2]) / tile) for p in q]
        add(mat, q, uv, [(0, 1, 2), (0, 2, 3)])

def ring(mat, cx, cy, z, r0, r1, seg=64):
    V, UV, F = [], [], []
    for i in range(seg + 1):
        a = 2 * math.pi * i / seg
        for r in (r0, r1):
            V.append((cx + r * math.cos(a), cy + r * math.sin(a), z)); UV.append((i / seg * 8, 0 if r == r0 else 1))
    for i in range(seg):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        F += [(a, b, d), (a, d, c)]
    add(mat, V, UV, F)

def crystal(mat, cx, cy, z0, h, r, sides=6):
    V = [(cx, cy, z0), (cx, cy, z0 + h)]; UV = [(0.5, 0), (0.5, 1)]; F = []
    zm = z0 + h * 0.35
    for i in range(sides):
        a = 2 * math.pi * i / sides
        V.append((cx + r * math.cos(a), cy + r * math.sin(a), zm)); UV.append((i / sides, 0.35))
    for i in range(sides):
        a, b = 2 + i, 2 + (i + 1) % sides
        F += [(0, b, a), (1, a, b)]
    add(mat, V, UV, F)

def build():
    box('stone', X0, Y0, BOTTOM, X1, Y1, TOP)                                   # the slab
    box('stonedark', X0 - 6, Y0 - 6, -10, X1 + 6, Y1 + 6, -3)                   # lip under the top edge
    # sloped stone skirt all round: walk up onto the stand from any side, even where the ground drops away
    t, b = -9, BOTTOM
    corners_top = [(X0, Y0), (X1, Y0), (X1, Y1), (X0, Y1)]
    corners_bot = [(X0 - GROW, Y0 - GROW), (X1 + GROW, Y0 - GROW), (X1 + GROW, Y1 + GROW), (X0 - GROW, Y1 + GROW)]
    for i in range(4):
        (ax, ay), (bx, by) = corners_top[i], corners_top[(i + 1) % 4]
        (cx, cy), (dx, dy) = corners_bot[(i + 1) % 4], corners_bot[i]
        q = [(ax, ay, t), (bx, by, t), (cx, cy, b), (dx, dy, b)]
        L = math.hypot(bx - ax, by - ay)
        uv = [(0, 0), (L / 96, 0), (L / 96, 2.4), (0, 2.4)]
        add('stonedark', q, uv, [(0, 2, 1), (0, 3, 2)])
    for px in (X0 + 22, X1 - 22):
        for py in (Y0 + 22, Y1 - 22):
            box('stonedark', px - 18, py - 18, TOP, px + 18, py + 18, 150, 64)  # pillar
            box('stone', px - 24, py - 24, 150, px + 24, py + 24, 162, 64)      # cap
            crystal('crystal', px, py, 162, 34, 9)
    # rune circle on the floor between the machines and the wall cases
    ring('rune', 0, 25, TOP + 0.6, 58, 66)
    ring('rune', 0, 25, TOP + 0.6, 36, 40)
    for k in range(6):
        a = 2 * math.pi * k / 6
        x, y = math.cos(a) * 48, 25 + math.sin(a) * 48
        crystal('rune', x, y, TOP + 0.6, 0.1, 5, 4)

def write_obj(path):
    lines = [f'mtllib {path.stem}.mtl']; vo = 1
    for mat, (V, UV, F) in parts.items():
        lines += [f'o {mat}', f'usemtl {mat}']
        lines += [f'v {x:.3f} {y:.3f} {z:.3f}' for x, y, z in V]
        lines += [f'vt {u:.4f} {v:.4f}' for u, v in UV]
        lines += [f'f {a+vo}/{a+vo} {b+vo}/{b+vo} {c+vo}/{c+vo}' for a, b, c in F]
        vo += len(V)
    path.write_text('\n'.join(lines) + '\n')
    path.with_suffix('.mtl').write_text(''.join(f'newmtl {m}\nKd 1 1 1\n' for m in parts))

def save(im, rel, alpha=False):
    p = OUT / 'Textures' / 'UndyingNights' / 'stand' / rel; p.parent.mkdir(parents=True, exist_ok=True)
    im.convert('RGBA' if alpha else 'RGB').save(p, pixel_format='DXT5' if alpha else 'DXT1')
    return f'UndyingNights\\stand\\{rel}'

def stone(base, size=512, tiles=4):
    n = rng.random((size, size)).astype(np.float32)
    n = np.asarray(Image.fromarray((n * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)), np.float32) / 255
    big = np.asarray(Image.fromarray((rng.random((size // 16, size // 16)) * 255).astype(np.uint8)).resize((size, size), Image.BICUBIC), np.float32) / 255
    y, x = np.mgrid[0:size, 0:size]
    cell = size // tiles
    off = ((y // cell) % 2) * (cell // 2)                      # running-bond flagstones
    grout = (((x + off) % cell) < 5) | ((y % cell) < 5)
    k = 0.72 + 0.18 * big + 0.12 * n
    k = np.where(grout, 0.45, k)
    rgb = np.clip(np.array(base, np.float32)[None, None, :] * k[..., None], 0, 255).astype(np.uint8)
    nrm = np.full((size, size, 4), (128, 128, 255, 30), np.uint8)
    gx = np.gradient(k, axis=1); gy = np.gradient(k, axis=0)
    nrm[..., 0] = np.clip(128 - gx * 600, 0, 255); nrm[..., 1] = np.clip(128 + gy * 600, 0, 255)
    return Image.fromarray(rgb, 'RGB'), Image.fromarray(nrm, 'RGBA')

def main():
    build()
    work = OUT.parent / 'stand'; work.mkdir(parents=True, exist_ok=True)
    obj = work / 'stand.obj'; write_obj(obj)
    s_d, s_n = stone((150, 148, 160)); d_d, d_n = stone((92, 88, 100), tiles=2)
    t = {'stone_d': save(s_d, 'stone_d.dds'), 'stone_n': save(s_n, 'stone_n.dds', True),
         'dark_d': save(d_d, 'dark_d.dds'), 'dark_n': save(d_n, 'dark_n.dds', True),
         'rune_d': save(Image.new('RGB', (64, 64), (170, 90, 255)), 'rune_d.dds'),
         'rune_g': save(Image.new('RGB', (64, 64), (190, 110, 255)), 'rune_g.dds'),
         'flat_n': save(Image.new('RGBA', (64, 64), (128, 128, 255, 160)), 'flat_n.dds', True)}
    out = OUT / 'Meshes' / 'UndyingNights' / 'stand' / 'stand.nif'; out.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {out}', 'origin 1', f'collidefrustum {X0} {Y0} {X1} {Y1} {TOP} {BOTTOM} {GROW}',
           f'mat stone diffuse={t["stone_d"]} normal={t["stone_n"]} spec=0.2 gloss=20',
           f'mat stonedark diffuse={t["dark_d"]} normal={t["dark_n"]} spec=0.2 gloss=20',
           f'mat rune diffuse={t["rune_d"]} normal={t["flat_n"]} glow={t["rune_g"]} emit=0.75,0.43,1.0 emitmult=3 spec=0.5',
           f'mat crystal diffuse={t["rune_d"]} normal={t["flat_n"]} glow={t["rune_g"]} emit=0.75,0.43,1.0 emitmult=2.5 spec=1.5 gloss=120']
    jf = work / 'stand.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([CONV, str(jf)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode: raise SystemExit('modelconv failed')

if __name__ == '__main__':
    main()
