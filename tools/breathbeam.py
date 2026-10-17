"""The dragon's breath (src/Movement.cpp BeamPose): one beam of fire, its own nif hung on your 3D while it breathes and
placed each frame at the dragon's jaws, turned to where it breathes (as the whirlwind is: shapes moved under the dragon's
own animated nodes were never drawn). Three cones of flame along +Y from the origin, in the dragon's units (the game scales
them with the dragon): BB1 the white-hot core, BB2 the flame round it (alpha-blended, so it shows against the sky), BB3 the
flare at the jaws. Texture: Textures/CurseOfAlduin/Dragon/beam.dds (tools/dragonfx.py).
    python tools/breathbeam.py <out.nif>"""
import math, pathlib, subprocess, sys
FXWRITE = '/home/claude/tools/fxwrite'
BEAM = r'textures\CurseOfAlduin\Dragon\beam.dds'


def cone(L, name, length, r0, r1, a0, a1, tiles, sides=14, segs=26, fade_out=0.2, blend='add'):
    L.append(f'shape {name} tex={BEAM} color=1,1,1,1 scale=0 blend={blend}')
    for i in range(segs + 1):
        t = i / segs
        y = 10 + (length - 10) * t
        r = r0 + (r1 - r0) * t ** 0.8
        al = (a0 + (a1 - a0) * t) * (min(1.0, t / 0.03) if t > 0 else 0.0) * min(1.0, (1 - t) / fade_out)
        for k in range(sides + 1):
            ang = 2 * math.pi * k / sides
            L.append(f'v {r * math.cos(ang):.2f} {y:.2f} {r * math.sin(ang):.2f} {2.0 * k / sides:.4f} {t * tiles:.3f} 1 1 1 {al:.3f}')
            L.append(f'n {math.cos(ang):.4f} 0 {math.sin(ang):.4f}')
    for i in range(segs):
        for k in range(sides):
            a = i * (sides + 1) + k
            b, c, d = a + 1, a + sides + 1, a + sides + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}']


def main():
    out = sys.argv[1]
    L = [f'out {out}']
    cone(L, 'BB1', 7000, 18, 150, 0.9, 0.55, 5.8)
    cone(L, 'BB2', 7000, 55, 430, 0.6, 0.18, 4.2, blend='alpha')
    cone(L, 'BB3', 900, 50, 230, 0.9, 0.0, 1.0, segs=8, fade_out=0.6)
    job = pathlib.Path(out).with_suffix('.job.txt')
    job.write_text('\n'.join(L) + '\n')
    r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
    print(r.stdout, r.stderr)
    job.unlink()
    if r.returncode:
        raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
