"""Bond II's Updraft shout: the whirlwind round you (src/Movement.cpp WhirlTick spins it, swells it and fades it).
    python tools/whirlwind.py <out.nif>
Unlit additive ribbons of air (tools/modelconv/fxwrite, the dragon's wind texture tools/dragonfx.py): four helices
winding up round you, widening as they rise, and a ring of air at your feet. Shapes WW1..WW5, unseen until the game
sets their brightness."""
import math, pathlib, subprocess, sys

FXWRITE = '/home/claude/tools/fxwrite'
TEX = r'textures\CurseOfAlduin\Dragon\wind.dds'


def main():
    out = sys.argv[1]
    L = [f'out {out}']
    for k in range(4):
        L.append(f'shape WW{k + 1} tex={TEX} color=0.88,0.94,1.0,1.0 scale=0 blend=add vscroll={0.9 + 0.15 * k}')
        n, turns, a0 = 28, 1.4, k * math.pi / 2
        for i in range(n + 1):
            t = i / n
            h = -10 + 420 * t
            r = 55 + 0.4 * h
            a = a0 + turns * 2 * math.pi * t
            wdt = 70 * (1 - 0.5 * t)
            alpha = min(1.0, t / 0.15) * min(1.0, (1 - t) / 0.3) * 0.85
            for s in (-1, 1):
                x, y = r * math.cos(a), r * math.sin(a)
                L.append(f'v {x:.2f} {y:.2f} {h + s * wdt / 2:.2f} {0.5 + 0.5 * s:.3f} {t * 3:.3f} 1 1 1 {alpha:.3f}')
        for i in range(n):
            q = 2 * i
            L += [f't {q} {q + 1} {q + 2}', f't {q + 1} {q + 3} {q + 2}']
    # the ring of air at your feet
    L.append(f'shape WW5 tex={TEX} color=0.9,0.95,1.0,1.0 scale=0 blend=add vscroll=0.6')
    n = 32
    for i in range(n + 1):
        a = 2 * math.pi * i / n
        for s, r in ((-1, 50), (1, 190)):
            L.append(f'v {r * math.cos(a):.2f} {r * math.sin(a):.2f} {4 + (s + 1) * 6:.2f} {0.5 + 0.5 * s:.3f} {i / n * 4:.3f} 1 1 1 {0.0 if s > 0 else 0.7}')
    for i in range(n):
        q = 2 * i
        L += [f't {q} {q + 1} {q + 2}', f't {q + 1} {q + 3} {q + 2}']
    job = pathlib.Path(out).with_suffix('.job.txt')
    job.write_text('\n'.join(L) + '\n')
    r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
    print(r.stdout, r.stderr)
    job.unlink()
    if r.returncode:
        raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
