"""(Noah, Nov 27 final - the armour sets' looks: "Embers: small bright embers shed off the set as you move, drifting up and winking
out") the Embers aura: a ring of tall ribbons of rising embers standing round the body, their light faded at the feet and over the
head, the embers climbing them (a greyscale ember strip scrolled up, through a palette src/Classes.cpp paints in the class's colour
at the tier's strength). Hung on you as a hit effect's art (tools/gen_esp.py COA_Art_armembers).

    python3 tools/auraembers.py  ->  assets/fx/Meshes/CurseOfAlduin/fx/aura_embers.nif"""
import math, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'aura_embers.nif'
WORK = ROOT / 'build' / 'auraembers'
TOOLS = pathlib.Path('/home/claude/tools')

N = 9            # ribbons round you
R = 26.0         # their distance from your middle
H0, H1 = 8.0, 150.0
W = 15.0         # each ribbon's width


def main():
    # (Noah, Nov 27 test of 3edd0b7: the armour effect "does not look how it does in the preview" - flat ribbons, seen edge-on from the
    # side) the preview's embers: bright motes shed all round the set, drifting up and winking out - each a camera-facing card of the
    # looping mote flipbook (tools/weaponfx.py aura), so they are the same from every side; their light fades at the feet and over the head
    WORK.mkdir(parents=True, exist_ok=True)
    rows = [f'out {OUT}']
    import random
    rnd = random.Random(1127)
    for i in range(34):
        a = rnd.random() * 2 * math.pi
        r = R * (0.7 + 0.5 * rnd.random())
        z = H0 + 12 + (H1 - H0 - 20) * rnd.random()
        k = (z - H0) / (H1 - H0)
        al = max(0.15, math.sin(math.pi * k) ** 1.2)
        h = 30.0 * (0.8 + 0.4 * rnd.random())
        rows.append(f'shape embers{i} tex=CurseOfAlduin\\wfx\\aura_motes_rise.dds color=1,1,1,1 scale=1.6 blend=add '
                    f'palette=CurseOfAlduin\\fx\\pal_fire.dds billboard=2 flip=8x8 fps=26 phase={rnd.random() * 2.4:.2f} '
                    f'at={math.cos(a) * r:.2f},{math.sin(a) * r:.2f},{z:.2f}')
        for X, Y, u, v in ((-h / 2, h / 2, 0, 0), (h / 2, h / 2, 1 / 8, 0), (h / 2, -h / 2, 1 / 8, 1 / 8), (-h / 2, -h / 2, 0, 1 / 8)):
            rows.append(f'v {X:.2f} {Y + h * 0.3:.2f} 0 {u} {v} 1 1 1 {al:.3f}')
        rows += ['t 0 1 2', 't 0 2 3']
    jf = WORK / 'embers.job'
    jf.write_text('\n'.join(rows) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(jf)], capture_output=True, text=True)
    if r.returncode: raise SystemExit(f'fxwrite failed: {r.stderr[-400:]}')
    print(OUT.name, 'written')


if __name__ == '__main__':
    main()
