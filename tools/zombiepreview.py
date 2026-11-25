"""The night's own dead for the workshop's ability pictures (Noah, 2026-11-25: "make the enemy models in the previews actual zombie
models with reactions to the ability effects" - "Your game's own", reacting "as the game does"): the six Thriceborn (Mihail's CC
Zombies' bodies, tools/gen_esp.py THRICEBORN) and their skeleton (skeletonzombie.nif), read from Noah's game with
tools/modelconv/nifskin.cpp and packed small for the page, which poses them itself (the workshop's zombies.js).

Reads build/zombies/nif/*.nif and build/zombies/tex/*.png|jpg (staged from his game, never committed); writes
build/preview/live/zom_skel.json, zom_<key>.json and live/tex/zom_<texture>.jpg.

    python3 tools/zombiepreview.py
"""
import base64, gzip, io, json, pathlib, subprocess, sys
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
Z = ROOT / 'build' / 'zombies'
OUT = ROOT / 'build' / 'preview' / 'live'
TOOLS = pathlib.Path('/home/claude/tools')
sys.path.insert(0, str(ROOT / 'tools'))
# (tools/gen_esp.py THRICEBORN: the night pool's six)
THRICEBORN = [('mangled', 'zombie01.nif'), ('foul', 'zombie02.nif'), ('putrid', 'zombie03putrid.nif'), ('malignant', 'zombie02dread.nif'),
              ('relentless', 'zombie03.nif'), ('dread_putrid', 'zombie01putrid.nif')]


def tool():
    exe = Z / 'nifskin'
    if not exe.exists():
        subprocess.run(['g++', '-std=c++20', '-O1', f'-I{TOOLS}/nifly/include', f'-I{TOOLS}/nifly/external', str(ROOT / 'tools' / 'modelconv' / 'nifskin.cpp'),
                        str(TOOLS / 'nifly' / 'build' / 'src' / 'libnifly.a'), '-o', str(exe)], check=True)
    return exe


def read(nif):
    return json.loads(subprocess.run([str(tool()), str(nif)], capture_output=True, text=True, check=True).stdout)


def texname(p):
    n = p.replace('/', '\\').split('\\')[-1][:-4].lower()
    return n


def tex(n):
    src = next((Z / 'tex' / (n + e) for e in ('.png', '.jpg') if (Z / 'tex' / (n + e)).exists()), None)
    if not src: return ''
    (OUT / 'tex').mkdir(parents=True, exist_ok=True)
    im = Image.open(src).convert('RGB'); im.thumbnail((1024, 1024))
    im.save(OUT / 'tex' / f'zom_{n}.jpg', quality=82)
    return f'zom_{n}'


def build():
    sk = read(Z / 'nif' / 'skeletonzombie.nif')
    used = set()
    bodies = {k: read(Z / 'nif' / nif) for k, nif in THRICEBORN}
    for b in bodies.values():
        for s in b['shapes']: used |= set(s['bones'])
    par = {n['name']: n['parent'] for n in sk['nodes']}
    keep = set()
    for b in used:
        c = b
        while c and c not in keep: keep.add(c); c = par.get(c, '')
    nodes = [dict(name=n['name'], parent=n['parent'] if n['parent'] in keep else '', x=n['x']) for n in sk['nodes'] if n['name'] in keep]
    (OUT / 'zom_skel.json').write_text(json.dumps({'nodes': nodes}, separators=(',', ':')))
    for k, b in bodies.items():
        head, buf = [], io.BytesIO()
        for s in b['shapes']:
            V = np.array(s['v'], np.float32).reshape(-1, 3); U = np.array(s['uv'], np.float32).reshape(-1, 2); T = np.array(s['t']).reshape(-1, 3)
            Wt = np.array(s['w'], np.float32).reshape(-1, 8)
            lo, hi = V.min(0) - 0.5, V.max(0) + 0.5; span = hi - lo
            buf.write(np.round((V - lo) / span * 65535 - 32768).astype('<i2').tobytes())
            buf.write(np.round(U * 8192).clip(-32768, 32767).astype('<i2').tobytes())
            buf.write(T.astype('<u2').tobytes())
            buf.write(Wt[:, :4].astype('u1').tobytes())
            buf.write(np.round(Wt[:, 4:] * 255).clip(0, 255).astype('u1').tobytes())
            head.append(dict(name=s['name'], n=len(V), nt=len(T), lo=lo.round(3).tolist(), span=span.round(3).tolist(), bones=s['bones'], s2b=s['s2b'],
                             map=tex(texname(s['tex'])), nrm=tex(texname(s['nrm'])) if s['nrm'] else ''))
        (OUT / f'zom_{k}.json').write_text(json.dumps({'key': k, 'shapes': head, 'gz': base64.b64encode(gzip.compress(buf.getvalue(), 9)).decode()}, separators=(',', ':')))
        print(k, (OUT / f'zom_{k}.json').stat().st_size // 1024, 'KB')


if __name__ == '__main__':
    build()
