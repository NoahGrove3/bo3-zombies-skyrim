"""(Noah: "I still want the Madness Sword to have an animated enchantment effect ... animated from the start, and the
animation should look stronger as it's upgraded") the Madness Sword's dark-red fire: the flame flipbook the altar bows'
arrowheads burn with (tools/bowarrows.py, emberstring), tinted the blood-red of the Madness ore, on small camera-facing
cards running up the blade - each its own phase, so the fire licks up the steel - with a white-hot seam at the edge
from Tier II and embers above it at Legendary. Hung on the WEAPON node in both bodies while the blade is drawn
(src/WonderProps.inl Props::TickMad): the grip at the origin, the blade up +Y.

    python3 tools/madflame.py   ->  assets/fx/Meshes/CurseOfAlduin/fx/madflame_<0..3>.nif"""
import pathlib, random, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'madflame'
FX = 'CurseOfAlduin\\bow\\emberstring_arrowfx.dds'
Y0, Y1 = 16.0, 92.0   # the blade, from just past the guard to the point

# tier: (cards up the blade, card half-size, colour scale, body colours, seam?, embers?)
TIERS = [
    (9, 3.2, 1.0, ('0.75,0.05,0.03', '0.6,0.03,0.02'), False, False),
    (13, 3.8, 1.25, ('0.85,0.08,0.04', '0.7,0.04,0.02', '0.95,0.15,0.05'), False, False),
    (17, 4.4, 1.5, ('0.95,0.1,0.05', '0.8,0.05,0.03', '1,0.22,0.08'), True, False),
    (23, 5.2, 1.85, ('1,0.12,0.05', '0.9,0.06,0.03', '1,0.3,0.1'), True, True),
]


def card(L, name, col, sc, w, h, at, phase, fps=28):
    L.append(f'shape {name} tex={FX} color={col},1 scale={sc} blend=add billboard=1 flip=8x8 fps={fps} phase={phase:.2f} at={at[0]:.2f},{at[1]:.2f},{at[2]:.2f}')
    for X, Y, u, v in ((-w, h, 0, 0), (w, h, 1 / 8, 0), (w, -h * 0.4, 1 / 8, 1 / 8), (-w, -h * 0.4, 0, 1 / 8)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u} {v} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def build(t):
    n, half, sc, body, seam, embers = TIERS[t]
    random.seed(31 + t)
    L = [f'out {OUT / f"madflame_{t}.nif"}']
    k = 0
    for i in range(n):
        y = Y0 + (Y1 - Y0) * (i + 0.5) / n
        taper = 1.0 - 0.45 * (y - Y0) / (Y1 - Y0)          # (thinner toward the point)
        card(L, f'm{k}', random.choice(body), sc, half * taper, half * 1.9 * taper,
             (random.uniform(-0.8, 0.8), y, random.uniform(-0.8, 0.8)), (k * 0.41) % 2.6); k += 1
    if seam:  # the white-hot seam along the edge: small, bright, quick
        for i in range(n // 2):
            y = Y0 + (Y1 - Y0) * (i + 0.25) / (n // 2)
            card(L, f'm{k}', '1,0.55,0.35', sc * 0.9, half * 0.45, half * 0.9, (0, y, 0), (k * 0.29) % 2.6, fps=36); k += 1
    if embers:  # flames thrown off above the point
        for i in range(5):
            card(L, f'm{k}', '1,0.2,0.06', sc, half * 0.8, half * 2.2, (random.uniform(-2, 2), Y1 + 4 + 5 * i, random.uniform(-2, 2)), (k * 0.53) % 2.6, fps=22); k += 1
    jf = WORK / f'madflame_{t}.job'
    jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(t, r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    for t in range(4): build(t)
