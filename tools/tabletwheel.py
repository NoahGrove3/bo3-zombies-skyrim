"""(Noah, Nov 27 - the Tablet's roll, his pick "The Wheel": "The weapons circle the Tablet on a ring of runes, spinning fast and
easing to a stop with the won one in front; it floats in to you") the ring of runes the weapons ride on: a flat band of glowing
runes lying level round the tablet at the height of its pool, a soft glow under it and a thin bright rim at each edge.
src/Tablet.cpp TickWheel turns it with the weapons.

    python3 tools/tabletwheel.py  ->  assets/fx/Meshes/CurseOfAlduin/tablet/wheel_runes.nif"""
import math, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'tablet' / 'wheel_runes.nif'
WORK = ROOT / 'build' / 'tabletwheel'
TOOLS = pathlib.Path('/home/claude/tools')

R0, R1 = 54.0, 70.0      # the rune band (inner, outer radius) - the weapons ride at 62 (src/Tablet.cpp kWheelR)
SEG = 120


def band(rows, name, tex, col, r0, r1, z, reps, scroll, a0=1.0, a1=1.0, extra=''):
    rows.append(f'shape {name} tex={tex} color={col} scale=1.6 blend=add uscroll={scroll}{extra}')
    for j in range(SEG + 1):
        th = 2 * math.pi * j / SEG
        for r, v, a in ((r0, 0.0, a0), (r1, 1.0, a1)):
            rows.append(f'v {r * math.cos(th):.2f} {r * math.sin(th):.2f} {z:.2f} {reps * j / SEG:.4f} {v} 1 1 1 {a:.3f}')
    for j in range(SEG):
        b = 2 * j
        rows += [f't {b} {b + 2} {b + 1}', f't {b + 1} {b + 2} {b + 3}', f't {b} {b + 1} {b + 2}', f't {b + 1} {b + 3} {b + 2}']


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    rows = [f'out {OUT}']
    band(rows, 'Runes', 'CurseOfAlduin\\priests\\moro_runeband.dds', '1,0.72,0.38,0.95', R0, R1, 0.0, 14, 60)
    # (a soft glow spread under the band, faded at both edges)
    rows.append(f'shape Glow tex=CurseOfAlduin\\wfx\\sig_band.dds color=1,0.6,0.25,0.45 scale=1.4 blend=add uscroll=-30')
    n = 3
    for j in range(SEG + 1):
        th = 2 * math.pi * j / SEG
        for k, r in enumerate((R0 - 14, (R0 + R1) / 2, R1 + 14)):
            a = 1.0 if k == 1 else 0.0
            rows.append(f'v {r * math.cos(th):.2f} {r * math.sin(th):.2f} -0.4 {6 * j / SEG:.4f} {k / 2} 1 1 1 {a}')
    for j in range(SEG):
        for k in range(2):
            a, b, c, d = j * n + k, j * n + k + 1, (j + 1) * n + k, (j + 1) * n + k + 1
            rows += [f't {a} {c} {b}', f't {b} {c} {d}', f't {a} {b} {c}', f't {b} {d} {c}']
    for nm, r in (('RimIn', R0), ('RimOut', R1)):   # (a thin bright line at each edge)
        band(rows, nm, 'CurseOfAlduin\\wfx\\sig_band.dds', '1,0.85,0.55,1', r - 0.7, r + 0.7, 0.2, 20, 6)
    jf = WORK / 'wheel.job'
    jf.write_text('\n'.join(rows) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(jf)], capture_output=True, text=True)
    if r.returncode: raise SystemExit(f'fxwrite failed: {r.stdout[-400:]} {r.stderr[-400:]}')
    print(OUT.name, 'written')


if __name__ == '__main__':
    main()
