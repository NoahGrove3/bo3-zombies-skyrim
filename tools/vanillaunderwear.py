"""(Noah, 2026-11-22: "the female underwear is very broken - is there a base Skyrim underwear for females we can use, just like the
male underwear?") Skyrim's own: its female body carries her underwear as a shape of its own (FemaleUnderwear, its own texture) -
lifted out of the body, both weights, and refitted to 3BA by tools/armourfit.py like the class sets (Skyrim's own bones and
weights kept, the body's sliders carried onto it). Its texture is Skyrim's, read from the game; nothing of 3BA's is in it.

    python3 tools/vanillaunderwear.py  ->  assets/fx/Meshes/CurseOfAlduin/underwear/female_underwear_<0|1>.nif + female_underwear.tri"""
import pathlib, subprocess, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import armourfit as A

OUT = A.ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'underwear'
SRC = A.VAN / 'meshes' / 'actors' / 'character' / 'character assets'
TRI = ('CurseOfAlduin\\underwear\\female_underwear.tri', OUT / 'female_underwear.tri')

if __name__ == '__main__':
    for wt in '10':   # (_1 first: the sliders are written from it)
        A.fit(SRC / f'femalebody_{wt}.nif', 'female', OUT / f'female_underwear_{wt}.nif', role='piece', tri=TRI, keep={'FemaleUnderwear'}, clearance=0.25)
        # (Noah, 2026-11-23: "the female body still doesn't have underwear on in the race menu") lifted off Skyrim's body, its
        # skin still named the body's part (32) while our underwear is worn in slot 52 - and Skyrim never draws an addon's mesh
        # whose part isn't its own slot: its part made 52 (tools/modelconv/setpart.cpp)
        out = OUT / f'female_underwear_{wt}.nif'
        subprocess.run([str(A.TOOLS / 'setpart'), str(out), str(out), '52'], check=True)
