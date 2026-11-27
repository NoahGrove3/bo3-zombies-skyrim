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
    WORK.mkdir(parents=True, exist_ok=True)
    rows = [f'out {OUT}']
    for i in range(N):
        a = i / N * 2 * math.pi + 0.3 * math.sin(i * 1.7)
        r = R * (0.85 + 0.3 * ((i * 37) % 10) / 10)
        cx, cy = math.cos(a) * r, math.sin(a) * r
        tx, ty = -math.sin(a), math.cos(a)            # (each faces out from you)
        sc = 1.2 + 0.15 * (i % 3)
        rows.append(f'shape embers{i} tex=CurseOfAlduin\\wfx\\sig_embers.dds color=1,1,1,1 scale={sc:.2f} blend=add '
                    f'palette=CurseOfAlduin\\fx\\pal_fire.dds vscroll={0.22 + 0.06 * (i % 4):.2f}')
        steps = 6
        for s in range(steps + 1):
            k = s / steps
            z = H0 + (H1 - H0) * k
            al = math.sin(math.pi * k) ** 1.5   # (faded at the feet and over the head)
            for side in (-1, 1):
                x, y = cx + tx * W / 2 * side, cy + ty * W / 2 * side
                rows.append(f'v {x:.2f} {y:.2f} {z:.2f} {0.0 if side < 0 else 1.0} {k * 2.2 + i * 0.37:.3f} 1 1 1 {al:.3f}')
        for s in range(steps):
            b = s * 2
            rows += [f't {b} {b + 1} {b + 3}', f't {b} {b + 3} {b + 2}']
    jf = WORK / 'embers.job'
    jf.write_text('\n'.join(rows) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(jf)], capture_output=True, text=True)
    if r.returncode: raise SystemExit(f'fxwrite failed: {r.stderr[-400:]}')
    print(OUT.name, 'written')


if __name__ == '__main__':
    main()
