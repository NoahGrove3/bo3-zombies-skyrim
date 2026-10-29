"""BO3 power-up tokens: Insta-Kill (red skull-crossed), Double Points (gold double coin), Kaboom (orange star burst),
Fire Sale (violet chest). A glowing core, an outer ring and a shape that tells them apart, ~30 units across.

    python tools/powerups.py <out Data dir>"""
import math, pathlib, subprocess, sys
import numpy as np
OUTDIR = sys.argv[1] if len(sys.argv) > 1 else None
sys.argv = [sys.argv[0], '/home/claude/work/staffs'] + ([OUTDIR] if OUTDIR else [])
import staffs as S        # noqa: E402
import staffs_proc as P   # noqa: E402
from PIL import Image

def disc(cz, r, thick, seg=40, cy=0.0):
    return P.lathe([(cy - thick / 2, 0.01), (cy - thick / 2, r), (cy + thick / 2, r), (cy + thick / 2, 0.01)], seg)

def token(kind):
    core = {'instakill': (255, 60, 50), 'double': (255, 205, 70), 'kaboom': (255, 140, 40), 'firesale': (180, 100, 255), 'maxammo': (70, 200, 255),
            'shadow': (120, 60, 200), 'thuum': (200, 230, 255), 'mercy': (90, 230, 120)}[kind]
    ring = S.merge([S.ring(0, 0, 0, 13, 14.5, axis='z'), S.ring(0, 0, 0, 15.5, 16.2, tilt=0.6)])
    if kind == 'instakill':     # a skull: cranium, two eye sockets (dark), crossed bones behind
        shape = S.merge([S.sphere(0, 2, 0, 6.5, 16), P.bar((-10, -9, -1), (10, 9, -1), 1.3), P.bar((-10, 9, -1), (10, -9, -1), 1.3)])
        dark = S.merge([S.sphere(-2.4, 2.5, 5.2, 1.6, 8), S.sphere(2.4, 2.5, 5.2, 1.6, 8)])
    elif kind == 'double':      # two stacked coins
        shape = S.merge([rot_x(disc(0, 8, 1.6), math.pi / 2, (-3, 0, 0)), rot_x(disc(0, 8, 1.6), math.pi / 2, (3, 2, 2))])
        dark = None
    elif kind == 'kaboom':      # a star burst
        shape = S.merge([S.crystal(0, 0, 0, 11, 1.6, 4, rot=a, tilt=(0.0, a)) for a in np.linspace(0, 2 * math.pi, 9)[:-1]] + [S.sphere(0, 0, 0, 4.5, 12)])
        dark = None
    elif kind == 'maxammo':     # (the Ranger's refill) a sheaf of five arrows, heads up
        shape = S.merge([P.bar((x, 0, -9), (x * 0.5, 0, 9), 0.9) for x in (-4, -2, 0, 2, 4)] + [S.crystal(x * 0.5, 0, 10, 2.6, 1.4, 4) for x in (-4, -2, 0, 2, 4)])
        dark = None
    elif kind == 'shadow':      # (Shadow of Nocturnal) an eye: a violet orb with a dark pupil
        shape = S.merge([S.sphere(0, 0, 0, 7.5, 18), S.ring(0, 0, 0, 9, 10.5, axis='z')])
        dark = S.sphere(0, 0, 6.2, 2.8, 10)
    elif kind == 'thuum':       # (Thu'um Unbound) three standing shards: the three words
        shape = S.merge([S.crystal(x, 0, -8, 16, 1.8, 4) for x in (-5, 0, 5)])
        dark = None
    elif kind == 'mercy':       # (Kynareth's Mercy) three leaves round a seed
        shape = S.merge([S.crystal(0, 0, 0, 12, 2.6, 4, rot=a, tilt=(0.0, a)) for a in (0.0, 2.094, 4.189)] + [S.sphere(0, 0, 0, 3.5, 12)])
        dark = None
    else:                       # a little chest
        shape = S.merge([box(-7, -4, -5, 7, 3, 5), box(-7.4, 3, -5.4, 7.4, 6, 5.4)])
        dark = None
    parts = {'shape': ('gem', shape, core), 'ring': ('glow_add', ring, core)}
    if dark is not None:
        parts['dark'] = ('dark', dark, (20, 10, 10))
    return parts

def rot_x(m, a, off):
    V, T, F = m
    c, s = math.cos(a), math.sin(a)
    V = V @ np.array([[1, 0, 0], [0, c, -s], [0, s, c]]).T + np.array(off)
    return V, T, F

def box(x0, y0, z0, x1, y1, z1):
    V = np.array([(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)], float)
    F = np.array([(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4), (1, 5, 7), (1, 7, 3)])
    return V, np.zeros((8, 2)), F

def write(kind, parts):
    work = S.OUT.parent / 'powerups' / kind; work.mkdir(parents=True, exist_ok=True)
    obj = work / f'{kind}.obj'
    flat_n = S.save_dds(Image.new('RGBA', (64, 64), (128, 128, 255, 140)), 'flat_n.dds', alpha=True)
    lines = [f'mtllib {kind}.mtl']; vo = 1; mtl = []; mats = []
    for name, (role, (V, T, F), col) in parts.items():
        V = V + np.array([0, 0, 40.0])[[0, 2, 1]] * 0 + np.array([0, 0, 0])
        lines += [f'o {name}', f'usemtl {name}'] + [f'v {x:.4f} {z:.4f} {y + 40:.4f}' for x, y, z in V] + [f'vt {a:.4f} {b:.4f}' for a, b in T]
        lines += [f'f {a+vo}/{a+vo} {b+vo}/{b+vo} {c+vo}/{c+vo}' for a, b, c in F]
        vo += len(V); mtl.append(f'newmtl {name}\nKd 1 1 1\n')
        if role == 'dark':
            d = S.save_dds(S.solid(col, noise=0.0), f'pu_{kind}_{name}_d.dds')
            mats.append(f'mat {name} diffuse={d} normal={flat_n} spec=0.2 gloss=10')
        else:
            d = S.save_dds(S.solid(col, noise=0.0), f'pu_{kind}_{name}_d.dds'); g = S.save_dds(S.solid(tuple(min(255, c + 50) for c in col), noise=0.0), f'pu_{kind}_{name}_g.dds')
            ec = ','.join(f'{c / 255:.3f}' for c in col)
            mats.append(f'mat {name} diffuse={d} normal={flat_n} glow={g} emit={ec} emitmult={3.0 if role == "gem" else 2.0} spec=1.5 gloss=150 doublesided=1' + (' alpha=add' if role == 'glow_add' else ''))
    obj.write_text('\n'.join(lines) + '\n'); (work / f'{kind}.mtl').write_text(''.join(mtl))
    nif = S.OUT / 'Meshes' / 'CurseOfAlduin' / 'powerup' / f'{kind}.nif'; nif.parent.mkdir(parents=True, exist_ok=True)
    jf = work / f'{kind}.job'; jf.write_text('\n'.join([f'in {obj}', f'out {nif}', 'origin 1', *mats]) + '\n')
    r = subprocess.run([S.CONV, str(jf)], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); raise SystemExit(kind)
    print(kind, 'ok')

if __name__ == '__main__':
    for k in (sys.argv[3:] if len(sys.argv) > 3 else ('instakill', 'double', 'kaboom', 'firesale', 'maxammo', 'shadow', 'thuum', 'mercy')):
        write(k, token(k))
