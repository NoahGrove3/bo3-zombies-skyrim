"""(Noah: "have all the classes have the same armor (dark mage set the one given in the destruction and alchemist class) set
with different accent colors matching their color class") the Black Mage robes' models and colour maps, copied for each class
with its accents - the stitching, lacing, trims and buckles, what stands out lighter than the black cloth round it - in the
class's colour (the Summoner's violet, Destruction's ember orange, the Alchemist's green, the Spellsword's blue), the cloth left black.

Built from the mod's own archives on this machine (never committed: they are its author's), into build/classarmor/Data:
    python3 tools/classarmor.py "<Black Mage Armor SE.bsa>" "<Black Mage Armor SE - Textures.bsa>"
-> Meshes/CurseOfAlduin/classarmor/<class>/*.nif, Textures/UNarmor_<A-D>/*.dds"""
import json, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'build' / 'classarmor'
RAW = OUT / 'raw'
DATA = OUT / 'Data'
COL = {'summoner': (0.62, 0.36, 1.0), 'destruction': (1.0, 0.43, 0.12), 'alchemist': (0.36, 0.92, 0.24), 'spellsword': (0.31, 0.6, 1.0)}   # (the classes' own colours: their cards, their armour's glow)
# (Noah: "my characters body was invisible" - rewritten by nifly the robes no longer drew) the models are copied byte for
# byte and only their colour maps' folder is changed, in place, to one of the same length: textures\BlackMage\ ->
# textures\UNarmor_A\ (A-D the four classes) - nothing else in the file moves
TEXDIR = {'summoner': 'UNarmor_A', 'destruction': 'UNarmor_B', 'alchemist': 'UNarmor_C', 'spellsword': 'UNarmor_D'}


def retex(b, cls):
    import re
    new = ('textures\\' + TEXDIR[cls] + '\\').encode()
    def sub(mm):
        tail = b[mm.end():mm.end() + 80]
        name = tail.split(b'.dds')[0].lower() if b'.dds' in tail.lower() else b''
        return mm.group(0) if name.endswith(b'_n') else new
    out = re.sub(rb'(?i)textures\\blackmage\\', sub, b)
    assert len(out) == len(b)
    return out
BSAX = '/home/claude/tools/bsax.py'


def tint(src, cls, dst):
    im = Image.open(src).convert('RGBA')
    a = np.asarray(im).astype(np.float32) / 255
    rgb, al = a[..., :3], a[..., 3:]
    l = rgb.mean(-1)
    W = im.size[0]
    blur = lambda x, r: np.asarray(Image.fromarray((np.clip(x, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r))).astype(np.float32) / 255
    bl = blur(l, max(4, W // 128))
    detail = np.clip((l - bl - 0.022) / 0.08, 0, 1)            # lighter than its surround: stitches, laces, edges
    bright = np.clip((l - 0.18) / 0.22, 0, 1)                  # the light parts outright: buckles, trims
    m = blur(np.clip(detail * 0.9 + bright * 0.75, 0, 1), max(1, W // 1024))
    c = np.array(COL[cls])
    coloured = c * (np.maximum(l, 0.04)[..., None] * 2.8 + 0.06)
    o = rgb * (1 - m[..., None]) + coloured * m[..., None]
    res = np.concatenate([np.clip(o, 0, 1), al], -1)
    dst.parent.mkdir(parents=True, exist_ok=True)
    png = RAW / 'tmp.png'
    Image.fromarray((res * 255).astype(np.uint8), 'RGBA').save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=12', str(dst)], check=True)


def main(mesh_bsa, tex_bsa):
    T = json.load(open(ROOT / 'tools' / 'templates' / 'blackmage_set.json'))['pieces']
    models = set()
    for p in T.values():
        for a in p['armas']:
            for name, hx in a['subs']:
                if name in ('MOD2', 'MOD3', 'MOD4', 'MOD5'): models.add(bytes.fromhex(hx).rstrip(b'\0').decode('latin1').lower())
    RAW.mkdir(parents=True, exist_ok=True)
    subprocess.run(['python3', BSAX, mesh_bsa, str(RAW)] + [m.split('\\')[-1] for m in models], check=True, capture_output=True)
    texs = set()
    for m in models:
        f = RAW / 'meshes' / m.replace('\\', '/')
        if not f.exists(): print('missing', m); continue
        for s in subprocess.run(['strings', '-n', '6', str(f)], capture_output=True, text=True).stdout.split('\n'):
            s = s.strip().lower()
            if s.startswith('textures\\blackmage\\') and '.dds' in s and '_n.dds' not in s: texs.add(s[:s.index('.dds') + 4])
    subprocess.run(['python3', BSAX, tex_bsa, str(RAW)] + [t.split('\\')[-1] for t in texs], check=True, capture_output=True)
    for cls in COL:
        for t in sorted(texs):
            src = RAW / t.replace('\\', '/')
            name = t.split('\\')[-1]
            tint(src, cls, DATA / 'Textures' / TEXDIR[cls] / name)
        for m in sorted(models):
            src = RAW / 'meshes' / m.replace('\\', '/')
            dst = DATA / 'Meshes' / 'CurseOfAlduin' / 'classarmor' / cls / m.split('\\')[-1]
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(retex(src.read_bytes(), cls))
        print(cls, len(texs), 'maps', len(models), 'models')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
