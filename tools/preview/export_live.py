"""Live previews for the review doc's weapon workshop: each weapon's model and its effect art in all five looks, packed small
(positions and uvs as 16-bit integers, gzipped) for the page to draw and re-colour itself.

    python3 tools/preview/export_live.py <art key> ...   ->  build/preview/live/<key>.json  (+ tex/<name>.png, shared once)

Each file: {"key", "scale" (the model root's own scale), "box", "shapes": [...], "looks": {look: [fx shapes]}, "gz": base64(gzip(binary))}.
"""
import sys, json, base64, gzip, io, pathlib, subprocess, struct
import numpy as np
from PIL import Image
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import weaponfx as W
OUT = ROOT / 'build' / 'preview' / 'live'; OUT.mkdir(parents=True, exist_ok=True)
TEXROOTS = [ROOT / 'assets' / 'fx' / 'Textures', ROOT / 'assets' / 'Textures']
GUESS = [('glass', '#5f8f62'), ('ebony', '#2a2630'), ('daedric', '#2b1a1c'), ('dragonbone', '#d8ccb0'), ('dbone', '#d8ccb0'), ('bone', '#cfc2a6'),
         ('stalhrim', '#8fb8d0'), ('stahlrim', '#8fb8d0'), ('silver', '#c8ccd4'), ('dwarven', '#a8804a'), ('falmer', '#7a6a4a'),
         ('forsworn', '#6a5a48'), ('nordic', '#9a9aa2'), ('vampire', '#5a3238'), ('gold', '#c9a04a'), ('leather', '#4a3426'),
         ('wood', '#5a4028'), ('steel', '#8e9298'), ('iron', '#6e6e72'), ('orcish', '#4e5a48'), ('elven', '#c4a868')]


def guess(s):
    s = s.lower()
    for k, c in GUESS:
        if k in s: return c
    return '#7a7c82'


def find_tex(rel):
    rel = rel.replace('\\', '/')
    if rel.lower().startswith('textures/'): rel = rel[9:]
    for r in TEXROOTS:
        p = r / rel
        if p.exists(): return p
        if p.parent.exists():
            for c in p.parent.iterdir():
                if c.name.lower() == p.name.lower(): return c
    # (Skyrim's own weapon textures the scythes wear: read small from Noah's game - tools/scythes.py)
    j = W.ROOT / 'build' / 'scythes' / 'tex' / (rel.lower().replace('/', '__').replace(' ', '_')[:-4] + '.jpg')
    return j if j.exists() else None


def root_scale(path):
    out = subprocess.run([str(W.TOOLS / 'niftree'), str(path)], capture_output=True, text=True).stdout.splitlines()
    try: return float(out[0].split(' s=')[1].split()[0])
    except Exception: return 1.0


def read_model(path):
    raw = subprocess.run([str(W.TOOLS / 'nifworld'), str(path)], capture_output=True, text=True, check=True).stdout
    shapes, cur = [], None
    for l in raw.splitlines():
        if l.startswith('shape '):
            p = l[6:].split('\t'); cur = {'name': p[0], 'tex': p[1], 'kind': p[2], 'v': [], 'uv': [], 't': []}; shapes.append(cur)
        elif l.startswith('v '):
            q = l.split(); cur['v'].append([float(x) for x in q[1:4]]); cur['uv'].append([float(q[4]), float(q[5])] if len(q) >= 6 else [0, 0])
        elif l.startswith('t '): cur['t'].append([int(x) for x in l.split()[1:4]])
    import re
    return [s for s in shapes if s['t'] and (s['kind'] == 'lit' or not re.search(r'haze|glow|bead|aura|mote|spark|mist(?!blade)', s['name'], re.I))]   # (the steel, and a ghostly blade - not the soft glows, which the game softens itself)


def art_rows(key, look):
    """the art's fxwrite rows for this weapon in the given look (its own fit, its own colours)"""
    Fs = FITS[key]
    rows = []
    rng = np.random.default_rng(abs(hash(key)) % 2 ** 31)
    for pi, F in enumerate(Fs):
        F2 = dict(F); F2['look'] = look
        W.art_part(F2, rows, rng, f'p{pi}_' if len(Fs) > 1 else '')
    return rows


def parse_rows(rows):
    shapes, cur = [], None
    for l in rows:
        if l.startswith('shape '):
            parts = l.split(); kv = dict(p.split('=', 1) for p in parts[2:] if '=' in p)
            stem = kv.get('palette', 'x_white.dds').split('\\')[-1][:-4]
            cur = {'tex': kv['tex'].split('\\')[-1][:-4], 'pal': stem.split('_', 1)[1], 'scale': float(kv.get('scale', 1)), 'v': [], 't': []}
            if stem.startswith('pald_'): cur['dark'] = 1
            if kv.get('blend') == 'alpha': cur['alpha'] = 1
            if 'flip' in kv: cur['flip'] = [int(x) for x in kv['flip'].split('x')]; cur['fps'] = float(kv['fps']); cur['phase'] = float(kv.get('phase', 0))
            if 'vscroll' in kv: cur['vscroll'] = float(kv['vscroll'])
            if 'pulse' in kv: cur['pulse'] = [float(x) for x in kv['pulse'].split(',')]
            if 'clamp' in kv: cur['clamp'] = int(kv['clamp'])
            if 'billboard' in kv: cur['bb'] = 1; cur['at'] = [float(x) for x in kv['at'].split(',')]
            shapes.append(cur)
        elif l.startswith('v '):
            p = [float(x) for x in l.split()[1:]]; cur['v'].append(p[:5] + [p[8]])
        elif l.startswith('t '): cur['t'].append([int(x) for x in l.split()[1:4]])
    return shapes


FITS = {}


def export(key):
    rel = (W.ALL.get(key) or W.CLASS[key])[0]
    path = W.model_path(rel)
    FITS[key] = W.fit(key)
    model = read_model(path)
    looks = {lk: parse_rows(art_rows(key, lk)) for lk in ('o1', 'o2', 'o3', 'o4', 'o5')}   # (Noah, 2026-11-25: each weapon's own five looks)
    allp = np.concatenate([np.array(s['v']) for s in model] + [np.array([v[:3] for v in sh['v']]) for lk in looks.values() for sh in lk if sh['v']])
    lo, hi = allp.min(0) - 1, allp.max(0) + 1
    span = np.maximum(hi - lo, 1e-3)
    buf = io.BytesIO()
    def pos(P): return np.round((np.asarray(P) - lo) / span * 65535 - 32768).clip(-32768, 32767).astype('<i2')
    def uvq(U): return np.round(np.asarray(U) * 2048).clip(-32768, 32767).astype('<i2')
    def idx(T, n): return np.asarray(T, dtype='<u2' if n < 65536 else '<u4')
    head = []
    texcache = {}
    for s in model:
        V = np.array(s['v']); U = np.array(s['uv']); T = np.array(s['t'])
        tp = find_tex(s['tex']) if s['tex'] else None
        m = {'k': 'm', 'n': len(V), 'nt': len(T), 'i32': len(V) >= 65536}
        if s['kind'] == 'effect': m['glow'] = 1
        if tp:
            if tp not in texcache:
                try:
                    if s['kind'] == 'effect':   # (a glow keeps its alpha)
                        im = Image.open(tp).convert('RGBA'); im.thumbnail((256, 256)); b = io.BytesIO(); im.save(b, 'PNG', optimize=True)
                        texcache[tp] = 'data:image/png;base64,' + base64.b64encode(b.getvalue()).decode()
                    else:
                        im = Image.open(tp).convert('RGB'); im.thumbnail((384, 384)); b = io.BytesIO(); im.save(b, 'JPEG', quality=78)
                        texcache[tp] = 'data:image/jpeg;base64,' + base64.b64encode(b.getvalue()).decode()
                except Exception: texcache[tp] = None
            if texcache[tp]: m['map'] = texcache[tp]
        if 'map' not in m: m['color'] = guess(s['tex'] + ' ' + s['name'])
        head.append(m)
        buf.write(pos(V).tobytes()); buf.write(uvq(U).tobytes()); buf.write(idx(T, len(V)).tobytes())
    lookhead = {}
    for lk, shapes in looks.items():
        lh = []
        for sh in shapes:
            A = np.array(sh['v']); T = np.array(sh['t'])
            d = {k: sh[k] for k in ('tex', 'pal', 'scale', 'flip', 'fps', 'phase', 'vscroll', 'pulse', 'clamp', 'bb', 'at', 'dark', 'alpha') if k in sh}
            d.update(n=len(A), nt=len(T), i32=len(A) >= 65536)
            lh.append(d)
            buf.write(pos(A[:, :3] if not sh.get('bb') else A[:, :3] * 0 + (np.array(sh['at']) if sh.get('bb') else 0)).tobytes())
            buf.write(uvq(A[:, 3:5]).tobytes())
            buf.write(np.round(np.clip(A[:, 5], 0, 1) * 255).astype('u1').tobytes())
            buf.write(idx(T, len(A)).tobytes())
            if sh.get('bb'): d['corner'] = np.round(A[:, :2], 3).ravel().tolist()   # (a card's corners, in its own plane)
        lookhead[lk] = lh
    gz = gzip.compress(buf.getvalue(), 9)
    rec = {'key': key, 'c1': (W.ALL.get(key) or W.CLASS[key])[4], 'scale': root_scale(path), 'lo': lo.round(4).tolist(), 'span': span.round(4).tolist(), 'shapes': head, 'looks': lookhead,
           'gz': base64.b64encode(gz).decode()}
    (OUT / f'{key}.json').write_text(json.dumps(rec, separators=(',', ':')))
    return (OUT / f'{key}.json').stat().st_size


def shared():
    T = OUT / 'tex'; T.mkdir(exist_ok=True)
    import weaponlooks as WL
    names = {l['tex'] for opts in WL.LOOKS3.values() for o in opts for l in o['edges'] + o['faces']}
    for n in sorted(names):
        im = Image.open(W.WORK / f'{n}.png')
        im.thumbnail((1024, 1024), Image.LANCZOS)   # (the page's copy: half the game's 2K, to keep it light)
        im.save(T / f'{n}.png', optimize=True)
    TR = ROOT / 'build' / 'trialfx'
    for n in ['band', 'curtain', 'glow', 'runes', 'smoke', 'spiritflame', 'take', 'trailfire']:
        Image.open(TR / f'{n}.png').save(T / f't_{n}.png', optimize=True)


def export_model(key, rel):
    """a model alone, no art of ours on it (the Wyrmspine Javelin in flight, for its bounce's preview)"""
    path = ROOT / 'assets' / 'fx' / 'Meshes' / rel.replace('\\', '/')
    model = read_model(path)
    allp = np.concatenate([np.array(s['v']) for s in model])
    lo, hi = allp.min(0) - 1, allp.max(0) + 1
    span = np.maximum(hi - lo, 1e-3)
    buf, head, cache = io.BytesIO(), [], {}
    for s in model:
        V = np.array(s['v']); U = np.array(s['uv']); T = np.array(s['t'])
        m = {'k': 'm', 'n': len(V), 'nt': len(T), 'i32': len(V) >= 65536}
        if s['kind'] == 'effect': m['glow'] = 1
        tp = find_tex(s['tex']) if s['tex'] else None
        if tp and tp not in cache:
            im = Image.open(tp).convert('RGB'); im.thumbnail((384, 384)); b = io.BytesIO(); im.save(b, 'JPEG', quality=78)
            cache[tp] = 'data:image/jpeg;base64,' + base64.b64encode(b.getvalue()).decode()
        if tp: m['map'] = cache[tp]
        else: m['color'] = guess(s['tex'] + ' ' + s['name'])
        head.append(m)
        buf.write(np.round((V - lo) / span * 65535 - 32768).clip(-32768, 32767).astype('<i2').tobytes())
        buf.write(np.round(U * 2048).clip(-32768, 32767).astype('<i2').tobytes())
        buf.write(np.asarray(T, dtype='<u2' if len(V) < 65536 else '<u4').tobytes())
    rec = {'key': key, 'scale': root_scale(path), 'lo': lo.round(4).tolist(), 'span': span.round(4).tolist(), 'shapes': head, 'looks': {},
           'gz': base64.b64encode(gzip.compress(buf.getvalue(), 9)).decode()}
    (OUT / f'{key}.json').write_text(json.dumps(rec, separators=(',', ':')))
    return (OUT / f'{key}.json').stat().st_size


if __name__ == '__main__':
    args = sys.argv[1:]
    if args == ['shared']: shared(); sys.exit()
    if args[:1] == ['model']: print(args[1], export_model(args[1], args[2]) // 1024, 'KB'); sys.exit()
    for k in args:
        n = export(k)
        print(k, n // 1024, 'KB')
