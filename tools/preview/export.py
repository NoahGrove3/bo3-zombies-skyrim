"""Export each weapon (its model, lit shapes only) and its effect art (the fxwrite job) to JSON for the preview renderer."""
import sys, json, base64, io, pathlib, re
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import weaponfx as W

OUT = pathlib.Path(__file__).resolve().parent.parent.parent / 'build' / 'preview' / 'data'; OUT.mkdir(parents=True, exist_ok=True)
REPO = pathlib.Path(__file__).resolve().parent.parent.parent
TEXROOTS = [REPO / 'assets' / 'fx' / 'Textures', REPO / 'assets' / 'Textures']


def uri(im, fmt='PNG', q=None):
    b = io.BytesIO(); im.save(b, fmt, **({'quality': q} if q else {})); return f'data:image/{fmt.lower()};base64,' + base64.b64encode(b.getvalue()).decode()


def find_tex(rel):
    rel = rel.replace('\\', '/')
    if rel.lower().startswith('textures/'): rel = rel[9:]
    for root in TEXROOTS:
        p = root / rel
        if p.exists(): return p
        par = p.parent
        if par.exists():
            for c in par.iterdir():
                if c.name.lower() == p.name.lower(): return c
    return None


# (a steel colour by the texture's name, for the models whose textures are only on Noah's PC)
GUESS = [('glass', '#5f8f62'), ('ebony', '#2a2630'), ('daedric', '#2b1a1c'), ('dragonbone', '#d8ccb0'), ('dbone', '#d8ccb0'), ('bone', '#cfc2a6'),
         ('stalhrim', '#8fb8d0'), ('stahlrim', '#8fb8d0'), ('silver', '#c8ccd4'), ('dwarven', '#a8804a'), ('falmer', '#7a6a4a'),
         ('forsworn', '#6a5a48'), ('nordic', '#9a9aa2'), ('vampire', '#5a3238'), ('gold', '#c9a04a'), ('leather', '#4a3426'),
         ('wood', '#5a4028'), ('steel', '#8e9298'), ('iron', '#6e6e72'), ('orcish', '#4e5a48'), ('elven', '#c4a868')]


def guess(tex, name):
    s = (tex + ' ' + name).lower()
    for k, c in GUESS:
        if k in s: return c
    return '#7a7c82'


def model(rel):
    shapes = W.read_model(W.model_path(rel))
    out = []
    raw = __import__('subprocess').run([str(W.TOOLS / 'nifworld'), str(W.model_path(rel))], capture_output=True, text=True).stdout
    # (re-read for the uvs: read_model keeps only xyz)
    cur = None; uvs = {}
    for l in raw.splitlines():
        if l.startswith('shape '): cur = l[6:].split('\t')[0]; uvs[cur] = []
        elif l.startswith('v '):
            p = l.split(); uvs[cur].append([float(p[4]), float(p[5])] if len(p) >= 6 else [0, 0])
    texcache = {}
    for s in shapes:
        if s['kind'] != 'lit' or not len(s['t']): continue
        tp = find_tex(s['tex']) if s['tex'] else None
        mat = {'color': guess(s['tex'], s['name'])}
        if tp:
            if tp not in texcache:
                try:
                    im = Image.open(tp).convert('RGB'); im.thumbnail((512, 512)); texcache[tp] = uri(im, 'JPEG', 82)
                except Exception as e:
                    texcache[tp] = None
            if texcache[tp]: mat = {'map': texcache[tp]}
        out.append({'name': s['name'], 'v': np.round(s['v'], 3).ravel().tolist(), 'uv': np.round(np.array(uvs[s['name']]), 4).ravel().tolist(),
                    't': s['t'].ravel().tolist(), **mat})
    return out


def job(key):
    shapes, cur = [], None
    for l in (W.WORK / f'{key}.job').read_text().splitlines():
        if l.startswith('shape '):
            parts = l.split()
            kv = dict(p.split('=', 1) for p in parts[2:] if '=' in p)
            cur = {'name': parts[1], 'tex': kv['tex'].split('\\')[-1][:-4], 'pal': kv.get('palette', '').split('\\')[-1][4:-4],
                   'scale': float(kv.get('scale', 1)), 'v': [], 't': []}
            if 'flip' in kv: cur['flip'] = [int(x) for x in kv['flip'].split('x')]; cur['fps'] = float(kv['fps']); cur['phase'] = float(kv.get('phase', 0))
            if 'vscroll' in kv: cur['vscroll'] = float(kv['vscroll'])
            if 'pulse' in kv: cur['pulse'] = [float(x) for x in kv['pulse'].split(',')]
            if 'billboard' in kv: cur['bb'] = 1; cur['at'] = [float(x) for x in kv['at'].split(',')]
            shapes.append(cur)
        elif l.startswith('v ') and cur is not None:
            p = [float(x) for x in l.split()[1:]]
            cur['v'] += [round(p[0], 3), round(p[1], 3), round(p[2], 3), round(p[3], 4), round(p[4], 4), round(p[8], 3)]
        elif l.startswith('t ') and cur is not None:
            cur['t'] += [int(x) for x in l.split()[1:4]]
    return shapes


def palettes():
    x = (np.arange(256) + 0.5) / 256; v = (np.arange(64) + 0.5) / 64
    out = {}
    for name, h in W.COL.items():
        pos = x[None, :] * (0.4 + 0.6 * v[:, None])
        rgb = W.ramp(W.hexrgb(h), pos)
        a = np.clip(x[None, :] * 1.3, 0, 1) * (0.35 + 0.65 * v[:, None])
        out[name] = uri(Image.fromarray((np.dstack([np.clip(rgb, 0, 1), a]) * 255).astype(np.uint8), 'RGBA'))
    return out


def textures():
    out = {}
    for n in ['cracks', 'flame_strip0', 'flame_strip1', 'haze', 'mist_strip0', 'mist_strip1', 'molten_strip', 'mote', 'runes', 'storm_edge', 'storm_shell', 'stream_strip']:
        out[n] = uri(Image.open(W.WORK / f'{n}.png'))
    return out


if __name__ == '__main__':
    keys = sys.argv[1:]
    if not keys or keys == ['shared']:
        (OUT / 'shared.json').write_text(json.dumps({'tex': textures(), 'pal': palettes()}))
        print('shared'); sys.exit()
    for key in keys:
        rel = W.ALL[key][0] if key in W.ALL else W.CLASS[key][0]
        d = {'key': key, 'model': model(rel), 'fx': job(key)}
        (OUT / f'{key}.json').write_text(json.dumps(d))
        print(key, len(d['model']), 'lit shapes', len(d['fx']), 'fx shapes')
