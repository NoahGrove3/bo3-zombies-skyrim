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


def sheath(L, name, tex, tint, sc, y0, y1, w, rep, scroll, ang, fade_in=0.15, fade_out=0.3, segs=14):
    """a sheet of fire lying along the weapon (its +Y): across it +-w turned ang degrees about Y, the flame sliding along
    it forever (vscroll), fading to nothing at both ends by its vertex colours - it lies on the blade, it doesn't stand up
    through it, and no edge of it ever shows"""
    a = math.radians(ang); ax, az = math.cos(a), math.sin(a)
    L.append(f'shape {name} tex={tex} color={tint},1 scale={sc} blend=add single=0 vscroll={scroll:.2f}')
    for k in range(segs + 1):
        f = k / segs
        y = y0 + (y1 - y0) * f
        fade = min(1.0, f / fade_in) * min(1.0, (1 - f) / fade_out)
        fade = fade * fade * (3 - 2 * fade)
        v = f * rep
        for side, uu in ((-1, 0.0), (1, 1.0)):
            L.append(f'v {side * w * ax:.3f} {y:.3f} {side * w * az:.3f} {uu} {v:.4f} {fade:.3f} {fade:.3f} {fade:.3f} 1')
    for k in range(segs):
        a0 = 2 * k
        L += [f't {a0} {a0 + 1} {a0 + 3}', f't {a0} {a0 + 3} {a0 + 2}']


def mace(t):
    """violet fire burning in the mace's head round its crystal (y 48.4): three sheets about the haft, rising out through the
    cage and thinning above it - more and taller each tier; from Tier II embers drifting off"""
    random.seed(100 + t)
    L = []
    cy = 48.4
    w, top, sc = [(6.0, 12.0, 1.1), (7.2, 16.0, 1.3), (8.5, 21.0, 1.55), (10.0, 27.0, 1.85)][t]
    for k, ang in enumerate((0, 60, 120)):
        sheath(L, f'f{k}', FX + 'blade_flame_violet.dds', '1,1,1', sc, cy - 9, cy + top, w, 0.9, -1.6 - 0.2 * k, ang, 0.2, 0.55, 10)
    if t >= 1:
        for k, ang in enumerate((30, 90, 150)):
            sheath(L, f'g{k}', FX + 'blade_flame_violet.dds', '0.8,0.75,1', sc * 0.7, cy - 5, cy + top * 1.25, w * 1.3, 1.4, -1.1, ang, 0.25, 0.6, 10)
    if t >= 2:
        for k in range(1 + t):
            card(L, f'e{k}', FX + 'motes_rise.dds', '0.7,0.45,1', 1.5, 7, 0, 26 + 6 * t, (0, cy, 0), (16, 16), random.uniform(54, 66), k * 0.9)
    write(L, f'maceflame_{t}.nif')


def madness(t):
    """dark red fire sheathing the Madness Sword's blade (y 6..62): two crossed sheets along the blade, the flame flowing
    along it; thicker and hotter each tier, a second, wilder layer from Tier I, embers from Tier II"""
    random.seed(200 + t)
    L = []
    y0, y1 = 7.0, 63.0
    w, sc = [(4.6, 1.0), (5.6, 1.2), (6.8, 1.45), (8.0, 1.75)][t]
    for k, ang in enumerate((0, 90)):
        sheath(L, f'f{k}', FX + 'blade_flame_red.dds', '1,1,1', sc, y0, y1, w, 1.6, -1.3 - 0.25 * k, ang, 0.12, 0.25)
    if t >= 1:
        for k, ang in enumerate((45, 135)):
            sheath(L, f'g{k}', FX + 'blade_flame_red.dds', '0.85,0.8,0.8', sc * 0.65, y0 + 3, y1 + 4, w * 1.35, 2.6, -0.9, ang, 0.15, 0.3)
    if t >= 2:
        for k in range(t + 1):
            card(L, f'e{k}', FX + 'motes_rise.dds', '0.85,0.15,0.05', 1.5, 5, 0, 22 + 6 * t, (0, y0 + 16 * k + 6, 0), (16, 16), random.uniform(54, 66), k * 1.1)
    write(L, f'madflame_{t}.nif')


def main():
    for t in range(4):
        mace(t)
        madness(t)


if __name__ == '__main__':
    main()
