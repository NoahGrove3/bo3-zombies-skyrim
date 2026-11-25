"""You, in the workshop's ability pictures (Noah, 2026-11-25: "update the players model in the previews too to skyrim player
model" - a male Nord, in the class set of whoever uses the weapon (the relics, Tablet weapons and Alduin's Bane in the Summoner's),
holding the weapon and moving with each ability): Skyrim's male body parts (HIMBO's hands, Skyrim's head, eyes and hair, read
from Noah's game, never committed) dressed in our own class sets at Tier III (assets/fx/Meshes/CurseOfAlduin/armour), on Skyrim's
skeleton (XPMSE's, its WEAPON node in the right hand). Packed as tools/zombiepreview.py packs the dead; the page's zombies.js poses
it.

    python3 tools/playerpreview.py    -> build/preview/live/plr_skel.json, plr_<class>.json, live/tex/plr_*.jpg|png
"""
import base64, gzip, io, json, pathlib, sys
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import zombiepreview as ZP

P = ROOT / 'build' / 'player'
OUT = ROOT / 'build' / 'preview' / 'live'
ARM = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'armour'
TIER = 't3'
SETS = {   # (the Tier III set, weight 1; the alchemist and spellsword sets have no hood: Skyrim's hair)
    'summoner': ['robes', 'hood', 'boots', 'HANDS', 'HEAD', 'EYES'],
    'destruction': ['robes', 'hood', 'boots', 'HANDS', 'HEAD', 'EYES'],
    'alchemist': ['cuirass', 'boots', 'gauntlets', 'HEAD', 'EYES', 'HAIR'],
    'spellsword': ['cuirass', 'boots', 'gauntlets', 'HEAD', 'EYES', 'HAIR'],
}
OWN = {'HANDS': P / 'nif' / 'malehands_1.nif', 'HEAD': P / 'nif' / 'malehead.nif', 'EYES': P / 'nif' / 'eyesmale.nif', 'HAIR': P / 'nif' / 'hair01.nif'}
HAIR_TINT = [0.36, 0.25, 0.16]   # (a Nord's brown: Skyrim's hair textures are grey, tinted by the character's hair colour)


def texture(p, cut):
    """a shape's diffuse, small for the page: ours from the repo, Skyrim's from what was read from the game"""
    k = p.replace('/', '\\').lower()
    if k.startswith('textures\\'): k = k[9:]
    name = k.split('\\')[-1][:-4]
    if k.startswith('curseofalduin\\'):
        src = ROOT / 'assets' / 'fx' / 'Textures' / pathlib.Path(*p.replace('/', '\\').split('\\')[1:])
        if not src.exists(): src = next((s for s in (ROOT / 'assets' / 'fx' / 'Textures').rglob('*.dds') if s.name.lower() == name + '.dds'), None)
    else:
        src = next((P / 'tex' / (name + e) for e in ('.jpg', '.png') if (P / 'tex' / (name + e)).exists()), None)
    if not src: print('  no texture', p); return '', False
    im = Image.open(src).convert('RGBA'); im.thumbnail((1024, 1024))
    (OUT / 'tex').mkdir(parents=True, exist_ok=True)
    if cut: im.save(OUT / 'tex' / f'plr_{name}.png', optimize=True)
    else: im.convert('RGB').save(OUT / 'tex' / f'plr_{name}.jpg', quality=82)
    return f'plr_{name}', cut


def pack(shapes, extra):
    head, buf = [], io.BytesIO()
    for s in shapes:
        V = np.array(s['v'], np.float32).reshape(-1, 3); U = np.array(s['uv'], np.float32).reshape(-1, 2); T = np.array(s['t']).reshape(-1, 3)
        Wt = np.array(s['w'], np.float32).reshape(-1, 8)
        lo, hi = V.min(0) - 0.5, V.max(0) + 0.5; span = hi - lo
        buf.write(np.round((V - lo) / span * 65535 - 32768).astype('<i2').tobytes())
        buf.write(np.round(U * 8192).clip(-32768, 32767).astype('<i2').tobytes())
        buf.write(T.astype('<u2').tobytes())
        buf.write(Wt[:, :4].astype('u1').tobytes())
        buf.write(np.round(Wt[:, 4:] * 255).clip(0, 255).astype('u1').tobytes())
        cut = bool(s.get('alpha', 0) & 0x200)
        m, cut = texture(s['tex'], cut)
        h = dict(name=s['name'], n=len(V), nt=len(T), lo=lo.round(3).tolist(), span=span.round(3).tolist(), bones=s['bones'], s2b=s['s2b'], map=m, ext='png' if cut else 'jpg')
        if cut: h['cut'] = round(s.get('athr', 128) / 255, 3)
        h.update(extra.get(s['name'], {}))
        head.append(h)
    return head, base64.b64encode(gzip.compress(buf.getvalue(), 9)).decode()


def build():
    sk = ZP.read(P / 'nif' / 'skeleton.nif')
    used = {'WEAPON'}
    bodies = {}
    for cls, parts in SETS.items():
        shapes, extra = [], {}
        for part in parts:
            nif = OWN.get(part) or ARM / cls / f'{part}_m_{TIER}_1.nif'
            d = ZP.read(nif)
            for s in d['shapes']:
                if 'Boxers' in s['name']: continue
                shapes.append(s); used |= set(s['bones'])
                if part == 'HAIR': extra[s['name']] = {'tint': HAIR_TINT}
        bodies[cls] = shapes
    par = {n['name']: n['parent'] for n in sk['nodes']}
    keep = set()
    for b in used:
        c = b
        while c and c not in keep: keep.add(c); c = par.get(c, '')
    nodes = [dict(name=n['name'], parent=n['parent'] if n['parent'] in keep else '', x=n['x']) for n in sk['nodes'] if n['name'] in keep]
    (OUT / 'plr_skel.json').write_text(json.dumps({'nodes': nodes}, separators=(',', ':')))
    for cls, shapes in bodies.items():
        head, gz = pack(shapes, {})
        for h, s in zip(head, shapes):
            if s['name'].lower().startswith('hair') or 'hair' in s['tex'].lower(): h['tint'] = HAIR_TINT
        (OUT / f'plr_{cls}.json').write_text(json.dumps({'key': cls, 'shapes': head, 'gz': gz}, separators=(',', ':')))
        print(cls, len(head), 'shapes', (OUT / f'plr_{cls}.json').stat().st_size // 1024, 'KB')


if __name__ == '__main__':
    build()
