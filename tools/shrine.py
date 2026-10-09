"""The Shrine of the Dirge: the whole station in one compact piece (the Coffer, the Brew Machine and the Atronach
Reforge live in its menu). An eight-sided step of Whiterun stone with a knotwork edge and a cobbled skirt you can
walk up from any side, a carved rune-stone at its heart with a glowing glyph on four faces, a ring of the curse's
rune on the floor, and a cradle of four iron prongs on the stone's crown where the Coffer's spells take shape.
Skyrim's own Whiterun textures, referenced by path. Origin = ground under the centre, +Z up.
    python tools/shrine.py <out Data dir>"""
import math, pathlib, subprocess, sys
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import stand as S

OUT = pathlib.Path(sys.argv[1]); S.OUT = OUT
R, TOP, BOTTOM, GROW = 150, 22, -60, 70
STONE_R, STONE_H = 36, 190
S.parts.clear()

def oct_(r, z): return S.octagon(r, z)

def build():
    top = oct_(R, TOP)
    S.add('floor', [(0, 0, TOP)] + top, [(0, 0)] + [(x / 150, y / 150) for x, y, _ in top], [(0, 1 + k, 1 + (k + 1) % 8) for k in range(8)])
    under, bot = oct_(R, TOP - 18), oct_(R + GROW, BOTTOM)
    for k in range(8):
        a, b = top[k], top[(k + 1) % 8]
        out = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2); L = math.hypot(*out); out = (out[0] / L, out[1] / L)
        S.band('trim', a[:2], b[:2], TOP - 18, TOP, (out[0] * 0.8, out[1] * 0.8), S.KNOT_BROWN, tile=120)
        c, d = under[k], under[(k + 1) % 8]; e, f = bot[(k + 1) % 8], bot[k]
        Lr = math.hypot(d[0] - c[0], d[1] - c[1])
        S.add('cobble', [c, d, e, f], [(0, 0), (Lr / 140, 0), (Lr / 140, 1.2), (0, 1.2)], [(0, 2, 1), (0, 3, 2)])
    S.ring('trim', 0, 0, TOP + 0.6, 70, 92, vband=S.KNOT_BLUE)
    S.ring('rune', 0, 0, TOP + 0.8, 94, 97)
    S.ring('rune', 0, 0, TOP + 0.8, 128, 130)
    base = TOP
    for rr, z0, z1 in ((58, base, base + 12), (46, base + 12, base + 26)):
        o0, o1 = oct_(rr, z0), oct_(rr, z1)
        S.add('stone', o1 + [(0, 0, z1)], [(x / 128, y / 128) for x, y, _ in o1] + [(0, 0)], [(8, k, (k + 1) % 8) for k in range(8)])
        for k in range(8):
            S.quad('stone', o0[k], o0[(k + 1) % 8], o1[(k + 1) % 8], o1[k], uv=((0, 0), (0.5, 0), (0.5, 0.12), (0, 0.12)))
    z0 = base + 26
    s0, s1 = oct_(STONE_R, z0), oct_(STONE_R * 0.8, z0 + STONE_H)
    for k in range(8):
        a, b, c, d = s0[k], s0[(k + 1) % 8], s1[(k + 1) % 8], s1[k]
        S.quad('panel' if k % 2 == 0 else 'stone', a, b, c, d, uv=((0, 0), (0.25, 0), (0.25, 1.0), (0, 1.0)))
        if k % 2 == 1:
            mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2; L = math.hypot(mx, my); nx, ny = mx / L, my / L
            tx, ty = -ny, nx; gz = z0 + STONE_H * 0.55; r = 12
            px, py = mx * 0.93 + nx * 1.5, my * 0.93 + ny * 1.5
            S.quad('glyph%d' % (k // 2 % 3), (px - tx * r * 0.75, py - ty * r * 0.75, gz), (px, py, gz - r), (px + tx * r * 0.75, py + ty * r * 0.75, gz), (px, py, gz + r), both=True)
    ztop = z0 + STONE_H
    cap = oct_(STONE_R * 0.95, ztop + 10)
    S.add('wood', cap + [(0, 0, ztop + 18)], [(x / 96, y / 96) for x, y, _ in cap] + [(0, 0)], [(8, k, (k + 1) % 8) for k in range(8)])
    for k in range(8):
        S.quad('wood', s1[k], s1[(k + 1) % 8], cap[(k + 1) % 8], cap[k], uv=((0, 0), (0.3, 0), (0.3, 0.1), (0, 0.1)))
    # the cradle: four iron prongs curling up round the place where the spell takes shape
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        p0 = (math.cos(a) * 22, math.sin(a) * 22, ztop + 14)
        p1 = (math.cos(a) * 34, math.sin(a) * 34, ztop + 44)
        p2 = (math.cos(a) * 26, math.sin(a) * 26, ztop + 70)
        S.slanted('wood', p0, p1, 6, 6); S.slanted('wood', p1, p2, 5, 5)
    S.ring('rune', 0, 0, ztop + 19, 12, 17, seg=24)
    return ztop

def main():
    ztop = build()
    work = OUT.parent / 'shrine'; work.mkdir(parents=True, exist_ok=True)
    obj = work / 'shrine.obj'; S.write_obj(obj)
    flat = S.save(Image.new('RGBA', (64, 64), (128, 128, 255, 160)), 'flat_n.dds', True)
    rune_d = S.save(Image.new('RGB', (64, 64), (170, 90, 255)), 'rune_d.dds')
    rune_g = S.save(Image.new('RGB', (64, 64), (190, 110, 255)), 'rune_g.dds')
    out = OUT / 'Meshes' / 'UndyingNights' / 'shrine' / 'shrine.nif'; out.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {out}', f'collidefrustum {-R} {-R} {R} {R} {TOP} {BOTTOM} {GROW}', f'collidebox2 {-STONE_R} {-STONE_R} {TOP} {STONE_R} {STONE_R} {ztop + 20}']
    for m, (d, n) in S.TEX.items():
        if m not in S.parts: continue
        spec = {'stone': 0.25, 'floor': 0.3, 'cobble': 0.25, 'wood': 0.2, 'trim': 0.25, 'panel': 0.2}.get(m, 0.2)
        job.append(f'mat {m} diffuse={S.W}{d}.dds normal={S.W}{n}.dds spec={spec} gloss=30')
    job.append(f'mat rune diffuse={rune_d} normal={flat} glow={rune_g} emit=0.75,0.43,1.0 emitmult=2.5 spec=0.5')
    for i, col in enumerate(((1.0, 0.45, 0.12), (0.70, 0.35, 1.0), (1.0, 0.75, 0.25))):
        rgb = tuple(int(c * 255) for c in col)
        gd = S.save(Image.new('RGB', (32, 32), rgb), f'glyph{i}_d.dds'); gg = S.save(Image.new('RGB', (32, 32), tuple(min(255, c + 40) for c in rgb)), f'glyph{i}_g.dds')
        job.append(f'mat glyph{i} diffuse={gd} normal={flat} glow={gg} emit={col[0]:.2f},{col[1]:.2f},{col[2]:.2f} emitmult=3 spec=0.5')
    jf = work / 'shrine.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([S.CONV, str(jf)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip(), '| crown at', ztop + 19)
    if r.returncode: raise SystemExit('modelconv failed')

main()
