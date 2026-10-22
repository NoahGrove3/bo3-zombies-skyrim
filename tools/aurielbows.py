#!/usr/bin/env python3
"""(Noah: the six elemental bows rebuilt on Auriel's Bow of Light) Each of the Echo Altar's bows takes one colour of
"Auriel's Bow of Light" (Nexus 34730, the FOMOD zip in Noah's Downloads): its mesh, with the colour's two textures
(aurielsbow.DDS, aurielsbowbase_g.DDS) moved to textures\\CurseOfAlduin\\aurie<C>\\ so all six colours load side by
side (the path is rewritten in place, same length; the normal map, specular and cubemap stay Auriel's own).

    python3 tools/aurielbows.py <the FOMOD zip> <Data dir for the textures>

writes meshes/CurseOfAlduin/bow/<bow>.nif and the textures under <Data dir>. They are made from another author's mod,
so they go straight into the game and the Party Pack's files - never into this (public) repo.
"""
import pathlib, sys, zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
COLOUR = {'emberstring': 'Red', 'rimehorn': 'Blue', 'stormreach': 'Pink', 'skyhowl': 'White', 'dawnspire': 'Yellow', 'thirstbow': 'Red'}  # (the plan's table)
TINT = {'stormreach': (0.62, 0.33, 1.0), 'thirstbow': (0.62, 0.0, 0.06)}   # violet / crimson glow (the shader's emissive colour, at 340784)
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
        if bid in TINT:
            import struct
            nif = bytearray(nif); struct.pack_into('<3f', nif, 340784, *TINT[bid]); nif = bytes(nif)
        md = pathlib.Path(data) / 'meshes' / 'CurseOfAlduin' / 'bow'
        md.mkdir(parents=True, exist_ok=True)
        (md / f'{bid}.nif').write_bytes(nif)
        td = pathlib.Path(data) / 'textures' / 'CurseOfAlduin' / tag
        td.mkdir(parents=True, exist_ok=True)
        for f in ('aurielsbow.dds', 'aurielsbowbase_g.dds'):
            (td / f).write_bytes(z.read(f'Default/{c} Default/Data/textures/dlc01/weapons/auriel/{f}'))
        print(bid, c)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
