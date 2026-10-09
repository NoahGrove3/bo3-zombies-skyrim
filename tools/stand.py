"""The Undying Nights station: an open Nordic shrine you can walk up to from every side. An eight-sided stone dais
(Whiterun's own stonework, a knotwork band round its edge, a cobbled skirt sloping down on every side) carries a
carved rune-stone at its heart; the Coffer, the Brew Machine, the Atronach Reforge and the Shrine of the Dirge stand
round it with their backs to the stone, each facing out to its own side, with a brazier on each of the four
corners between them. Every texture is Skyrim's own Whiterun set, referenced by path (nothing of the game is
shipped), so texture overhauls restyle the station with the city.

Model frame: origin = centre of the dais top, +Y = the Coffer's side, up = +Z. Machines (layout sheet, plan frame):
Coffer (0,-175) facing out, Reforge (0,175), Brew Machine (175,0), Shrine (-175,0).
Collision: the dais (walkable sloped skirt on every side) and the rune-stone.
    python tools/stand.py <out Data dir>"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build' / 'assets' / 'Data')
CONV = '/home/claude/tools/modelconv'

R = 300                                                       # dais: across the flats, half
X0, X1, Y0, Y1, TOP, BOTTOM = -R, R, -R, R, 0, -100            # (the walkable collision frustum)
GROW = 110                                                     # skirt reach: ~30 degree walkable slope
STONE_R, STONE_H = 40, 230                                     # the rune-stone at the heart
ALCOVES = [(0, 0, 0, (1.0, 0.45, 0.12)), (0, 0, 0, (0.70, 0.35, 1.0)), (0, 0, 0, (1.0, 0.75, 0.25))]  # (glyph colours: Reforge, Coffer, Brew)

W = 'textures\\architecture\\whiterun\\'
TEX = {   # Skyrim's Whiterun textures (diffuse, normal)
    'floor': ('wrstoneblocks01', 'wrstoneblocks01_n'),
    'stone': ('wrstoneblocks01', 'wrstoneblocks01_n'),
    'cobble': ('wrstonefloor01', 'wrstonefloor01_n'),
    'wood': ('wrwoodbeam01', 'wrwoodbeam01_n'),
    'trim': ('wrtrims01', 'wrtrims01_n'),
    'panel': ('wrhallcarvings01', 'wrhallcarvings01_n'),
    'roof': ('wrdragontile01', 'wrdragontile01_n'),
    'banner': ('wrcitybannerinsidetall01', 'wrcitybannerinsidetall01_n'),
}
# bands of the trim atlas (DirectX V, top-down): brown knotwork, blue knotwork
KNOT_BROWN, KNOT_BLUE = (0.555, 0.865), (0.14, 0.36)

parts = {}
def add(mat, V, UV, F):
    v, u, f = parts.setdefault(mat, ([], [], []))
    b = len(v); v.extend(V); u.extend(UV); f.extend([(a + b, c + b, d + b) for a, c, d in F])

def quad(mat, p0, p1, p2, p3, uv=((0, 0), (1, 0), (1, 1), (0, 1)), both=False):
    """p0..p3 counter-clockwise seen from the front; uv in OBJ convention (v up)"""
    add(mat, [p0, p1, p2, p3], list(uv), [(0, 1, 2), (0, 2, 3)])
    if both:
        add(mat, [p0, p3, p2, p1], [uv[0], uv[3], uv[2], uv[1]], [(0, 1, 2), (0, 2, 3)])

def box(mat, x0, y0, z0, x1, y1, z1, tile=128.0, skip=()):
    """axis-aligned box, outward faces, planar UVs in world units / tile; skip: faces to leave out"""
    faces = {
        'top': ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), 'z'),
        'bottom': ((x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0), 'z'),
        'back': ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), 'y'),
        'front': ((x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1), 'y'),
        'left': ((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), 'x'),
        'right': ((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1), 'x'),
    }
    for name, (*q, ax) in faces.items():
        if name in skip:
            continue
        uv = [((p[0] if ax != 'x' else p[1]) / tile, (p[1] if ax == 'z' else p[2]) / tile) for p in q]
        add(mat, q, uv, [(0, 1, 2), (0, 2, 3)])

def band(mat, a, b, z0, z1, out, vband, tile=160.0):
    """a vertical strip of trim along a->b (x,y), facing 'out' (unit x,y), mapped onto one band of the atlas"""
    (ax, ay), (bx, by) = a, b
    ox, oy = out
    L = math.hypot(bx - ax, by - ay)
    v0, v1 = 1 - vband[1], 1 - vband[0]
    p = [(ax + ox, ay + oy, z0), (bx + ox, by + oy, z0), (bx + ox, by + oy, z1), (ax + ox, ay + oy, z1)]
    # wind so the face looks along 'out'
    n = np.cross(np.subtract(p[1], p[0]), np.subtract(p[3], p[0]))
    uv = [(0, v0), (L / tile, v0), (L / tile, v1), (0, v1)]
    if n[0] * ox + n[1] * oy < 0:
        p = [p[1], p[0], p[3], p[2]]; uv = [uv[1], uv[0], uv[3], uv[2]]
    quad(mat, *p, uv=uv)

def slanted(mat, a, b, w, h, tile=128.0):
    """a beam from a to b (3D points), width w (horizontal) and height h (vertical), wood grain along it"""
    a, b = np.array(a, float), np.array(b, float)
    d = b - a; L = np.linalg.norm(d); d /= L
    side = np.cross(d, (0, 0, 1)); side /= np.linalg.norm(side)
    up = np.cross(side, d)
    c = [a + side * sx * w / 2 + up * sz * h / 2 for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    e = [p + d * L for p in c]
    for i in range(4):
        j = (i + 1) % 4
        uv = [(0, 0), (L / tile, 0), (L / tile, 0.25), (0, 0.25)]
        quad(mat, tuple(c[i]), tuple(e[i]), tuple(e[j]), tuple(c[j]), uv=uv)
    quad(mat, *[tuple(p) for p in c[::-1]]); quad(mat, *[tuple(p) for p in e])

def ring(mat, cx, cy, z, r0, r1, seg=72, vband=None, tile=1.0):
    V, UV, F = [], [], []
    circ = 2 * math.pi * (r0 + r1) / 2
    for i in range(seg + 1):
        a = 2 * math.pi * i / seg
        for k, r in enumerate((r0, r1)):
            V.append((cx + r * math.cos(a), cy + r * math.sin(a), z))
            if vband:
                UV.append((i / seg * circ / 160.0, 1 - (vband[1] if k == 0 else vband[0])))
            else:
                UV.append((i / seg * 8, k))
    for i in range(seg):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        F += [(a, b, d), (a, d, c)]
    add(mat, V, UV, F)

def disc(mat, cx, cy, z, r, seg=48, tile=128.0):
    V = [(cx, cy, z)] + [(cx + r * math.cos(2 * math.pi * i / seg), cy + r * math.sin(2 * math.pi * i / seg), z) for i in range(seg)]
    UV = [(x / tile, y / tile) for x, y, _ in V]
    add(mat, V, UV, [(0, 1 + i, 1 + (i + 1) % seg) for i in range(seg)])

def octagon(r, z):
    return [(r / math.cos(math.pi / 8) * math.cos(math.pi / 8 + k * math.pi / 4), r / math.cos(math.pi / 8) * math.sin(math.pi / 8 + k * math.pi / 4), z) for k in range(8)]

def build():
    # ---- the dais: eight sides of Whiterun stone, a knotwork band round the edge, a cobbled skirt all round
    top = octagon(R, TOP)
    add('floor', [(0, 0, TOP)] + top, [(0, 0)] + [(x / 150, y / 150) for x, y, _ in top], [(0, 1 + k, 1 + (k + 1) % 8) for k in range(8)])
    under = octagon(R, -22); bot = octagon(R + GROW, BOTTOM)
    for k in range(8):
        a, b = top[k], top[(k + 1) % 8]
        out = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2); L = math.hypot(*out); out = (out[0] / L, out[1] / L)
        band('trim', a[:2], b[:2], -22, 0, (out[0] * 0.8, out[1] * 0.8), KNOT_BROWN, tile=150)
        c, d = under[k], under[(k + 1) % 8]; e, f = bot[(k + 1) % 8], bot[k]
        Lr = math.hypot(d[0] - c[0], d[1] - c[1])
        add('cobble', [c, d, e, f], [(0, 0), (Lr / 140, 0), (Lr / 140, 1.6), (0, 1.6)], [(0, 2, 1), (0, 3, 2)])
    # the seal in the floor: a knotwork ring round the heart, a faint rune of the curse
    ring('trim', 0, 0, TOP + 0.6, 92, 116, vband=KNOT_BLUE)
    ring('rune', 0, 0, TOP + 0.8, 118, 121)
    ring('rune', 0, 0, TOP + 0.8, 86, 88)
    ring('rune', 0, 0, TOP + 0.8, 268, 271)
    # ---- the rune-stone: a stepped plinth, an eight-sided carved shaft, a glowing glyph on each face, a cap
    for i, (rr, z0, z1) in enumerate(((70, TOP, 14), (54, 14, 30))):
        o0, o1 = octagon(rr, z0), octagon(rr, z1)
        add('stone', o1 + [(0, 0, z1)], [(x / 128, y / 128) for x, y, _ in o1] + [(0, 0)], [(8, k, (k + 1) % 8) for k in range(8)])
        for k in range(8):
            a, b = o0[k], o0[(k + 1) % 8]; c, d = o1[(k + 1) % 8], o1[k]
            quad('stone', a, b, c, d, uv=((0, 0), (0.6, 0), (0.6, 0.15), (0, 0.15)))
    s0, s1 = octagon(STONE_R, 30), octagon(STONE_R * 0.82, 30 + STONE_H)
    for k in range(8):
        a, b = s0[k], s0[(k + 1) % 8]; c, d = s1[(k + 1) % 8], s1[k]
        quad('panel' if k % 2 == 0 else 'stone', a, b, c, d, uv=((0, 0), (0.25, 0), (0.25, 1.0), (0, 1.0)))
        if k % 2 == 1:  # a glyph on every other face
            mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2; L = math.hypot(mx, my); nx, ny = mx / L, my / L
            tx, ty = -ny, nx; gz = 30 + STONE_H * 0.62; r = 13
            px, py = mx * 0.94 + nx * 1.5, my * 0.94 + ny * 1.5
            quad('glyph%d' % (k // 2 % 3), (px - tx * r * 0.75, py - ty * r * 0.75, gz), (px, py, gz - r), (px + tx * r * 0.75, py + ty * r * 0.75, gz), (px, py, gz + r), both=True)
    cap = octagon(STONE_R * 0.95, 30 + STONE_H + 14)
    add('wood', cap + [(0, 0, 30 + STONE_H + 30)], [(x / 96, y / 96) for x, y, _ in cap] + [(0, 0)], [(8, k, (k + 1) % 8) for k in range(8)])
    for k in range(8):
        a, b = s1[k], s1[(k + 1) % 8]; c, d = cap[(k + 1) % 8], cap[k]
        quad('wood', a, b, c, d, uv=((0, 0), (0.3, 0), (0.3, 0.12), (0, 0.12)))

def collision_boxes():
    """solid beyond the dais: the rune-stone (model units)"""
    return [(-STONE_R, -STONE_R, TOP, STONE_R, STONE_R, 30 + STONE_H + 30), (-70, -70, TOP, 70, 70, 30)]

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

def main():
    build()
    work = OUT.parent / 'stand'; work.mkdir(parents=True, exist_ok=True)
    obj = work / 'stand.obj'; write_obj(obj)
    flat = save(Image.new('RGBA', (64, 64), (128, 128, 255, 160)), 'flat_n.dds', True)
    rune_d = save(Image.new('RGB', (64, 64), (170, 90, 255)), 'rune_d.dds')
    rune_g = save(Image.new('RGB', (64, 64), (190, 110, 255)), 'rune_g.dds')
    out = OUT / 'Meshes' / 'UndyingNights' / 'stand' / 'stand.nif'; out.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {out}', 'origin 1', f'collidefrustum {X0} {Y0} {X1} {Y1} {TOP} {BOTTOM} {GROW}']
    job += [f'collidebox2 {" ".join(str(v) for v in b)}' for b in collision_boxes()]
    for m, (d, n) in TEX.items():
        if m not in parts:
            continue
        extra = {'banner': ' doublesided=1 alpha=1', 'roof': ' doublesided=1', 'panel': ''}.get(m, '')
        spec = {'stone': 0.25, 'floor': 0.3, 'cobble': 0.25, 'wood': 0.2, 'trim': 0.25, 'panel': 0.2, 'roof': 0.15, 'banner': 0.1}[m]
        job.append(f'mat {m} diffuse={W}{d}.dds normal={W}{n}.dds spec={spec} gloss=30{extra}')
    job.append(f'mat rune diffuse={rune_d} normal={flat} glow={rune_g} emit=0.75,0.43,1.0 emitmult=2.5 spec=0.5')
    for i, (*_, col) in enumerate(ALCOVES):
        rgb = tuple(int(c * 255) for c in col)
        gd = save(Image.new('RGB', (32, 32), rgb), f'glyph{i}_d.dds'); gg = save(Image.new('RGB', (32, 32), tuple(min(255, c + 40) for c in rgb)), f'glyph{i}_g.dds')
        job.append(f'mat glyph{i} diffuse={gd} normal={flat} glow={gg} emit={col[0]:.2f},{col[1]:.2f},{col[2]:.2f} emitmult=3 spec=0.5')
    jf = work / 'stand.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([CONV, str(jf)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode: raise SystemExit('modelconv failed')

if __name__ == '__main__':
    main()
