"""The Undying Nights station: a Nordic shrine built like Whiterun's own stonework. A stone dais carries the
Atronach Reforge, Sheogorath's Coffer and the Brew Machine, each set into its own alcove in a carved back wall
(stone piers, timber lintels, carved dragon panels at the back of every alcove), under a timber-and-shingle
canopy on carved posts, with Nordic knotwork trim round the dais. Every texture is Skyrim's own Whiterun set,
referenced by path (nothing of the game is shipped), so texture overhauls restyle the station with the city.

Model frame: origin = centre of the dais top, front (where you walk up) = +Y, up = +Z. Machines (layout sheet,
seen from this frame): Reforge x=-165, Coffer x=80, Brew Machine x=235, all at y=-70, facing +Y.
Collision: the dais (walkable sloped skirt), the wall piers and backs, the posts. The canopy is see-through to
the physics (it never counts as an overhead obstruction when the station is checked).
    python tools/stand.py <out Data dir>"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build' / 'assets' / 'Data')
CONV = '/home/claude/tools/modelconv'

X0, X1, Y0, Y1, TOP, BOTTOM = -330, 330, -150, 185, 0, -100   # dais (layout sheet footprint)
GROW = 170                                                     # skirt reach: ~30 degree walkable slope
WALL_BACK, NICHE_BACK, WALL_FRONT, WALL_H = -150, -140, -112, 300
# alcoves: (x0, x1, opening height, glyph colour) for the Reforge, the Coffer and the Brew Machine
ALCOVES = [(-305, -25, 235, (1.0, 0.45, 0.12)), (25, 135, 165, (0.70, 0.35, 1.0)), (180, 290, 215, (1.0, 0.75, 0.25))]
POSTS = [(-318, -14), (318, -14)]
EAVE_Y, EAVE_Z, RIDGE_Z = 12, 276, 352                         # canopy: from the wall top down to the eave

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

def piers():
    """the wall's solid parts between the alcoves"""
    xs = [X0] + [v for a in ALCOVES for v in a[:2]] + [X1]
    return [(xs[i], xs[i + 1]) for i in range(0, len(xs), 2)]

def build():
    # ---- the dais: Whiterun stone blocks on top, a knotwork band round the edge, a cobbled skirt
    box('floor', X0, Y0, BOTTOM, X1, Y1, TOP, tile=150, skip=('bottom',))
    for (a, b, out) in (((X0, Y1), (X1, Y1), (0, 1)), ((X1, Y1), (X1, Y0), (1, 0)), ((X0, Y0), (X0, Y1), (-1, 0))):
        band('trim', a, b, -22, 0, (out[0] * 0.8, out[1] * 0.8), KNOT_BROWN, tile=150)
    t, bt = -22, BOTTOM
    corners_top = [(X0, Y0), (X1, Y0), (X1, Y1), (X0, Y1)]
    corners_bot = [(X0 - GROW, Y0 - GROW), (X1 + GROW, Y0 - GROW), (X1 + GROW, Y1 + GROW), (X0 - GROW, Y1 + GROW)]
    for i in range(4):
        (ax, ay), (bx, by) = corners_top[i], corners_top[(i + 1) % 4]
        (cx, cy), (dx, dy) = corners_bot[(i + 1) % 4], corners_bot[i]
        q = [(ax, ay, t), (bx, by, t), (cx, cy, bt), (dx, dy, bt)]
        L = math.hypot(bx - ax, by - ay)
        uv = [(0, 0), (L / 140, 0), (L / 140, 1.6), (0, 1.6)]
        add('cobble', q, uv, [(0, 2, 1), (0, 3, 2)])
    # broad stone steps up the front, flanked by low knotwork-trimmed cheek walls
    for i, (ya, yb, zt) in enumerate(((Y1, Y1 + 34, -26), (Y1 + 34, Y1 + 68, -52), (Y1 + 68, Y1 + 102, -78))):
        box('floor', -150, ya, BOTTOM, 150, yb, zt, tile=150, skip=('bottom', 'back'))
        band('trim', (-150, yb), (150, yb), zt - 10, zt, (0, 0.6), KNOT_BROWN, tile=150)
    for sx in (-1, 1):
        xa, xb = (150, 168) if sx > 0 else (-168, -150)
        box('stone', xa, Y1, BOTTOM, xb, Y1 + 102, 8, skip=('bottom', 'back'))
    # the seal in the floor before the machines: a knotwork ring round a cobbled disc, a faint rune of the curse
    disc('cobble', 0, 70, TOP + 0.5, 64)
    ring('trim', 0, 70, TOP + 0.6, 64, 86, vband=KNOT_BLUE)
    ring('rune', 0, 70, TOP + 0.8, 88, 91)
    ring('rune', 0, 70, TOP + 0.8, 58, 60)

    # ---- the back wall: stone piers, an alcove for each machine with a carved dragon panel at its back
    for x0, x1 in piers():
        box('stone', x0, WALL_BACK, TOP, x1, WALL_FRONT, WALL_H, skip=('bottom',))
        box('stone', x0 - 4, WALL_FRONT, TOP, x1 + 4, WALL_FRONT + 10, 26, skip=('bottom',))   # plinth course
    for x0, x1, h, col in ALCOVES:
        box('stone', x0, WALL_BACK, TOP, x1, NICHE_BACK, h, skip=('bottom', 'left', 'right'))     # alcove back
        quad('panel', (x0 + 6, NICHE_BACK + 0.6, 18), (x1 - 6, NICHE_BACK + 0.6, 18), (x1 - 6, NICHE_BACK + 0.6, h - 8), (x0 + 6, NICHE_BACK + 0.6, h - 8),
             uv=((0, 0), (1, 0), (1, min(1.0, (h - 26) / (x1 - x0 - 12) * 0.5)), (0, min(1.0, (h - 26) / (x1 - x0 - 12) * 0.5))))
        box('stone', x0, WALL_BACK, h + 22, x1, WALL_FRONT, WALL_H, skip=('bottom', 'left', 'right'))  # wall over the lintel
        box('wood', x0 - 12, WALL_FRONT - 18, h, x1 + 12, WALL_FRONT + 8, h + 22, tile=96)        # timber lintel
        # the station's glyph above its alcove: a glowing diamond in a knotwork frame
        cx, gz = (x0 + x1) / 2, h + 22 + (WALL_H - 24 - h - 22) / 2
        r = min(18, (WALL_H - 24 - h - 22) / 2 - 4)
        if r > 6:
            quad('glyph%d' % ALCOVES.index((x0, x1, h, col)), (cx, WALL_FRONT + 0.7, gz - r), (cx + r * 0.75, WALL_FRONT + 0.7, gz), (cx, WALL_FRONT + 0.7, gz + r), (cx - r * 0.75, WALL_FRONT + 0.7, gz))
    # the wall's crown: blue knotwork along the top, a timber cap
    band('trim', (X0, WALL_FRONT), (X1, WALL_FRONT), WALL_H - 26, WALL_H, (0, 1.5), KNOT_BLUE)
    box('wood', X0 - 8, WALL_BACK - 8, WALL_H, X1 + 8, WALL_FRONT + 10, WALL_H + 14, tile=96)
    # ---- the canopy: carved posts, an eave beam, rafters and a shingled roof sloping to the front
    for px, py in POSTS:
        box('wood', px - 11, py - 11, TOP, px + 11, py + 11, EAVE_Z, tile=96)
        box('stone', px - 17, py - 17, TOP, px + 17, py + 17, 18, skip=('bottom',))          # footing
        band('trim', (px - 11, py + 11), (px + 11, py + 11), 150, 186, (0, 0.8), KNOT_BROWN, tile=60)  # carved collar
    box('wood', X0 - 14, POSTS[0][1] - 12, EAVE_Z, X1 + 14, POSTS[0][1] + 12, EAVE_Z + 20, tile=96)  # eave beam
    for k in range(7):
        x = X0 + 10 + k * (X1 - X0 - 20) / 6
        slanted('wood', (x, WALL_BACK - 6, WALL_H + 22), (x, EAVE_Y + 8, EAVE_Z + 16), 12, 14)
    # roof: shingles above, planks beneath
    lo, hi = (X0 - 22, EAVE_Y + 16, EAVE_Z + 24), (X1 + 22, WALL_BACK - 14, WALL_H + 34)
    Lr = math.hypot(hi[1] - lo[1], hi[2] - lo[2])
    quad('roof', (lo[0], lo[1], lo[2]), (hi[0], lo[1], lo[2]), (hi[0], hi[1], hi[2]), (lo[0], hi[1], hi[2]),
         uv=((0, 0), ((hi[0] - lo[0]) / 120, 0), ((hi[0] - lo[0]) / 120, Lr / 120), (0, Lr / 120)))
    quad('wood', (lo[0], hi[1], hi[2] - 4), (hi[0], hi[1], hi[2] - 4), (hi[0], lo[1], lo[2] - 4), (lo[0], lo[1], lo[2] - 4),
         uv=((0, 0), ((hi[0] - lo[0]) / 96, 0), ((hi[0] - lo[0]) / 96, Lr / 96), (0, Lr / 96)))
    for x in (lo[0], hi[0]):   # gable edges
        box('wood', x - 4, hi[1], lo[2] - 6, x + 4, lo[1], lo[2] + 2, tile=96)
    # banners hanging from the eave over the two inner piers
    for x0, x1 in piers()[1:-1]:
        cx, w = (x0 + x1) / 2, min(54, x1 - x0 + 10)
        y = POSTS[0][1] - 14
        quad('banner', (cx - w / 2, y, EAVE_Z - 118), (cx + w / 2, y, EAVE_Z - 118), (cx + w / 2, y, EAVE_Z - 2), (cx - w / 2, y, EAVE_Z - 2), both=True)
        box('wood', cx - w / 2 - 6, y - 3, EAVE_Z - 6, cx + w / 2 + 6, y + 3, EAVE_Z, tile=96)

def collision_boxes():
    """solid parts beyond the dais: the piers, the alcove backs and the posts (model units)"""
    out = [(x0, WALL_BACK, TOP, x1, WALL_FRONT, WALL_H) for x0, x1 in piers()]
    out += [(x0, WALL_BACK, TOP, x1, NICHE_BACK, h) for x0, x1, h, _ in ALCOVES]
    out += [(px - 11, py - 11, TOP, px + 11, py + 11, EAVE_Z) for px, py in POSTS]
    return out

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
