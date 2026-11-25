import json, pathlib, base64, io
from PIL import Image
REPO = pathlib.Path(__file__).resolve().parent.parent.parent
WORK = REPO / 'build' / 'trialfx'; PALD = REPO / 'assets/fx/Textures/CurseOfAlduin/trial'
def uri(im): b = io.BytesIO(); im.save(b, 'PNG'); return 'data:image/png;base64,' + base64.b64encode(b.getvalue()).decode()
def job(name):
    nodes, shapes, cur = {}, [], None
    for l in (WORK / f'{name}.job').read_text().splitlines():
        if l.startswith('node '):
            p = l.split(); nodes[p[1]] = {'parent': p[2], 'at': [float(x) for x in p[3:6]]}
        elif l.startswith('shape '):
            parts = l.split(); kv = dict(p.split('=', 1) for p in parts[2:] if '=' in p)
            cur = {'name': parts[1], 'tex': kv['tex'].split('\\')[-1][:-4], 'pal': kv.get('palette', '').split('\\')[-1][4:-4], 'scale': float(kv.get('scale', 1)), 'v': [], 't': [], 'parent': kv.get('parent', '')}
            if 'flip' in kv: cur['flip'] = [int(x) for x in kv['flip'].split('x')]; cur['fps'] = float(kv['fps']); cur['phase'] = float(kv.get('phase', 0))
            if 'vscroll' in kv: cur['vscroll'] = float(kv['vscroll'])
            if 'pulse' in kv: cur['pulse'] = [float(x) for x in kv['pulse'].split(',')]
            if 'clamp' in kv: cur['clamp'] = int(kv['clamp'])
            if 'billboard' in kv: cur['bb'] = 1; cur['at'] = [float(x) for x in kv['at'].split(',')]
            shapes.append(cur)
        elif l.startswith('v ') and cur is not None:
            p = [float(x) for x in l.split()[1:]]; cur['v'] += [round(p[0], 2), round(p[1], 2), round(p[2], 2), round(p[3], 4), round(p[4], 4), round(p[8], 3)]
        elif l.startswith('t ') and cur is not None: cur['t'] += [int(x) for x in l.split()[1:4]]
    return {'nodes': nodes, 'fx': shapes}
(REPO / 'build' / 'preview' / 'data').mkdir(parents=True, exist_ok=True)
out = {'tex': {n: uri(Image.open(WORK / f'{n}.png')) for n in ['band', 'curtain', 'glow', 'runes', 'smoke', 'spiritflame', 'take', 'trailfire']},
       'pal': {n: uri(Image.open(PALD / f'pal_{n}.dds')) for n in ['altar', 'spirit', 'whitefire']},
       'ring': job('ring_1400'), 'soul': job('soul'), 'take': job('soul_take')}
(pathlib.Path(__file__).resolve().parent.parent.parent / 'build' / 'preview' / 'data' / 'trial.json').write_text(json.dumps(out)); print('ok')
