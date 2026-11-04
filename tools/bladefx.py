"""(Noah: "the glowing purple crystal in the center of the mace's head to have purple flames that grow stronger each upgrade ...
In the Spellsword class, the Madness Sword should also have this same effect style ... a dark red flame surrounding the blade
that grows in power and visually as you upgrade the sword") the two class weapons' fire, one model per tier (0 Base .. 3
Legendary), hung on the weapon in your hand by Gun.cpp (its WEAPON node: the weapon's own space - the blade up its +Y):

  maceflame_<t>.nif  violet tongues of fire wreathing the mace's crystal (at y 48.4, tools/wonderprops.py mace), more of them,
                     taller and brighter each tier; a glow at its heart; from Tier II embers drifting off
  madflame_<t>.nif   dark red fire running up the Madness Sword's blade (y 6..62), thicker, taller and hotter each tier;
                     from Tier II embers streaming off; at Legendary a hot core along the edge

All 60 fps flipbooks (tools/alchfx.py ground_fire, tools/motes.py).     python3 tools/bladefx.py"""
import math, pathlib, random, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'bladefx'
FX = 'CurseOfAlduin\\fx\\'


def card(L, name, tex, col, sc, w, h0, h1, at, flip=None, fps=60, phase=0.0):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add billboard=2 ' + (f'flip={flip[0]}x{flip[1]} fps={fps} phase={phase:.2f} ' if flip else '') + f'at={at[0]:.2f},{at[1]:.2f},{at[2]:.2f}')
    cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
    for X, Y, u, v in ((-w, h1, 0, 0), (w, h1, cu, 0), (w, h0, cu, cv), (-w, h0, 0, cv)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u} {v} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def write(L, name):
    WORK.mkdir(parents=True, exist_ok=True); MESH.mkdir(parents=True, exist_ok=True)
    out = MESH / name
    jf = WORK / (out.stem + '.job'); jf.write_text('\n'.join([f'out {out}'] + L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(name, r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


def mace(t):
    random.seed(100 + t)
    L = []
    cy = 48.4
    n, w, sc = [(4, 4.5, 1.2), (6, 5.5, 1.45), (8, 6.8, 1.7), (10, 8.0, 2.0)][t]
    cols = ('0.62,0.28,1', '0.5,0.2,1', '0.78,0.45,1')
    for i in range(n):
        a = 2 * math.pi * (i + random.uniform(-0.2, 0.2)) / n
        r = random.uniform(2.5, 4.5)
        card(L, f'f{i}', FX + 'ground_fire.dds', random.choice(cols), sc, w, -w * 0.4, w * (2.0 + 0.25 * t),
             (r * math.cos(a), cy - 5 + random.uniform(-2, 2), r * math.sin(a)), (16, 8), random.uniform(54, 66), random.uniform(0, 2.1))
    card(L, 'core', FX + 'glow_disc.dds', '0.55,0.25,1', 0.9 + 0.25 * t, 8 + 2.5 * t, -8 - 2.5 * t, 8 + 2.5 * t, (0, cy, 0))
    if t >= 2:
        for i in range(2 + t):
            card(L, f'e{i}', FX + 'motes_rise.dds', '0.7,0.4,1', 1.6, 8, 0, 28 + 6 * t, (0, cy - 2, 0), (16, 16), random.uniform(54, 66), i * 0.9)
    write(L, f'maceflame_{t}.nif')


def madness(t):
    random.seed(200 + t)
    L = []
    y0, y1 = 6.0, 62.0
    n, w, sc = [(5, 3.6, 1.1), (7, 4.4, 1.35), (9, 5.4, 1.6), (12, 6.4, 1.9)][t]
    cols = ('0.62,0.04,0.02', '0.5,0.02,0.02', '0.75,0.1,0.03')
    for i in range(n):
        y = y0 + (y1 - y0) * (i + 0.5) / n + random.uniform(-1.5, 1.5)
        card(L, f'f{i}', FX + 'ground_fire.dds', random.choice(cols), sc, w * (1.1 - 0.35 * (y - y0) / (y1 - y0)), -w * 0.5, w * (1.8 + 0.3 * t),
             (random.uniform(-1, 1), y, random.uniform(-0.6, 0.6)), (16, 8), random.uniform(54, 66), random.uniform(0, 2.1))
    if t >= 1:   # a dark glow down the blade
        for i, y in enumerate((18.0, 36.0, 54.0)):
            card(L, f'g{i}', FX + 'glow_disc.dds', '0.35,0.02,0.01', 0.8 + 0.2 * t, 7 + 1.5 * t, -9 - 2 * t, 9 + 2 * t, (0, y, 0))
    if t >= 2:   # embers streaming off
        for i in range(t + 1):
            card(L, f'e{i}', FX + 'motes_rise.dds', '0.85,0.15,0.05', 1.6, 6, 0, 26 + 6 * t, (0, y0 + 18 * i, 0), (16, 16), random.uniform(54, 66), i * 1.1)
    if t >= 3:   # Legendary: a hot core along the edge
        for i, y in enumerate((14.0, 26.0, 38.0, 50.0, 60.0)):
            card(L, f'c{i}', FX + 'glow_disc.dds', '0.9,0.25,0.08', 0.9, 3.2, -4.5, 4.5, (0, y, 0))
    write(L, f'madflame_{t}.nif')


def main():
    for t in range(4):
        mace(t)
        madness(t)


if __name__ == '__main__':
    main()
