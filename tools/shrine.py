"""The Echo Altar (Noah's picture): a round platform of dark stone engraved with rings of the curse's runes
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
import rockart as RA
import altarart as AA

OUT = pathlib.Path(sys.argv[1]); S.OUT = OUT
R, TOP, BOTTOM, GROW = 160, 16, -60, 70
LIFT = 21.5             # the dais stands this much above its model's ground (the floor's top at TOP + LIFT); its collision goes with it
SEG = 32
CRYSTAL_Z = 150          # the crystal's centre above the ground (sheets/dressing.json: shrine_crystal / shrine_shards z)
BLUE = (0.35, 0.65, 1.0)


def circle(r, z, seg=SEG):
    return [(r * math.cos(2 * math.pi * k / seg), r * math.sin(2 * math.pi * k / seg), z) for k in range(seg)]


def platform():
    S.parts.clear()
    top = circle(R, TOP)
    # the floor: one disc carrying the whole engraved design (tools/altarart.py), its texture spread edge to edge;
    # finely divided so the rim is round
    top = circle(R, TOP, 128)
    S.add('floor', [(0, 0, TOP)] + top, [(0.5, 0.5)] + [(0.5 + x / (2 * R), 0.5 + y / (2 * R)) for x, y, _ in top], [(0, 1 + k, 1 + (k + 1) % 128) for k in range(128)])
    top = circle(R, TOP)
    under, bot = circle(R, TOP - 14), circle(R + GROW, BOTTOM)
    for k in range(SEG):
        a, b = top[k], top[(k + 1) % SEG]
        c, d = under[k], under[(k + 1) % SEG]
        e, f = bot[(k + 1) % SEG], bot[k]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        S.quad('trim', c, d, b, a, uv=((0, S.KNOT_BROWN[0]), (L / 120, S.KNOT_BROWN[0]), (L / 120, S.KNOT_BROWN[1]), (0, S.KNOT_BROWN[1])))
        Lr = math.hypot(d[0] - c[0], d[1] - c[1])
        S.add('cobble', [c, d, e, f], [(0, 0), (Lr / 140, 0), (Lr / 140, 1.2), (0, 1.2)], [(0, 2, 1), (0, 3, 2)])
    # (the engraved rings, runes and the sunburst at the heart are all in the floor's texture now: gold inlay that glows)


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
    chipped at the ends"""
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
        UV = RA.fix_seam([RA.rock_uv(v[0], v[1], v[2], L) for v in t])  # (a sleeve round the rock: its bracelets wrap each end)
        S.add(mat, V, UV, [(0, 1, 2)])


def shards():
    S.parts.clear()
    # (angle, distance, height from the crystal's centre, tilt, length): standing on end round the crystal like the
    # picture's, none low enough to reach the floor (the lowest tip stays ~60 above it, bobbing included)
    ring = [(0.0, 70, 70, 1.25, 120), (0.9, 78, -20, 1.45, 105), (1.8, 66, 55, 1.05, 95), (2.7, 82, -35, 1.6, 115), (3.6, 72, 40, 1.35, 110),
            (4.5, 76, -10, 1.15, 100), (5.4, 64, 85, 1.5, 85), (6.0, 58, -45, 1.3, 70)]
    for i, (a, d, z, tilt, L) in enumerate(ring):
        rock(f'shard{i:02d}', math.cos(a) * d, math.sin(a) * d, z, a + math.pi / 2, tilt, L, seed=i + 3)  # (each its own shape: each sways)
    for i in range(8):  # little chips drifting with them
        a = i * 0.79 + 0.3
        rock(f'shard{i + 8:02d}', math.cos(a) * (40 + i * 5), math.sin(a) * (40 + i * 5), -20 + i * 12, a, i * 0.5, 16 + (i % 3) * 5, seed=60 + i)


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
    """the shrine's heart: a smooth orb of light, nothing round it"""
    S.parts.clear()
    V, F = icosphere(22.0, 4)  # 5120 faces: smooth
    for f in F:
        tri = [V[i] for i in f]
        uv = [(0.5 + v[0] / 44.0, 0.5 + v[2] / 44.0) for v in tri]
        S.add('crystal', tri, uv, [(0, 2, 1)])
    # (its aura of cold flame: shells of rising flame round it, added by aura() - they turn and bob with it, and the
    # floating rocks pass in front of them)


def tex_flame(w=256, h=512, seed=21):
    """tongues of cold flame, seamless both ways (the shells scroll it upward): long thin licks with dark gaps"""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = xx / w, yy / h
    rng = np.random.default_rng(seed)
    f = np.zeros((h, w), np.float32)
    for _ in range(26):
        kx, ky = int(rng.integers(3, 14)), int(rng.integers(1, 4))
        a = 1.0 / (kx * 0.35 + ky)
        f += a * np.sin(2 * np.pi * (kx * u + ky * v * rng.choice([-1, 1])) + rng.uniform(0, 6.28) + 0.8 * np.sin(2 * np.pi * (2 * v + u)))
    f = (f - f.mean()) / f.std()
    lick = np.clip((f - 0.1) / 1.4, 0, 1) ** 1.5
    fine = 0.75 + 0.25 * np.sin(2 * np.pi * (11 * u + 3 * v) + 2 * np.sin(2 * np.pi * 5 * v))
    a = np.clip(lick * fine, 0, 1)
    rgb = np.dstack([0.55 + 0.45 * a, 0.80 + 0.20 * a, np.ones_like(a)])
    return Image.fromarray(np.dstack([rgb * 255, a * 255]).astype(np.uint8), 'RGBA')


def fxjob(name, job):
    work = OUT.parent / 'shrine'
    jf = work / f'{name}.job'
    jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(name, '|', r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit('fxwrite failed')


def quad(job, x0, x1, y0, y1, z, cu=1 / 16, cv=1 / 8):
    """a flipbook quad in a billboard's own plane (XY; its Z axis is turned to the camera: z > 0 sits nearer you)"""
    for x, y, u, v in [(x0, y1, 0, 0), (x1, y1, cu, 0), (x1, y0, cu, cv), (x0, y0, 0, cv)]:
        job.append(f'v {x:.3f} {y:.3f} {z:.3f} {u} {v}')
    job += ['t 0 1 2', 't 0 2 3']


def aura(nif, fire, swirl):
    """the orb, alive: its own light swirling over it (tools/orbfx.py orb_swirl, on a quad just in front of it) and
    purple fire made of magic coming out of it (tools/magicfire.py: a skin of flame clinging round its edge that
    climbs into a plume over it) - looping flipbooks on quads that always face you, added to crystal.nif with fxwrite
    (unlit additive effect shapes): they bob with the orb, and the rocks pass in front of them."""
    job = [f'base {nif}', f'out {nif}']
    R = 22.0  # the orb's radius
    job.append(f'shape orbswirl tex={swirl} color=1,1,1,1 scale=1.3 blend=add billboard=2 flip=16x8 fps=24 phase=0')
    quad(job, -R * 1.02, R * 1.02, -R * 1.02, R * 1.02, R + 1.5)  # (in front of the ball's face)
    # the fire: ORB_R (0.36) of a frame's half-size, its centre ORB_Y (-0.30) below the frame's middle; set a little in
    # front of the orb's centre so the flames that cling to its edge lie over the rim, as if they came out of it
    for i, (sc, ph, col, br) in enumerate([(1.0, 0.0, (1.0, 1.0, 1.0, 1.0), 1.5), (1.12, 2.6, (0.85, 0.75, 1.0, 0.5), 1.2)]):
        hh = R / 0.36 * sc
        lift = 0.30 * hh
        job.append(f'shape aura{i} tex={fire} color={",".join(str(c) for c in col)} scale={br} blend=add billboard=2 flip=16x8 fps=24 phase={ph}')
        quad(job, -hh, hh, -hh + lift, hh + lift, 10.0 - 3.0 * i)
    fxjob('aura', job)


def motes(nif, tex):
    """motes of light rising from the shrine's pool up to its orb, spiralling in (tools/orbfx.py motes), on a tall
    quad that always faces you - added to shrine.nif (it stands still; the orb's quads bob with the orb)"""
    job = [f'base {nif}', f'out {nif}']
    job.append(f'shape motes tex={tex} color=1,1,1,1 scale=1.4 blend=add billboard=2 flip=16x8 fps=24 phase=0 at=0,0,95')
    quad(job, -40.0, 40.0, -80.0, 80.0, 0.0)
    # (the billboard's node stands between the pool and the orb, at=: it turns about it)
    fxjob('motes', job)


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


def tex_orb(n=2048):
    """the orb: white-hot at the heart, ice-blue to the rim, veins of light swirling through it"""
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    r = np.hypot(xx - 0.5, yy - 0.5) * 2
    e = np.zeros_like(r)
    rng = np.random.default_rng(3)
    for _ in range(7):
        d = rng.normal(size=2); fr = rng.uniform(6, 14)
        e += np.sin((xx * d[0] + yy * d[1]) * fr + rng.uniform(0, 6.28) + np.sin(xx * fr * 0.7) * 1.3)
    e = e / 7 * 0.5 + 0.5
    veins = np.asarray(Image.fromarray((np.clip(1 - np.abs(e - 0.5) * 6, 0, 1) ** 1.4 * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(n / 256)), np.float32) / 255
    e2 = np.zeros_like(r)                                     # a finer web of filaments between the veins (the 2K's detail)
    for _ in range(7):
        d = rng.normal(size=2); fr = rng.uniform(16, 34)
        e2 += np.sin((xx * d[0] + yy * d[1]) * fr + rng.uniform(0, 6.28) + np.sin(yy * fr * 0.6) * 1.2)
    e2 = e2 / 7 * 0.5 + 0.5
    veins = np.maximum(veins, np.clip(1 - np.abs(e2 - 0.5) * 14, 0, 1) ** 2.0 * 0.6)
    core = np.clip(1.15 - r, 0, 1) ** 1.2
    rgb = np.dstack([0.30 + 0.45 * core + 0.35 * veins, 0.58 + 0.32 * core + 0.28 * veins, 0.95 + 0.05 * core])
    return Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), 'RGB')


def tex_halo(n=256):
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    sw = 0.5 + 0.5 * np.sin((xx * 9 + yy * 5) * 6.283 + np.sin(yy * 12) * 2)
    a = 0.18 + 0.22 * sw
    rgb = np.dstack([0.25 + 0.2 * sw, 0.55 + 0.25 * sw, np.ones_like(sw)])
    return Image.fromarray(np.dstack([(np.clip(rgb, 0, 1) * 255), a * 255]).astype(np.uint8), 'RGBA')


def tex_rock(n=1024, seed=5):
    """the floating rocks: dark slate, cracked and worn, carved with the Curse's runes that glow blue in their
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
    glyph = np.asarray(gl.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
    height = h * 0.8 - crack * 0.5 - glyph * 0.6
    base = np.array([62, 60, 74], np.float32)
    rgb = base[None, None] * (0.55 + 0.75 * h[..., None]) * (1 - 0.55 * crack[..., None]) * (1 - 0.5 * glyph[..., None])
    gy, gx = np.gradient(height * 6.0)
    nrm = np.dstack([-gx, -gy, np.ones_like(gx)]); nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    nimg = Image.fromarray(np.dstack([(nrm[..., 0] * 0.5 + 0.5) * 255, (nrm[..., 1] * 0.5 + 0.5) * 255, (nrm[..., 2] * 0.5 + 0.5) * 255, np.full_like(gx, 90)]).astype(np.uint8), 'RGBA')
    glow = np.clip(glyph * 1.0, 0, 1)  # (only the runes glow: the cracks stay dark stone)
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
    out = OUT / 'Meshes' / 'CurseOfAlduin' / 'shrine' / f'{name}.nif'
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
    fd, fn, fg, _ = AA.floor_maps(2048)
    floor = (f'mat floor diffuse={S.save(fd, "altarfloor_d.dds", mips=True)} normal={S.save(fn.convert("RGBA"), "altarfloor_n.dds", True, mips=True)} '
             f'glow={S.save(fg, "altarfloor_g.dds", mips=True)} emit=1.0,0.78,0.42 emitmult=1.6 spec=0.45 gloss=45')
    cobble = f'mat cobble diffuse={W}{S.TEX["cobble"][0]}.dds normal={W}{S.TEX["cobble"][1]}.dds spec=0.25 gloss=30'
    trim = f'mat trim diffuse={W}{S.TEX["trim"][0]}.dds normal={W}{S.TEX["trim"][1]}.dds spec=0.25 gloss=30'
    platform()
    PLATFORM_ONLY = len(sys.argv) > 2 and sys.argv[2] == 'platform'
    # (collidebox2: an unseen block round the orb, high over your head, so looking at the shrine's heart finds the shrine)
    convert('shrine', ['origin 1', f'offset 0 0 {LIFT}', f'collidefrustum {-R} {-R} {R} {R} {TOP + LIFT} {BOTTOM + LIFT} {GROW}', 'collidebox2 -34 -34 140 34 34 212'],
            [floor, cobble, trim, rune, runes, pool])
    if PLATFORM_ONLY:  # (the floor alone: the motes go back on it, the rest stays as built)
        motes(OUT / 'Meshes' / 'CurseOfAlduin' / 'shrine' / 'shrine.nif', 'CurseOfAlduin\\stand\\shrine_motes.dds')
        return
    shards()
    vg = S.save(Image.new('RGB', (32, 32), (130, 200, 255)), 'vein_g.dds')
    rkd, rkn, rkg = RA.rock_maps(2048)  # (2K, shared with the menu's rocks: tools/rockart.py)
    rock_tex = f'diffuse={S.save(rkd, "shrock_d.dds", mips=True)} normal={S.save(rkn, "shrock_n.dds", True, mips=True)} glow={S.save(rkg, "shrock_g.dds", mips=True)} emit=0.35,0.7,1.0 emitmult=1.8 spec=0.35 gloss=40 doublesided=1'
    convert('shards', [], [f'mat shard{i:02d} {rock_tex}' for i in range(16)])
    # each rock sways on its own as the ring turns, and the runes cut in them breathe blue light
    shards_nif = OUT / 'Meshes' / 'CurseOfAlduin' / 'shrine' / 'shards.nif'
    jf = OUT.parent / 'shrine' / 'shards_anim.job'
    jf.write_text(f'in {shards_nif}\nout {shards_nif}\nsway shard 4.5 5.5 7\npulse shard 0.25 3.2 2.6\n')
    r = subprocess.run(['/home/claude/tools/nifanim', str(jf)], capture_output=True, text=True)
    print('shards anim |', r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit('nifanim failed')
    crystal()
    cd = S.save(tex_orb(), 'orb_d.dds', mips=True)  # (2K, mipmapped)
    convert('crystal', [], [f'mat crystal diffuse={cd} normal={flat} glow={cd} emit=0.7,0.9,1.0 emitmult=3.2 spec=1.0 gloss=120 doublesided=1'])
    fire = OUT.parent / 'shrine' / 'magicfire.png'
    subprocess.run([sys.executable, str(pathlib.Path(__file__).parent / 'magicfire.py'), str(fire)], check=True)
    subprocess.run([sys.executable, str(pathlib.Path(__file__).parent / 'orbfx.py'), str(fire.parent)], check=True)
    aura(OUT / 'Meshes' / 'CurseOfAlduin' / 'shrine' / 'crystal.nif', S.save(Image.open(fire).convert('RGBA'), 'aura_fire.dds', True, mips=True),
         S.save(Image.open(fire.parent / 'orb_swirl.png').convert('RGBA'), 'orb_swirl.dds', True, mips=True))
    motes(OUT / 'Meshes' / 'CurseOfAlduin' / 'shrine' / 'shrine.nif', S.save(Image.open(fire.parent / 'motes.png').convert('RGBA'), 'shrine_motes.dds', True, mips=True))
    print('crystal centre at z', CRYSTAL_Z)


main()
