"""(Noah: "Alduin's Bane ... floating and slowly circulating and bobbing up and down with a glow around the blade", the wonders
floating in front of the statues, "a fiery pillar for the other things we were going to put in this room") High Hrothgar's
great hall:

  hall_glow.nif       a soft round glow with fine motes rising through it - behind a floating wonder
  hall_glow_tall.nif  the same drawn up tall - round Alduin's Bane, blade up
  pillar_fire.nif     tongues of fire climbing the hall's centre pillar (HHLightPedestal02) on every side, embers lifting
                      off it and a warm glow at its foot - the Hall's rewards are taken there (src/Echo.cpp)

All flipbooks at 60 fps (tools/alchfx.py, tools/motes.py).     python3 tools/hallfx.py"""
import math, pathlib, random, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'hallfx'
FX = 'CurseOfAlduin\\fx\\'


def card(L, name, tex, col, sc, w, h0, h1, at, flip=None, fps=60, phase=0.0, bb=2):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add billboard={bb} ' + (f'flip={flip[0]}x{flip[1]} fps={fps} phase={phase:.2f} ' if flip else '') + f'at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
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


def glow(name, tall):
    random.seed(len(name))
    L = []
    hw, hh = (34, 120) if tall else (46, 46)
    card(L, 'halo', FX + 'glow_disc.dds', '0.85,0.62,0.3', 1.3, hw * 1.6, -hh * 1.5, hh * 1.5, (0, 0, 0))
    card(L, 'core', FX + 'glow_disc.dds', '1,0.9,0.7', 1.1, hw * 0.8, -hh * 0.9, hh * 0.9, (0, 0, 0))
    for k in range(4 if tall else 3):   # fine motes drifting up through it
        a = 2 * math.pi * k / (4 if tall else 3)
        card(L, f'm{k}', FX + 'motes_rise.dds', '1,0.82,0.45', 1.5, hw * 0.55, -hh, hh * 1.1, (14 * math.cos(a), 14 * math.sin(a), 0), (16, 16), random.uniform(54, 66), k * 1.1)
    write(L, name)


def pillar():
    random.seed(77)
    L = []
    R = 44.0   # (the pillar's half-width, near enough: the cards stand just off its faces)
    k = 0
    for ring, (z0, n, w, sc) in enumerate(((0.0, 10, 30.0, 1.7), (70.0, 8, 24.0, 1.5), (135.0, 7, 18.0, 1.3), (190.0, 6, 13.0, 1.1))):
        for i in range(n):
            a = 2 * math.pi * (i + random.uniform(-0.25, 0.25)) / n + ring * 0.6
            card(L, f'f{k}', FX + 'ground_fire.dds', random.choice(('1,0.55,0.15', '1,0.42,0.08', '1,0.68,0.25')), sc, w, -w * 0.1, w * 2.6,
                 (R * math.cos(a), R * math.sin(a), z0), (16, 8), random.uniform(54, 66), random.uniform(0, 2.1))
            k += 1
    for i in range(6):   # the element's living fire on the faces, embers lifting off it
        a = 2 * math.pi * i / 6
        card(L, f'b{i}', FX + 'alch_fire.dds', '1,0.6,0.2', 1.4, 34, -10, 60, ((R + 6) * math.cos(a), (R + 6) * math.sin(a), 40 + 60 * (i % 3)), (16, 8), 60, i * 0.37)
        card(L, f'e{i}', FX + 'motes_rise.dds', '1,0.55,0.15', 1.6, 26, 0, 140, (R * math.cos(a + 0.5), R * math.sin(a + 0.5), 60 + 40 * (i % 2)), (16, 16), random.uniform(54, 66), i * 0.7)
    card(L, 'foot', FX + 'glow_disc.dds', '0.6,0.25,0.05', 1.2, 150, -60, 120, (0, 0, 20))
    write(L, 'pillar_fire.nif')


def main():
    glow('hall_glow.nif', False)
    glow('hall_glow_tall.nif', True)
    pillar()


if __name__ == '__main__':
    main()
