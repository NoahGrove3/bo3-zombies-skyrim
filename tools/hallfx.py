"""(Noah: "Alduin's Bane ... floating and slowly circulating and bobbing up and down with a glow around the blade", the wonders
floating in front of the statues, "a fiery pillar for the other things we were going to put in this room") High Hrothgar's
great hall:

  hall_glow.nif       a soft round glow with fine motes rising through it - behind a floating wonder
  hall_glow_tall.nif  the same drawn up tall - round Alduin's Bane, blade up
  pillar_fire.nif     tongues of fire climbing the hall's centre pillar (HHLightPedestal02) on every side, embers lifting
                      off it and a warm glow at its foot - the Hall's rewards are taken there (src/Echo.cpp)

All flipbooks at 60 fps (tools/alchfx.py, tools/motes.py).     python3 tools/hallfx.py"""
import math, pathlib, random, subprocess
import realfire  # (Noah: Skyrim's own fire flipbooks - realistic with Ultimate HD Fire Effects)

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
    jf = WORK / (out.stem + '.job'); jf.write_text('\n'.join([f'out {out}'] + realfire.fix(L, 'fire')) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(name, r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


def pillar():
    """(Noah: "The flames on The Endless Flame need to look higher quality, and actually look like this pillar is on fire. I can see
    the cutoff of the flame animations") the centre pillar (HHLightPedestal02: 184 square at its foot, 416 tall, narrowing as it
    rises) wrapped in fire: four rings of tall flame sheets (tools/alchfx.py pillar_flame - soft to nothing at every edge),
    standing upright round it and climbing it, the lowest the largest; the faces behind the pillar hidden by it, the ones in front
    burning over its stone; embers lifting and a warm glow at its foot"""
    random.seed(77)
    L = []
    k = 0
    for ring, (z0, R, n, w) in enumerate(((0.0, 108.0, 12, 42.0), (95.0, 92.0, 10, 34.0), (180.0, 78.0, 8, 26.0), (245.0, 66.0, 6, 18.0))):
        for i in range(n):
            a = 2 * math.pi * (i + random.uniform(-0.2, 0.2)) / n + ring * 0.45
            r = R * random.uniform(0.92, 1.06)
            card(L, f'f{k}', FX + 'pillar_flame.dds', random.choice(('1,0.92,0.85', '1,0.85,0.7', '1,0.95,0.9')), 1.25, w * random.uniform(0.9, 1.15), -w * 0.1, w * 3.9,
                 (r * math.cos(a), r * math.sin(a), z0 + random.uniform(-8, 8)), (16, 8), random.uniform(52, 64), random.uniform(0, 2.1))
            k += 1
    for i in range(8):   # embers lifting off it
        a = 2 * math.pi * i / 8
        card(L, f'e{i}', FX + 'motes_rise.dds', '1,0.55,0.15', 1.7, 34, 0, 190, (100 * math.cos(a), 100 * math.sin(a), 40 + 50 * (i % 3)), (16, 16), random.uniform(54, 66), i * 0.6)
    card(L, 'foot', FX + 'glow_disc.dds', '0.55,0.22,0.04', 1.2, 230, -80, 160, (0, 0, 30))
    write(L, 'pillar_fire.nif')


def glow(name, tall):
    """a soft glow behind a floating wonder, fine motes drifting up through it and slow sparkles: drawn as a lingering hazard"""
    random.seed(len(name))
    L = []
    hw, hh = (34, 110) if tall else (44, 60)
    card(L, 'halo', FX + 'glow_disc.dds', '0.85,0.62,0.3', 1.3, hw * 1.7, -hh * 1.4, hh * 1.4, (0, 0, 0))
    card(L, 'core', FX + 'glow_disc.dds', '1,0.9,0.7', 1.0, hw * 0.75, -hh * 0.8, hh * 0.8, (0, 0, 0))
    for k in range(5 if tall else 4):
        a = 2 * math.pi * k / (5 if tall else 4)
        card(L, f'm{k}', FX + 'motes_rise.dds', '1,0.82,0.45', 1.6, hw * 0.6, -hh * 1.1, hh * 1.3, (18 * math.cos(a), 18 * math.sin(a), 0), (16, 16), random.uniform(54, 66), k * 1.1)
    for k in range(2):
        card(L, f's{k}', FX + 'sparks_burst.dds', '1,0.85,0.5', 1.2, hw * 0.9, -hh * 0.9, hh * 0.9, (0, 0, 10 * k), (16, 8), 30, k * 1.0)
    write(L, name)


def main():
    glow('hall_glow.nif', False)
    glow('hall_glow_tall.nif', True)
    pillar()


if __name__ == '__main__':
    main()
