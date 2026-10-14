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
        UV = [(0.5 + v[0] / L, 0.5 + (v[1] + v[2]) / (L * 0.45)) for v in t]
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


def icosphere(r, sub):
    t = (1 + 5 ** 0.5) / 2
    V = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    V = [np.array(v, float) / np.linalg.norm(v) for v in V]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(sub):
        cache = {}
        def mid(i, j):
            k = (min(i, j), max(i, j))
            if k not in cache:
                m = V[i] + V[j]
                V.append(m / np.linalg.norm(m))
                cache[k] = len(V) - 1
            return cache[k]
        F2 = []
        for a_, b_, c_ in F:
            ab, bc, ca = mid(a_, b_), mid(b_, c_), mid(c_, a_)
            F2 += [(a_, ab, ca), (b_, bc, ab), (c_, ca, bc), (ab, bc, ca)]
        F = F2
    return [tuple(v * r) for v in V], F


def crystal():
    """the shrine's heart: a smooth orb of light (no facets, nothing on top), a clear shell of blue round it, and an
    aura of cold flame round both (the game turns the whole crystal, so the tongues of flame run round it)"""
    S.parts.clear()
    V, F = icosphere(22.0, 4)  # 5120 faces: smooth
    for f in F:
        tri = [V[i] for i in f]
        uv = [(0.5 + v[0] / 44.0, 0.5 + v[2] / 44.0) for v in tri]
        S.add('crystal', tri, uv, [(0, 2, 1)])
    V2, F2 = icosphere(27.0, 3)  # the shell of light round it
    for f in F2:
        tri = [V2[i] for i in f]
        uv = [(0.5 + v[0] / 54.0, 0.5 + v[2] / 54.0) for v in tri]
        S.add('halo', tri, uv, [(0, 2, 1)])
    V3, F3 = icosphere(36.0, 3)  # the aura: tongues of flame painted round a larger shell (u round it, v up it)
    for f in F3:
        tri = [V3[i] for i in f]
        us = [(math.atan2(v[1], v[0]) / (2 * math.pi)) % 1.0 for v in tri]
        if max(us) - min(us) > 0.5:  # (the seam: keep a face's u together)
            us = [u_ + 1.0 if u_ < 0.5 else u_ for u_ in us]
        uv = [(u_ * 3.0, 1.0 - (v[2] + 36.0) / 72.0) for u_, v in zip(us, tri)]
        S.add('aura', tri, uv, [(0, 2, 1)])


def tex_aura(w=512, h=256):
    """tongues of cold flame rising round the orb: brightest at their roots, flickering thin at the tips"""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u = xx / w; v = 1.0 - yy / h                       # v: 0 at the bottom of the shell, 1 at the top
    rng = np.random.default_rng(11)
    hgt = np.zeros(w, np.float32)
    for k in range(1, 7):
        hgt += np.sin(u[0] * 2 * np.pi * (k * 3) + rng.uniform(0, 6.28)) / k
    hgt = 0.62 + 0.18 * hgt / 2.0
    tongue = np.clip((hgt[None, :] - v) / 0.35, 0, 1) ** 1.3
    flick = 0.65 + 0.35 * np.sin(u * 2 * np.pi * 21 + v * 9)
    a = tongue * flick * np.clip(v / 0.25, 0, 1)
    rgb = np.dstack([0.35 + 0.5 * tongue, 0.65 + 0.3 * tongue, np.ones_like(a)])
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 1) * 255, np.clip(a * 0.85, 0, 1) * 255]).astype(np.uint8), 'RGBA')


def tex_orb(n=512):
    """the orb: white-hot at the heart, ice-blue to the rim, veins of light swirling through it"""
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    r = np.hypot(xx - 0.5, yy - 0.5) * 2
    e = np.zeros_like(r)
    rng = np.random.default_rng(3)
    for _ in range(7):
        d = rng.normal(size=2); fr = rng.uniform(6, 14)
        e += np.sin((xx * d[0] + yy * d[1]) * fr + rng.uniform(0, 6.28) + np.sin(xx * fr * 0.7) * 1.3)
    e = e / 7 * 0.5 + 0.5
    veins = np.clip(1 - np.abs(e - 0.5) * 7, 0, 1) ** 1.6
    core = np.clip(1.15 - r, 0, 1)
    rgb = np.dstack([0.35 + 0.5 * core + 0.4 * veins, 0.65 + 0.3 * core + 0.3 * veins, np.ones_like(r)])
    return Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), 'RGB')


def tex_halo(n=256):
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    sw = 0.5 + 0.5 * np.sin((xx * 9 + yy * 5) * 6.283 + np.sin(yy * 12) * 2)
    a = 0.18 + 0.22 * sw
    rgb = np.dstack([0.25 + 0.2 * sw, 0.55 + 0.25 * sw, np.ones_like(sw)])
    return Image.fromarray(np.dstack([(np.clip(rgb, 0, 1) * 255), a * 255]).astype(np.uint8), 'RGBA')


def tex_rock(n=1024, seed=5):
    """the floating rocks: dark slate, cracked and worn, carved with the Dirge's runes that glow blue in their
    grooves (diffuse, normal and glow maps)"""
    rng = random.Random(seed)
    nrng = np.random.default_rng(seed)
    def fbm(oct, base):
        out = np.zeros((n, n), np.float32); amp = 1; tot = 0
        for o in range(oct):
            g = nrng.random((base * 2 ** o + 1, base * 2 ** o + 1)).astype(np.float32)
            out += np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC), np.float32) / 255 * amp
            tot += amp; amp *= 0.55
        return out / tot
    h = fbm(7, 4) * 0.55 + fbm(5, 40) * 0.30 + fbm(3, 160) * 0.15  # broad wear, grain and grit
    # cracks: thin dark lines
    cr = Image.new('L', (n, n), 0); cd = ImageDraw.Draw(cr)
    for _ in range(14):
        x, y = rng.uniform(0, n), rng.uniform(0, n); a = rng.uniform(0, 6.28)
        pts = [(x, y)]
        for _ in range(10):
            a += rng.uniform(-0.6, 0.6); x += math.cos(a) * n * 0.04; y += math.sin(a) * n * 0.04; pts.append((x, y))
        cd.line(pts, fill=255, width=2)
    crack = np.asarray(cr.filter(ImageFilter.GaussianBlur(1)), np.float32) / 255
    # carved runes in bands across the stone
    gl = Image.new('L', (n, n), 0); gd = ImageDraw.Draw(gl)
    strokes = [[(0, -1, 0, 1), (0, -0.3, 0.6, -0.9)], [(-0.5, 1, 0, -1), (0.5, 1, 0, -1), (-0.3, 0.3, 0.3, 0.3)], [(0, -1, 0, 1), (-0.6, 0, 0.6, 0)],
               [(-0.6, -1, 0.6, 1), (0.6, -1, -0.6, 1)], [(0, -1, 0, 1), (0, 0.1, 0.6, 0.9)], [(-0.6, -0.8, 0.6, -0.8), (0, -0.8, 0, 1)],
               [(-0.5, -1, -0.5, 1), (-0.5, 0, 0.5, -0.6), (-0.5, 0, 0.5, 0.6)], [(0, -1, 0.6, 0), (0.6, 0, 0, 1), (0, 1, -0.6, 0), (-0.6, 0, 0, -1)]]
    for row in range(3):
        cy = n * (0.25 + row * 0.25)
        for col in range(7):
            cx = n * (0.09 + col * 0.137)
            sz = n * 0.035
            for x0, y0, x1, y1 in strokes[rng.randrange(len(strokes))]:
                gd.line([(cx + x0 * sz, cy + y0 * sz * 1.4), (cx + x1 * sz, cy + y1 * sz * 1.4)], fill=255, width=max(3, n // 170))
        gd.line([(n * 0.03, cy + n * 0.075), (n * 0.97, cy + n * 0.075)], fill=180, width=max(2, n // 340))  # a carved line under each band
    glyph = np.asarray(gl.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
    height = h * 0.8 - crack * 0.5 - glyph * 0.6
    base = np.array([62, 60, 74], np.float32)
    rgb = base[None, None] * (0.55 + 0.75 * h[..., None]) * (1 - 0.55 * crack[..., None]) * (1 - 0.5 * glyph[..., None])
    rgb += np.array([30, 60, 110]) * glyph[..., None] * 0.5
    gy, gx = np.gradient(height * 6.0)
    nrm = np.dstack([-gx, -gy, np.ones_like(gx)]); nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    nimg = Image.fromarray(np.dstack([(nrm[..., 0] * 0.5 + 0.5) * 255, (nrm[..., 1] * 0.5 + 0.5) * 255, (nrm[..., 2] * 0.5 + 0.5) * 255, np.full_like(gx, 90)]).astype(np.uint8), 'RGBA')
    glow = np.clip(glyph * 1.0 + crack * 0.35, 0, 1)
    gimg = Image.fromarray((np.dstack([0.35 * glow, 0.7 * glow, 1.0 * glow]) * 255).astype(np.uint8), 'RGB')
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB'), nimg, gimg


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
    # (collidebox2: an unseen block round the orb, high over your head, so looking at the shrine's heart finds the shrine)
    convert('shrine', [f'collidefrustum {-R} {-R} {R} {R} {TOP} {BOTTOM} {GROW}', 'collidebox2 -34 -34 140 34 34 212'],
            [floor, cobble, trim, rune, runes, pool])
    shards()
    vg = S.save(Image.new('RGB', (32, 32), (130, 200, 255)), 'vein_g.dds')
    rkd, rkn, rkg = tex_rock()
    rock_mat = f'mat shard diffuse={S.save(rkd, "shrock_d.dds")} normal={S.save(rkn, "shrock_n.dds", True)} glow={S.save(rkg, "shrock_g.dds")} emit=0.35,0.7,1.0 emitmult=1.8 spec=0.35 gloss=40 doublesided=1'
    convert('shards', [], [rock_mat, f'mat vein diffuse={rune_d} normal={flat} glow={vg} emit=0.4,0.75,1.0 emitmult=3.0 spec=0.3 doublesided=1'])
    crystal()
    cd = S.save(tex_orb(), 'orb_d.dds')
    hd = S.save(tex_halo(), 'orbhalo_d.dds', True)
    ad = S.save(tex_aura(), 'orbaura_d.dds', True)
    convert('crystal', [], [f'mat crystal diffuse={cd} normal={flat} glow={cd} emit=0.7,0.9,1.0 emitmult=4.0 spec=1.0 gloss=120 doublesided=1',
                            f'mat halo diffuse={hd} normal={flat} glow={hd} emit=0.3,0.6,1.0 emitmult=2.5 alpha=1 doublesided=1',
                            f'mat aura diffuse={ad} normal={flat} glow={ad} emit=0.4,0.75,1.0 emitmult=3.2 alpha=1 doublesided=1'])
    print('crystal centre at z', CRYSTAL_Z)


main()
