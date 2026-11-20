"""(Noah, 2026-11-19: the Soulcaller's spectral sabre cats "have the eye sockets in their model have flaming eyes ... blue in
color to match the ... ghostly effect") one small blue flame licking up out of an eye socket - Skyrim's own fire flipbook on
camera-facing cards (as tools/purplefire.py), its blue ramp; two of them hung on each cat's head (src/Classes.cpp EyeFlames).
The model's origin is the flame's foot.

    python3 tools/eyeflame.py   ->  assets/fx/Meshes/CurseOfAlduin/fx/eyeflame_blue.nif"""
import pathlib, subprocess
import realfire

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'eyeflame_blue.nif'
WORK = ROOT / 'build' / 'eyeflame'
FX = 'CurseOfAlduin\\bow\\emberstring_arrowfx.dds'


def card(L, name, col, sc, w, h, at, phase, fps=28):
    L.append(f'shape {name} tex={FX} color={col},1 scale={sc} blend=add billboard=2 flip=8x8 fps={fps} phase={phase:.2f} at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
    for X, Y, u, v in ((-w, h, 0, 0), (w, h, 1 / 8, 0), (w, -h * 0.35, 1 / 8, 1 / 8), (-w, -h * 0.35, 0, 1 / 8)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u} {v} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    L = [f'out {OUT}']
    card(L, 'ef0', '1,1,1', 1.6, 2.6, 7.0, (0, 0, 1.0), 0.0)        # the white-hot core
    card(L, 'ef1', '0.8,0.8,0.8', 1.5, 3.4, 9.5, (0.6, 0.3, 0.5), 0.9)   # the blue body
    card(L, 'ef2', '0.6,0.6,0.6', 1.3, 3.0, 8.0, (-0.6, -0.3, 0.0), 1.7)
    jf = WORK / 'eyeflame.job'
    jf.write_text('\n'.join(realfire.fix(L, 'blue')) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
