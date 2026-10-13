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


def shards():
    S.parts.clear()
    ring = [(0.0, 95, 70, 0.4, 120), (0.9, 105, 10, -0.3, 135), (1.8, 92, 105, 0.9, 110), (2.7, 110, -40, -0.7, 140), (3.6, 98, 40, 0.2, 125),
            (4.5, 104, -90, 1.1, 120), (5.4, 90, -10, -0.2, 115), (6.0, 80, 140, 0.6, 85), (0.45, 76, -130, -1.0, 95)]
    for i, (a, d, z, tilt, L) in enumerate(ring):
        shard_mesh('shard', math.cos(a) * d, math.sin(a) * d, z, a + math.pi / 2, tilt, L, L * 0.5, 16, seed=i)
    # little chips drifting with them
    for i in range(10):
        a = i * 0.63 + 0.2
        shard_mesh('shard', math.cos(a) * (60 + i * 6), math.sin(a) * (60 + i * 6), -70 + i * 16, a, i * 0.4, 22, 12, 6, seed=50 + i)


def crystal():
    S.parts.clear()
    # an eight-sided bipyramid, tall, with a crown of three spikes
    h_top, h_bot, r = 40, 34, 20
    mid = [(r * math.cos(2 * math.pi * k / 8 + 0.2), r * math.sin(2 * math.pi * k / 8 + 0.2), 0) for k in range(8)]
    for k in range(8):
        a, b = mid[k], mid[(k + 1) % 8]
        S.add('crystal', [a, b, (0, 0, h_top)], [(0, 0), (1, 0), (0.5, 1)], [(0, 1, 2)])
        S.add('crystal', [b, a, (0, 0, -h_bot)], [(0, 0), (1, 0), (0.5, 1)], [(0, 1, 2)])
    for k, (ang, hgt) in enumerate(((0.0, 28), (2.1, 22), (4.2, 25))):
        bx, by = math.cos(ang) * 9, math.sin(ang) * 9
        base = [(bx + 5 * math.cos(2 * math.pi * j / 4), by + 5 * math.sin(2 * math.pi * j / 4), h_top * 0.55) for j in range(4)]
        tip = (bx * 1.6, by * 1.6, h_top * 0.55 + hgt)
        for j in range(4):
            S.add('crystal', [base[j], base[(j + 1) % 4], tip], [(0, 0), (1, 0), (0.5, 1)], [(0, 1, 2)])


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
    convert('shards', [], [stone('shard', 0.35), f'mat vein diffuse={rune_d} normal={flat} glow={vg} emit=0.4,0.75,1.0 emitmult=3.0 spec=0.3 doublesided=1'])
    crystal()
    cd = S.save(tex_crystal(), 'crystal_d.dds')
    convert('crystal', [], [f'mat crystal diffuse={cd} normal={flat} glow={cd} emit=0.6,0.85,1.0 emitmult=3.2 spec=1.0 gloss=80'])
    print('crystal centre at z', CRYSTAL_Z)


main()
