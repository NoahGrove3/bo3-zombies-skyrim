"""Puts the permanent workshop together: python3 docs/workshop/v2/build.py <site dir>
index.html + its CSS and scripts in one page, data.json inlined; the 3D models it shows copied from the last workshop's site
(live/ - built from Noah's own install by tools/preview/export_live.py, never committed)."""
import json, pathlib, shutil, sys
HERE = pathlib.Path(__file__).resolve().parent
LIVE = pathlib.Path('/tmp/claude-0/-home-claude/0ec68033-d637-57d9-8f95-48d7c09e98c8/scratchpad/build12/site/live')
out = pathlib.Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
data = json.loads((HERE / 'data.json').read_text())
page = (HERE / 'index.html').read_text()
page = page.replace('__UMOCK__', (HERE / 'umock.css').read_text())
page = page.replace('__DATA__', json.dumps(data, ensure_ascii=False).replace('<', '\\u003c'))
page = page.replace('__VIEWER__', (HERE / 'viewer.js').read_text()).replace('__UISCREENS__', (HERE / 'uiscreens.js').read_text()).replace('__APP__', (HERE / 'app.js').read_text())
(out / 'index.html').write_text(page)
keys = [m['key'] for c in data['classes'] for m in c['cands']] + [w['key'] for w in data['tablet'] if w['key']]
(out / 'live').mkdir(exist_ok=True)
texs = set()
for k in keys:
    shutil.copy(LIVE / f'{k}.json', out / 'live' / f'{k}.json')
    d = json.loads((LIVE / f'{k}.json').read_text())
    for look in d['looks'].values():
        for s in look: texs.add(s['tex'])
    for s in d['shapes']:
        if s.get('map') and not s['map'].startswith('data:'): texs.add('MAP:' + s['map'])
(out / 'live' / 'tex').mkdir(exist_ok=True)
maps = []
for t in texs:
    if t.startswith('MAP:'): maps.append(t[4:]); continue
    shutil.copy(LIVE / 'tex' / f'{t}.png', out / 'live' / 'tex' / f'{t}.png')
for m in maps:   # (a model's own texture, at the path its json names, relative to the page)
    src = LIVE.parent / m; dst = out / m; dst.parent.mkdir(parents=True, exist_ok=True)
    if src.exists(): shutil.copy(src, dst)
    else: print('missing map', m)
for c in data.get('clips', []):
    src = HERE / 'clips' / pathlib.Path(c['file']).name
    (out / 'clips').mkdir(exist_ok=True); shutil.copy(src, out / 'clips' / src.name)
n = sum(1 for _ in out.rglob('*') if _.is_file())
print('site:', out, n, 'files', round(sum(f.stat().st_size for f in out.rglob('*') if f.is_file()) / 1e6, 1), 'MB; page', round((out / 'index.html').stat().st_size / 1e3), 'KB')
