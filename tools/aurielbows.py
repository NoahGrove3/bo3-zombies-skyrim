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
    vortex(z, data)


def vortex(z, data):
    """(Noah: the Subsuming Vortex should look like the elemental bows) the Ranger's wonder on the same bow: the Pink colour's
    textures turned black-violet with lilac runes (textures\\CurseOfAlduin\\aurieV\\), a deep violet glow"""
    import numpy as np, struct
    c = 'Pink'
    nif = z.read(f'Default/{c} Default/Data/meshes/dlc01/weapons/auriel/aurielsbow.nif')
    new = b'textures\\CurseOfAlduin\\aurieV\\'
    for f in (b'aurielsbow.DDS', b'aurielsbowbase_g.DDS'):
        nif = nif.replace(OLD + f, new + f)
    nif = bytearray(nif); struct.pack_into('<3f', nif, 340784, 0.55, 0.12, 1.0)
    (pathlib.Path(data) / 'meshes' / 'CurseOfAlduin' / 'bow' / 'vortex.nif').write_bytes(bytes(nif))
    td = pathlib.Path(data) / 'textures' / 'CurseOfAlduin' / 'aurieV'
    td.mkdir(parents=True, exist_ok=True)
    src = z.read(f'Default/{c} Default/Data/textures/dlc01/weapons/auriel/aurielsbow.dds')
    hdr, px = src[:128], np.frombuffer(src[128:], np.uint8).copy()   # A8R8G8B8, read as B,G,R,A per pixel (every mip alike)
    n = len(px) // 4; q = px[:n * 4].reshape(-1, 4).astype(np.float32) / 255
    lum = 0.3 * q[:, 2] + 0.59 * q[:, 1] + 0.11 * q[:, 0]
    t = np.clip((lum - 0.35) / 0.35, 0, 1)[:, None]
    body = np.stack([lum * 0.95, lum * 0.22, lum * 0.62], 1) * 1.25
    rune = np.stack([np.ones_like(lum), lum * 0.55 + 0.1, lum * 0.75 + 0.15], 1)
    q[:, :3] = np.clip(body * (1 - t) + rune * t, 0, 1)
    px[:n * 4] = (q * 255 + 0.5).astype(np.uint8).reshape(-1)
    (td / 'aurielsbow.dds').write_bytes(hdr + px.tobytes())
    (td / 'aurielsbowbase_g.dds').write_bytes(z.read(f'Default/{c} Default/Data/textures/dlc01/weapons/auriel/aurielsbowbase_g.dds'))
    print('vortex', c, '(recoloured violet)')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
