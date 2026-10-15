"""Two staffs built from scratch (no source model): Chronos Hourglass (time) and Prismheart (crystal).
Same frame as tools/staffs.py: grip at the origin, head toward +Y, head centre at y=62, ~132 units long.

    python tools/staffs_proc.py <out Data dir>"""
import math, pathlib, subprocess, sys
import numpy as np
OUTDIR = sys.argv[1] if len(sys.argv) > 1 else None
sys.argv = [sys.argv[0], '/home/claude/work/staffs'] + ([OUTDIR] if OUTDIR else [])
import staffs as S  # noqa: E402  (shared texture/geometry helpers)
from PIL import Image

def lathe(profile, seg=20, twist=0.0, sides=None):
    """surface of revolution from [(y, r)], optionally twisted (degrees per unit) or faceted (sides)"""
    n = sides or seg
    V, T, F = [], [], []
    for i, (y, r) in enumerate(profile):
        for j in range(n + 1):
            a = 2 * math.pi * j / n + math.radians(twist * y)
            V.append((r * math.cos(a), y, r * math.sin(a))); T.append((j / n, y / 40.0))
    for i in range(len(profile) - 1):
        for j in range(n):
            a = i * (n + 1) + j; b = a + n + 1
            F += [(a, a + 1, b), (a + 1, b + 1, b)]
    return np.array(V, float), np.array(T, float), np.array(F)

def gear(cy, r_in, r_out, teeth, thick, tilt=0.0, depth=2.2):
    """flat gear ring facing forward (normal along z), teeth on the outside"""
    pts = []
    for k in range(teeth * 4):
        a = 2 * math.pi * k / (teeth * 4)
        rr = r_out + (depth if (k % 4) in (1, 2) else 0)
        pts.append((a, rr))
    V, T, F = [], [], []
    m = len(pts)
    for z in (-thick / 2, thick / 2):
        for a, rr in pts:
            V.append((rr * math.cos(a), rr * math.sin(a), z)); T.append((a / 6.28, 1))
        for a, rr in pts:
            V.append((r_in * math.cos(a), r_in * math.sin(a), z)); T.append((a / 6.28, 0))
    for k in range(m):
        k2 = (k + 1) % m
        for base in (0, 2 * m):
            o, i_ = base + k, base + m + k
            o2, i2 = base + k2, base + m + k2
            F += [(o, o2, i_), (o2, i2, i_)] if base else [(o, i_, o2), (o2, i_, i2)]
        F += [(k, k2, 2 * m + k), (k2, 2 * m + k2, 2 * m + k)]                     # outer rim
        F += [(m + k, 3 * m + k, m + k2), (m + k2, 3 * m + k, 3 * m + k2)]         # inner rim
    V = np.array(V, float)
    c, s = math.cos(tilt), math.sin(tilt)
    V = V @ np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]]).T
    V[:, 1] += cy
    return V, np.array(T, float), np.array(F)

def bar(p0, p1, r, seg=8):
    p0, p1 = np.array(p0, float), np.array(p1, float)
    d = p1 - p0; L = np.linalg.norm(d); d /= L
    up = np.array([0, 0, 1.0]) if abs(d[2]) < 0.9 else np.array([1.0, 0, 0])
    u = np.cross(d, up); u /= np.linalg.norm(u); w = np.cross(d, u)
    V, T, F = [], [], []
    for i, pt in enumerate((p0, p1)):
        for j in range(seg + 1):
            a = 2 * math.pi * j / seg
            V.append(pt + (u * math.cos(a) + w * math.sin(a)) * r); T.append((j / seg, i))
    for j in range(seg):
        F += [(j, j + 1, seg + 1 + j), (j + 1, seg + 2 + j, seg + 1 + j)]
    return np.array(V), np.array(T, float), np.array(F)

def curve(points, r0, r1, seg=8):
    parts = []
    for i in range(len(points) - 1):
        t0, t1 = i / (len(points) - 1), (i + 1) / (len(points) - 1)
        parts.append(bar(points[i], points[i + 1], r0 + (r1 - r0) * (t0 + t1) / 2, seg))
    return S.merge(parts)

def chronos():
    shaft = [(-60, 0.2), (-55, 1.8), (-52, 1.2)] + sum([[(y, 1.25), (y + 1, 1.9), (y + 2.5, 1.9), (y + 3.5, 1.25)] for y in range(-45, 40, 14)], []) + [(44, 1.3), (46, 3.2), (48.5, 3.6), (49.5, 3.0)]
    cap_top = [(75, 3.0), (76, 3.6), (78.5, 3.2), (80, 1.5), (84, 0.2)]
    gold = S.merge([lathe(shaft), lathe(cap_top), bar((8.5, 49, 0), (8.5, 76, 0), 0.8), bar((-8.5, 49, 0), (-8.5, 76, 0), 0.8),
                    bar((0, 49, 8.5), (0, 76, 8.5), 0.8), bar((0, 49, -8.5), (0, 76, -8.5), 0.8),
                    lathe([(48.5, 9.2), (49.5, 9.2)]), lathe([(75.0, 9.2), (76.0, 9.2)]),
                    gear(62, 13.5, 15.0, 24, 1.2), gear(62, 17.0, 18.0, 32, 0.8, tilt=0.6, depth=1.6)])
    glass = lathe([(50.5, 0.3), (51, 6.8), (58, 5.2), (61.5, 1.0), (62.5, 1.0), (66, 5.2), (73, 6.8), (73.5, 0.3)], 24)
    sand = S.merge([lathe([(51.2, 0.2), (51.6, 6.0), (55.5, 3.2), (56.5, 0.2)], 20), lathe([(67.5, 0.2), (68.5, 3.0), (72.6, 5.8), (72.9, 0.2)], 20),
                    bar((0, 56.5, 0), (0, 67.5, 0), 0.25, 6)])
    return {'gold': ('metal', gold, (205, 160, 70)), 'glass': ('glow_add', glass, (255, 200, 110)), 'sand': ('gem', sand, (255, 170, 50))}

def prism():
    shaft = lathe([(-60, 0.3), (-56, 1.6), (-50, 1.4), (45, 1.4), (48, 2.4), (51, 2.6), (52, 1.8)], sides=6, twist=2.2)
    prongs = []
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        pts = [(math.cos(a) * r, y, math.sin(a) * r) for y, r in ((51, 2.0), (54, 5.0), (57, 7.6), (61, 8.8), (65, 8.0), (68, 6.0))]
        prongs.append(curve(pts, 1.1, 0.3))
    slate = S.merge([shaft] + prongs)
    big = S.crystal(0, 51.0, 0, 26.0, 6.0, sides=4, rot=math.pi / 4)
    small = S.merge([S.crystal(math.cos(a) * 12, 60 + 4 * math.sin(3 * a), math.sin(a) * 12, 6, 1.3, sides=4, rot=a) for a in np.linspace(0, 2 * math.pi, 7)[:-1]])
    shards = S.merge([S.crystal(math.cos(a) * 1.4, y, math.sin(a) * 1.4, 4.5, 0.8, sides=4, rot=a, tilt=(0.0, 0.5 * math.cos(a)))
                      for y, a in ((-20, 0.3), (-5, 2.4), (12, 4.1), (28, 1.2))])
    return {'slate': ('dark', slate, (52, 58, 72)), 'prism': ('gem', big, (190, 240, 255)), 'orbit': ('glow_add', small, (150, 220, 255)),
            'shards': ('gem', shards, (120, 200, 255))}

def write(sid, parts, glow):
    work = S.OUT.parent / 'staffs' / sid; work.mkdir(parents=True, exist_ok=True)
    obj = work / f'{sid}.obj'
    flat_n = S.save_dds(Image.new('RGBA', (64, 64), (128, 128, 255, 140)), 'flat_n.dds', alpha=True)
    lines = [f'mtllib {sid}.mtl']; vo = 1; mtl = []; mats = []
    for name, (role, (V, T, F), col) in parts.items():
        lines += [f'o {name}', f'usemtl {name}'] + [f'v {x:.4f} {y:.4f} {z:.4f}' for x, y, z in V] + [f'vt {a:.4f} {b:.4f}' for a, b in T]
        lines += [f'f {a+vo}/{a+vo} {b+vo}/{b+vo} {c+vo}/{c+vo}' for a, b, c in F]
        vo += len(V); mtl.append(f'newmtl {name}\nKd 1 1 1\n')
        if role in ('gem', 'glow_add'):
            d = S.save_dds(S.solid(col, noise=0.0), f'{sid}_{name}_d.dds'); g = S.save_dds(S.solid(tuple(min(255, c + 40) for c in col), noise=0.0), f'{sid}_{name}_g.dds')
            ec = ','.join(f'{c / 255:.3f}' for c in col)
            mats.append(f'mat {name} diffuse={d} normal={flat_n} glow={g} emit={ec} emitmult={3.0 if role == "gem" else 2.0} spec=1.5 gloss=200 doublesided=1' + (' alpha=add' if role == 'glow_add' else ''))
        else:
            import skymat
            dif, nm = skymat.make('bronze' if role == 'metal' else 'iron', col, seed=len(sid) + len(name))
            d = S.save_dds(dif, f'{sid}_{name}_d.dds'); n = S.save_dds(nm, f'{sid}_{name}_n.dds', alpha=True)
            mats.append(f'mat {name} diffuse={d} normal={n} spec={1.4 if role == "metal" else 0.9} gloss={90 if role == "metal" else 60} doublesided=1')
    obj.write_text('\n'.join(lines) + '\n'); (work / f'{sid}.mtl').write_text(''.join(mtl))
    nif = S.OUT / 'Meshes' / 'CurseOfAlduin' / 'staff' / f'{sid}.nif'; nif.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {nif}', 'origin 1', 'prn WeaponStaff', *mats]
    jf = work / f'{sid}.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([S.CONV, str(jf)], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); raise SystemExit(f'modelconv failed for {sid}')
    print(f'{sid}: {sum(len(p[1][2]) for p in parts.values())} tris')

if __name__ == '__main__':
    # (kagrenac and longshot are bundle remakes now, built by staffs.py; these two shapes became city relics)
    write('lorkhan', chronos(), (1.0, 0.6, 0.2))
    write('magnus', prism(), (0.55, 0.85, 1.0))
