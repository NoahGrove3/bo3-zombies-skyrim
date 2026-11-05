"""(Noah: "Horn of the Spectral Wyrm should shoot purple fire") the Spectral Dragon's breath: a burst of violet flame - the
fire flipbook Emberstring's arrows burn with (tools/bowarrows.py), on camera-facing cards in a ragged ring round a white-hot
core, each card its own phase so the burst churns. Placed along the breath's line and taken away a moment later
(src/Classes.cpp the Horn's dragon; src/Gun.cpp PurpleFire).

    python3 tools/purplefire.py   ->  assets/fx/Meshes/CurseOfAlduin/fx/purplefire.nif"""
import math, pathlib, random, subprocess
import realfire  # (Noah: Skyrim's own fire flipbooks - realistic with Ultimate HD Fire Effects)

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'purplefire.nif'
WORK = ROOT / 'build' / 'purplefire'
FX = 'CurseOfAlduin\\bow\\emberstring_arrowfx.dds'   # an 8x8 fire flipbook (white-hot to orange: tinted here)


def card(L, name, col, sc, w, h, at, phase, fps=26):
    L.append(f'shape {name} tex={FX} color={col},1 scale={sc} blend=add billboard=2 flip=8x8 fps={fps} phase={phase:.2f} at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
    for X, Y, u, v in ((-w, h, 0, 0), (w, h, 1 / 8, 0), (w, -h * 0.35, 1 / 8, 1 / 8), (-w, -h * 0.35, 0, 1 / 8)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u} {v} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def main():
    random.seed(7)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    L = [f'out {OUT}']
    k = 0
    # the core: white-violet, tall
    for i in range(3):
        card(L, f'pf{k}', '1,0.75,1', 1.5, 26, 54, (random.uniform(-6, 6), random.uniform(-6, 6), 0), i * 0.83); k += 1
    # the body: magenta-violet tongues in a ragged ring
    for i in range(9):
        a = 2 * math.pi * i / 9 + random.uniform(-0.2, 0.2)
        r = random.uniform(28, 60)
        card(L, f'pf{k}', random.choice(('1,0.3,0.85', '1,0.2,0.75', '1,0.4,0.95')), 1.7, random.uniform(22, 32), random.uniform(40, 66),
             (r * math.cos(a), r * math.sin(a), random.uniform(-4, 14)), (k * 0.37) % 2.4); k += 1
    # the fringe: deep violet, low and wide
    for i in range(6):
        a = 2 * math.pi * i / 6 + 0.4
        card(L, f'pf{k}', '0.8,0.08,0.65', 1.4, 40, 34, (82 * math.cos(a), 82 * math.sin(a), -6), (k * 0.53) % 2.4, fps=20); k += 1
    jf = WORK / 'purplefire.job'
    jf.write_text('\n'.join(realfire.fix(L)) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
