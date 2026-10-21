#!/usr/bin/env python3
"""(Noah: the six elemental bows rebuilt on Auriel's Bow of Light) Each of the Echo Altar's bows takes one colour of
"Auriel's Bow of Light" (Nexus 34730, the FOMOD zip in Noah's Downloads): its mesh, with the colour's two textures
(aurielsbow.DDS, aurielsbowbase_g.DDS) moved to textures\\CurseOfAlduin\\aurie<C>\\ so all six colours load side by
side (the path is rewritten in place, same length; the normal map, specular and cubemap stay Auriel's own).

    python3 tools/aurielbows.py <the FOMOD zip> <Data dir for the textures>

writes assets/fx/Meshes/CurseOfAlduin/bow/<bow>.nif and the textures under <Data dir> (they are 22 MB each, so they
ship with the Party Pack's files, not in the repo).
"""
import pathlib, sys, zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
COLOUR = {'emberstring': 'Red', 'rimehorn': 'White', 'stormreach': 'Blue', 'skyhowl': 'Green', 'dawnspire': 'Yellow', 'thirstbow': 'Pink'}
OLD = b'textures\\dlc01\\weapons\\auriel\\'


def main(zpath, data):
    z = zipfile.ZipFile(zpath)
    for bid, c in COLOUR.items():
        tag = 'aurie' + c[0]
        new = b'textures\\CurseOfAlduin\\' + tag.encode() + b'\\'
        assert len(new) == len(OLD)
        nif = z.read(f'Default/{c} Default/Data/meshes/dlc01/weapons/auriel/aurielsbow.nif')
        for f in (b'aurielsbow.DDS', b'aurielsbowbase_g.DDS'):
            assert nif.count(OLD + f) == 1
            nif = nif.replace(OLD + f, new + f)
        (ROOT / 'assets/fx/Meshes/CurseOfAlduin/bow' / f'{bid}.nif').write_bytes(nif)
        td = pathlib.Path(data) / 'textures' / 'CurseOfAlduin' / tag
        td.mkdir(parents=True, exist_ok=True)
        for f in ('aurielsbow.dds', 'aurielsbowbase_g.dds'):
            (td / f).write_bytes(z.read(f'Default/{c} Default/Data/textures/dlc01/weapons/auriel/{f}'))
        print(bid, c)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
