"""The Shrine of the Dirge (Noah's picture): a round platform of dark stone engraved with rings of the curse's runes
glowing blue, and above it a crystal held in a slow ring of broken, floating rock shards with blue light in their
cracks. Three models, so the middle can move:
    shrine.nif    the platform (walkable, collision), its rune rings and the pool of light at its heart
    crystal.nif   the crystal (turned by the game: the shrine's living heart; faster while the Coffer rolls)
    shards.nif    the ring of floating shards (turned slowly round the crystal)
Skyrim's own stone textures by path. Origin = ground under the centre, +Z up; the crystal and the shards are built
round their own centre (the dressing sheet lifts them to CRYSTAL_Z).
    python tools/shrine.py <out Data dir>"""
import math, pathlib, random, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import stand as S

OUT = pathlib.Path(sys.argv[1]); S.OUT = OUT
R, TOP, BOTTOM, GROW = 160, 16, -60, 70
SEG = 32
CRYSTAL_Z = 150          # the crystal's centre above the ground (sheets/dressing.json: shrine_crystal / shrine_shards z)
BLUE = (0.35, 0.65, 1.0)


def circle(r, z, seg=SEG):
    return [(r * math.cos(2 * math.pi * k / seg), r * math.sin(2 * math.pi * k / seg), z) for k in range(seg)]


def platform():
    S.parts.clear()
    top = circle(R, TOP)
    S.add('floor', [(0, 0, TOP)] + top, [(0, 0)] + [(x / 160, y / 160) for x, y, _ in top], [(0, 1 + k, 1 + (k + 1) % SEG) for k in range(SEG)])
    under, bot = circle(R, TOP - 14), circle(R + GROW, BOTTOM)
    for k in range(SEG):
        a, b = top[k], top[(k + 1) % SEG]
        c, d = under[k], under[(k + 1) % SEG]
        e, f = bot[(k + 1) % SEG], bot[k]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        S.quad('trim', c, d, b, a, uv=((0, S.KNOT_BLUE[0]), (L / 120, S.KNOT_BLUE[0]), (L / 120, S.KNOT_BLUE[1]), (0, S.KNOT_BLUE[1])))
        Lr = math.hypot(d[0] - c[0], d[1] - c[1])
        S.add('cobble', [c, d, e, f], [(0, 0), (Lr / 140, 0), (Lr / 140, 1.2), (0, 1.2)], [(0, 2, 1), (0, 3, 2)])
    # the engraved rings: thin glowing lines, and a band of runes between them
    for r0, r1 in ((150, 153), (126, 129), (82, 85), (44, 47)):
        S.ring('rune', 0, 0, TOP + 0.6, r0, r1, seg=96)
    S.ring('runes', 0, 0, TOP + 0.7, 92, 120, seg=96)
    S.ring('runes', 0, 0, TOP + 0.7, 52, 74, seg=64)
    # the pool of light right under the crystal
    S.disc('pool', 0, 0, TOP + 0.9, 30, seg=32, tile=60)


def shard_mesh(mat, cx, cy, cz, yaw, tilt, length, width, depth, seed):
    """a slab of broken rock: an irregular outline extruded, its edges chipped; laid along its local x"""
    rng = random.Random(seed)
    n = 9
    outline = []
    for i in range(n):
        a = 2 * math.pi * i / n
        rx = length * 0.5 * (0.75 + rng.uniform(0, 0.35))
        ry = width * 0.5 * (0.65 + rng.uniform(0, 0.45))
        outline.append((math.cos(a) * rx, math.sin(a) * ry))
    def place(x, y, z):
        # tilt about local y, then yaw about z, then move
        xt, zt = x * math.cos(tilt) - z * math.sin(tilt), x * math.sin(tilt) + z * math.cos(tilt)
        return (cx + xt * math.cos(yaw) - y * math.sin(yaw), cy + xt * math.sin(yaw) + y * math.cos(yaw), cz + zt)
    front = [place(x, y * 0.25, z) for x, z in outline for y in [depth]]
    back = [place(x, -depth * 0.25 - rng.uniform(0, 3), z * 0.92) for x, z in outline]
    # faces: the slab lies in local xz (thin in y)
    fv = [place(0, depth * 0.25, 0)] + front
    S.add(mat, fv, [(0.5, 0.5)] + [(0.5 + x / length, 0.5 + z / width) for x, z in outline], [(0, 1 + i, 1 + (i + 1) % n) for i in range(n)])
    bv = [place(0, -depth * 0.3, 0)] + back
    S.add(mat, bv, [(0.5, 0.5)] + [(0.5 + x / length, 0.5 + z / width) for x, z in outline], [(0, 1 + (i + 1) % n, 1 + i) for i in range(n)])
    for i in range(n):
        a, b = front[i], front[(i + 1) % n]
        c, d = back[(i + 1) % n], back[i]
        S.quad(mat, a, d, c, b, uv=((0, 0), (0, 0.15), (0.3, 0.15), (0.3, 0)))
    # a blue vein of the crystal's light across the face
    p0 = place(-length * 0.28, depth * 0.25 + 0.6, -width * 0.05)
    p1 = place(length * 0.05, depth * 0.25 + 0.6, width * 0.08)
    p2 = place(length * 0.30, depth * 0.25 + 0.6, -width * 0.02)
    for (u, v) in ((p0, p1), (p1, p2)):
        w = 1.6
        S.quad('vein', (u[0], u[1], u[2] - w), (v[0], v[1], v[2] - w), (v[0], v[1], v[2] + w), (u[0], u[1], u[2] + w), both=True)


def rock(mat, cx, cy, cz, yaw, tilt, L, seed):
    """a broken shard of rock: a jagged closed hull (no gaps to see into), long and flat like the picture's,
    chipped at the ends; its broad faces carry the blue veins"""
    from scipy.spatial import ConvexHull
    rng = np.random.default_rng(seed)
    pts = []
    for _ in range(48):
        u = rng.uniform(-1, 1)
        th = rng.uniform(0, 2 * math.pi)
        taper = 1 - abs(u) ** 1.8 * 0.55
        pts.append((u * L * 0.5, math.cos(th) * L * 0.2 * taper * rng.uniform(0.75, 1.1), math.sin(th) * L * 0.09 * taper * rng.uniform(0.7, 1.1)))
    pts = np.array(pts)
    hull = ConvexHull(pts)
    tris = pts[hull.simplices]
    ctr = pts.mean(0)
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    flip = ((tris.mean(1) - ctr) * n).sum(1) < 0
    tris[flip] = tris[flip][:, ::-1]
    def place(v):
        x, y, z = v
        xt, zt = x * math.cos(tilt) - z * math.sin(tilt), x * math.sin(tilt) + z * math.cos(tilt)
        return (cx + xt * math.cos(yaw) - y * math.sin(yaw), cy + xt * math.sin(yaw) + y * math.cos(yaw), cz + zt)
    for t in tris:
        V = [place(v) for v in t]
        UV = [(0.5 + v[0] / L, 0.5 + v[1] / (L * 0.4)) for v in t]
        S.add(mat, V, UV, [(0, 1, 2)])
    # veins: thin glowing strips over the broad faces (both sides), following the rock's length
    for side in (1, -1):
        zf = side * L * 0.072  # just under the broad face's height near the middle: the vein sits in the stone
        x0 = -L * 0.2
        y0 = rng.uniform(-L * 0.03, L * 0.03)
        for k in range(3):
            x1 = x0 + L * 0.13
            y1 = y0 + rng.uniform(-L * 0.04, L * 0.04)
            w = 1.3
            a0, a1 = place((x0, y0 - w, zf)), place((x1, y1 - w, zf))
            b1, b0 = place((x1, y1 + w, zf)), place((x0, y0 + w, zf))
            S.quad('vein', a0, a1, b1, b0, both=True)
            x0, y0 = x1, y1


def shards():
    S.parts.clear()
    # (angle, distance, height from the crystal's centre, tilt, length): standing on end round the crystal like the
    # picture's, none low enough to reach the floor (the lowest tip stays ~60 above it, bobbing included)
    ring = [(0.0, 70, 70, 1.25, 120), (0.9, 78, -20, 1.45, 105), (1.8, 66, 55, 1.05, 95), (2.7, 82, -35, 1.6, 115), (3.6, 72, 40, 1.35, 110),
            (4.5, 76, -10, 1.15, 100), (5.4, 64, 85, 1.5, 85), (6.0, 58, -45, 1.3, 70)]
    for i, (a, d, z, tilt, L) in enumerate(ring):
        rock('shard', math.cos(a) * d, math.sin(a) * d, z, a + math.pi / 2, tilt, L, seed=i + 3)
    for i in range(8):  # little chips drifting with them
        a = i * 0.79 + 0.3
        rock('shard', math.cos(a) * (40 + i * 5), math.sin(a) * (40 + i * 5), -20 + i * 12, a, i * 0.5, 16 + (i % 3) * 5, seed=60 + i)


def crystal():
    """a brilliant-cut gem (the picture's heart of light): a table, a crown of star and kite facets, a girdle,
    and a pavilion of long facets to a point; 16-fold"""
    S.parts.clear()
    n = 16
    rg, rt = 26.0, 14.0           # girdle and table radius
    hc, hp, hg = 12.0, 30.0, 2.5  # crown height, pavilion depth, girdle half-thickness
    def ring_(r, z, off=0.0, m=n):
        return [(r * math.cos(2 * math.pi * (k + off) / m), r * math.sin(2 * math.pi * (k + off) / m), z) for k in range(m)]
    table = ring_(rt, hc, 0.5)
    star = ring_((rt + rg) / 2 * 1.02, hc * 0.55, 0.0)
    gtop = ring_(rg, hg)
    gbot = ring_(rg, -hg)
    pmid = ring_(rg * 0.45, -hp * 0.62, 0.5)
    tip = (0.0, 0.0, -hp)
    top = (0.0, 0.0, hc)
    for k in range(n):
        k1 = (k + 1) % n
        S.add('crystal', [top, table[k], table[k1]], [(0.5, 0.5), (0, 0), (1, 0)], [(0, 1, 2)])                  # table
        S.add('crystal', [table[k], star[k1], table[k1]], [(0, 0), (0.5, 1), (1, 0)], [(0, 1, 2)])               # star facets
        S.add('crystal', [table[k], star[k], star[k1]], [(0, 0), (0, 1), (1, 1)], [(0, 1, 2)])
        S.add('crystal', [star[k], gtop[k], gtop[k1]], [(0, 0), (0, 1), (1, 1)], [(0, 1, 2)])                    # kite / upper girdle
        S.add('crystal', [star[k], gtop[k1], star[k1]], [(0, 0), (1, 1), (1, 0)], [(0, 1, 2)])
        S.add('crystal', [gtop[k], gbot[k], gbot[k1]], [(0, 0), (0, 1), (1, 1)], [(0, 1, 2)])                    # girdle
        S.add('crystal', [gtop[k], gbot[k1], gtop[k1]], [(0, 0), (1, 1), (1, 0)], [(0, 1, 2)])
        S.add('crystal', [gbot[k], pmid[k], gbot[k1]], [(0, 0), (0.5, 1), (1, 0)], [(0, 1, 2)])                  # lower girdle
        S.add('crystal', [gbot[k], pmid[(k - 1) % n], pmid[k]], [(0, 0), (0, 1), (1, 1)], [(0, 1, 2)])
        S.add('crystal', [pmid[k], tip, pmid[k1]], [(0, 0), (0.5, 1), (1, 0)], [(0, 1, 2)])                      # pavilion
    # a crown of three bright spikes over it (the picture's star of light)
    for ang, hgt in ((0.0, 26), (2.1, 20), (4.2, 23)):
        bx, by = math.cos(ang) * 6, math.sin(ang) * 6
        base = [(bx + 4 * math.cos(2 * math.pi * j / 4), by + 4 * math.sin(2 * math.pi * j / 4), hc - 1) for j in range(4)]
        tipp = (bx * 1.5, by * 1.5, hc + hgt)
        for j in range(4):
            S.add('crystal', [base[j], base[(j + 1) % 4], tipp], [(0, 0), (1, 0), (0.5, 1)], [(0, 1, 2)])


def tex_runes(w=1024, h=256):
    """the band of runes: glyph grooves with blue light in them, on a transparent ground"""
    rnd = random.Random(7)
    mask = Image.new('L', (w, h), 0)
    dr = ImageDraw.Draw(mask)
    strokes = [[(0, -1, 0, 1), (0, -0.3, 0.6, -0.9)], [(-0.5, 1, 0, -1), (0.5, 1, 0, -1), (-0.3, 0.3, 0.3, 0.3)], [(0, -1, 0, 1), (-0.6, 0, 0.6, 0)],
               [(-0.6, -1, 0.6, 1), (0.6, -1, -0.6, 1)], [(0, -1, 0, 1), (0, 0.1, 0.6, 0.9)], [(-0.6, -0.8, 0.6, -0.8), (0, -0.8, 0, 1)],
               [(-0.5, -1, -0.5, 1), (-0.5, 0, 0.5, -0.6), (-0.5, 0, 0.5, 0.6)], [(0, -1, 0.6, 0), (0.6, 0, 0, 1), (0, 1, -0.6, 0), (-0.6, 0, 0, -1)]]
    n = 6
    for i in range(n):
        cx, cy = (i + 0.5) * w / n, h / 2
        sx, sy = w / n * 0.3, h * 0.34
        for x0, y0, x1, y1 in strokes[(i * 3 + 1) % len(strokes)]:
            dr.line([(cx + x0 * sx, cy + y0 * sy), (cx + x1 * sx, cy + y1 * sy)], fill=255, width=int(h * 0.07))
    soft = mask.filter(ImageFilter.GaussianBlur(3))
    core = mask.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.GaussianBlur(2))
    a = soft.point(lambda v: 255 if v > 40 else 0)
    sm = np.asarray(soft, np.float32) / 255
    cm = np.asarray(core, np.float32) / 255
    rgb = np.dstack([0.04 + 0.35 * cm, 0.08 + 0.6 * cm, 0.14 + 0.9 * cm]) * (0.6 + 0.4 * sm[..., None])
    d = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), 'RGB')
    d.putalpha(a)
    g = Image.fromarray((np.clip(np.dstack([0.35 * cm, 0.65 * cm, 1.0 * cm]), 0, 1) * 255).astype(np.uint8), 'RGB')
    return d, g


def tex_crystal(n=256):
    """faceted ice-blue light, whiter at the heart"""
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    fac = (np.sin(xx * 23) * np.cos(yy * 17) * 0.5 + 0.5) * 0.25
    base = np.dstack([0.55 + fac, 0.80 + fac * 0.6, np.ones_like(xx)])
    return Image.fromarray((np.clip(base, 0, 1) * 255).astype(np.uint8), 'RGB')


def convert(name, extra_job, mats):
    work = OUT.parent / 'shrine'
    work.mkdir(parents=True, exist_ok=True)
    obj = work / f'{name}.obj'
    S.write_obj(obj)
    out = OUT / 'Meshes' / 'UndyingNights' / 'shrine' / f'{name}.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {out}'] + extra_job + [m for m in mats if m.split()[1] in S.parts]
    jf = work / f'{name}.job'
    jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([S.CONV, str(jf)], capture_output=True, text=True)
    print(name, '|', r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit('modelconv failed')


def main():
    flat = S.save(Image.new('RGBA', (64, 64), (128, 128, 255, 160)), 'flat_n.dds', True)
    rune_d = S.save(Image.new('RGB', (64, 64), (90, 160, 255)), 'srune_d.dds')
    rune_g = S.save(Image.new('RGB', (64, 64), (110, 180, 255)), 'srune_g.dds')
    rd, rg = tex_runes()
    runes = f'mat runes diffuse={S.save(rd, "sruneband_d.dds", True)} normal={flat} glow={S.save(rg, "sruneband_g.dds")} emit={BLUE[0]},{BLUE[1]},{BLUE[2]} emitmult=2.4 alpha=1 spec=0.3'
    rune = f'mat rune diffuse={rune_d} normal={flat} glow={rune_g} emit={BLUE[0]},{BLUE[1]},{BLUE[2]} emitmult=2.6 spec=0.5'
    pool = f'mat pool diffuse={rune_d} normal={flat} glow={rune_g} emit=0.5,0.8,1.0 emitmult=3.5 spec=0.2'
    W = S.W
    stone = lambda m, spec: f'mat {m} diffuse={W}{S.TEX["stone"][0]}.dds normal={W}{S.TEX["stone"][1]}.dds spec={spec} gloss=30'
    floor = f'mat floor diffuse={W}{S.TEX["floor"][0]}.dds normal={W}{S.TEX["floor"][1]}.dds spec=0.3 gloss=30'
    cobble = f'mat cobble diffuse={W}{S.TEX["cobble"][0]}.dds normal={W}{S.TEX["cobble"][1]}.dds spec=0.25 gloss=30'
    trim = f'mat trim diffuse={W}{S.TEX["trim"][0]}.dds normal={W}{S.TEX["trim"][1]}.dds spec=0.25 gloss=30'
    platform()
    convert('shrine', [f'collidefrustum {-R} {-R} {R} {R} {TOP} {BOTTOM} {GROW}'], [floor, cobble, trim, rune, runes, pool])
    shards()
    vg = S.save(Image.new('RGB', (32, 32), (130, 200, 255)), 'vein_g.dds')
    convert('shards', [], [stone('shard', 0.35) + ' doublesided=1', f'mat vein diffuse={rune_d} normal={flat} glow={vg} emit=0.4,0.75,1.0 emitmult=3.0 spec=0.3 doublesided=1'])
    crystal()
    cd = S.save(tex_crystal(), 'crystal_d.dds')
    convert('crystal', [], [f'mat crystal diffuse={cd} normal={flat} glow={cd} emit=0.7,0.9,1.0 emitmult=4.0 spec=1.0 gloss=120 doublesided=1'])
    print('crystal centre at z', CRYSTAL_Z)


main()
