"""(Noah, 2026-11-22: "the female underwear is very broken - is there a base Skyrim underwear for females we can use, just like the
male underwear?") Skyrim's own: its female body carries her underwear as a shape of its own (FemaleUnderwear, its own texture) -
lifted out of the body and refitted to 3BA by tools/armourfit.py like the class sets (Skyrim's own bones and weights kept, the
body's sliders carried onto it). Its texture is Skyrim's, read from the game; nothing of 3BA's is in it.

(2026-11-23: "the bra looks strange like there's too much of a gap from the skin" / "with the weight slider the clothes change
size but the body doesn't") laid back onto the skin at Skyrim's own small offset (fit's hug), and the thin weight (_0) is the
_1 fit moved by the player's own body's weight difference - so it changes with the weight slider exactly as much as the body
does (with Noah's 3BA, not at all).

    python3 tools/vanillaunderwear.py  ->  assets/fx/Meshes/CurseOfAlduin/underwear/female_underwear_<0|1>.nif + female_underwear.tri"""
import pathlib, subprocess, sys
import numpy as np
from scipy.spatial import cKDTree
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import armourfit as A

OUT = A.ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'underwear'
SRC = A.VAN / 'meshes' / 'actors' / 'character' / 'character assets'
TRI = ('CurseOfAlduin\\underwear\\female_underwear.tri', OUT / 'female_underwear.tri')
SHAPE = 'FemaleUnderwear'


def setpart(out):
    # (2026-11-23: "the female body still doesn't have underwear on in the race menu") lifted off Skyrim's body, its skin still
    # names the body's part (32) while our underwear is worn in slot 52 - and Skyrim never draws an addon's mesh whose part
    # isn't its own slot: its part made 52 (tools/modelconv/setpart.cpp)
    subprocess.run([str(A.TOOLS / 'setpart'), str(out), str(out), '52'], check=True)


if __name__ == '__main__':
    out1 = OUT / 'female_underwear_1.nif'
    P1 = A.fit(SRC / 'femalebody_1.nif', 'female', out1, role='piece', tri=TRI, keep={SHAPE}, clearance=0.25, hug=(0.25, 0.5))[SHAPE]
    setpart(out1)
    # the thin weight: the full one moved by the body's own _1 -> _0 difference (its nearest 6 vertices, the nearest counting most)
    B1 = A.shapes_global(A.GAME / 'femalebody_1.nif')['3BA'][0]
    B0 = A.shapes_global(A.GAME / 'femalebody_0.nif')['3BA'][0]
    d, j = cKDTree(B1).query(P1, k=6)
    w = 1 / np.maximum(d, 0.3) ** 2; w /= w.sum(1, keepdims=True)
    P0 = P1 + ((B0 - B1)[j] * w[..., None]).sum(1)
    src0 = SRC / 'femalebody_0.nif'
    job = [f'armour {src0}', f'out {OUT / "female_underwear_0.nif"}'] + [f'drop {n}' for n in A.shapes_global(src0) if n != SHAPE]
    f = A.WORK / 'female_underwear_0_pos.f32'; f.write_bytes(P0.astype('<f4').tobytes())
    job.append(f'pos {SHAPE} {f}')
    jf = A.WORK / 'female_underwear_0.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([str(A.TOOLS / 'armourfit'), str(jf)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('armourfit failed')
    setpart(OUT / 'female_underwear_0.nif')
    print('female_underwear_0.nif: the _1 fit moved by the body\'s weight (max', round(float(np.linalg.norm(P0 - P1, axis=1).max()), 2), ')')
