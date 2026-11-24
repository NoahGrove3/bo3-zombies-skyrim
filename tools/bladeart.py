"""(Noah, 2026-11-18: "our own element art for the class weapons" - in place of Vibrant Weapons' ShockSwordR / FrostSwordR /
FireSwordR) the element running along a blade in the hand: an art object hung on the WEAPON node, so its +Y is the blade from
just above the guard to the point. Along it, small camera-facing flipbook cards of the element (tools/alchfx.py's sheets; the
fire ones swapped onto Skyrim's own fire flipbook by tools/realfire.py), each its own phase so the blade never pulses as one,
tapering toward the point; a soft glow strip of the element's colour hugging the steel; a few motes drifting off.

    python3 tools/bladeart.py  ->  assets/fx/Meshes/CurseOfAlduin/fx/blade_<fire|frost|shock>.nif"""
import math, pathlib, random, subprocess, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import realfire
from meteorfx import card

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'bladeart'
FX = 'CurseOfAlduin\\fx\\'
Y0, Y1 = 14.0, 84.0   # the blade, guard to point (the class swords at 85%, the Dragon Slayer)
EL = {   # flipbook, tints, glow colour, card half-size, fps
    'fire':  ('alch_fire.dds', ('1,0.55,0.15', '1,0.42,0.08', '1,0.68,0.25'), '1,0.45,0.12', 4.2, 40),
    'frost': ('alch_frost.dds', ('0.7,0.88,1', '0.55,0.8,1', '0.85,0.95,1'), '0.5,0.8,1', 3.6, 24),
    'shock': ('alch_shock.dds', ('0.7,0.75,1', '0.55,0.62,1', '0.9,0.92,1'), '0.55,0.6,1', 3.4, 50),
}


def strip(L, name, col, y0, y1, w, al):
    """a flat glow strip along the blade, both faces (seen from either side)"""
    L.append(f'shape {name} tex={FX}glow_disc.dds color={col},{al} scale=1.0 blend=add billboard=0')
    n = 12
    for k in range(n + 1):
        y = y0 + (y1 - y0) * k / n
        ww = w * (1 - 0.6 * (k / n) ** 2)
        for x in (-ww, ww):
            L.append(f'v {x:.2f} {y:.2f} 0 {0.0 if x < 0 else 1.0} {0.5:.2f} 1 1 1 {math.sin(math.pi * k / n) ** 0.6:.3f}')
    for k in range(n):
        a = 2 * k
        L += [f't {a} {a + 1} {a + 3}', f't {a} {a + 3} {a + 2}', f't {a} {a + 3} {a + 1}', f't {a} {a + 2} {a + 3}']


def build(key):
    tex, tints, glow, hs, fps = EL[key]
    random.seed(41 + len(key))
    L = [f'out {MESH / f"blade_{key}.nif"}']
    strip(L, 'glow', glow, Y0, Y1, 2.2, 0.55)
    n = 15
    for i in range(n):
        s = i / (n - 1)
        y = Y0 + (Y1 - Y0) * s
        w = hs * (1.0 - 0.45 * s) * random.uniform(0.85, 1.15)
        x = random.uniform(-0.6, 0.6)
        card(L, f'e{i}', FX + tex, random.choice(tints), 1.2, w, (x, y + w * 0.4, 0.0), random.uniform(0, 2.0), fps=fps)
    for i in range(4):   # (motes drifting off the edge)
        y = random.uniform(Y0 + 10, Y1 - 6)
        card(L, f'm{i}', FX + 'glow_disc.dds', glow, 1.4, 0.8, (random.choice((-2.2, 2.2)), y, 0.0), 0, flip=None)
    WORK.mkdir(parents=True, exist_ok=True); MESH.mkdir(parents=True, exist_ok=True)
    jf = WORK / f'blade_{key}.job'
    jf.write_text('\n'.join(realfire.fix(L, 'fire') if key == 'fire' else L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(key, r.stdout.strip()[-120:], r.stderr.strip()[-300:])
    if r.returncode: raise SystemExit('fxwrite failed')
    # (2026-11-24: the art stood on the floor at the feet) hung on the weapon in the hand, as Vibrant Weapons' arts are:
    # the root's AttachT names the WEAPON node (tools/modelconv/addattach.cpp)
    subprocess.run(['/home/claude/tools/addattach', str(MESH / f'blade_{key}.nif'), 'WEAPON'], check=True)


if __name__ == '__main__':
    for k in EL: build(k)
