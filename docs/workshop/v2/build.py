"""Puts the permanent workshop together: python3 docs/workshop/v2/build.py <site dir>
index.html + its CSS and scripts in one page, data.json inlined; the 3D models it shows copied from the last workshop's site
(live/ - built from Noah's own install by tools/preview/export_live.py, never committed)."""
import json, pathlib, shutil, sys
HERE = pathlib.Path(__file__).resolve().parent
LIVE = pathlib.Path('/tmp/claude-0/-home-claude/0ec68033-d637-57d9-8f95-48d7c09e98c8/scratchpad/build12/site/live')
out = pathlib.Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
data = json.loads((HERE / 'data.json').read_text())
page = (HERE / 'index.html').read_text()
page = page.replace('__UICSS__', (HERE / 'ui.css').read_text()).replace('__REAL__', (HERE / 'real.js').read_text())
page = page.replace('__DATA__', json.dumps(data, ensure_ascii=False).replace('<', '\\u003c'))
page = page.replace('__VIEWER__', (HERE / 'viewer.js').read_text()).replace('__VFX__', (HERE / 'vfx.js').read_text()).replace('__ZOMBIES__', (HERE / 'zombies.js').read_text()).replace('__ACTION__', (HERE / 'action.js').read_text() + '\n' + (HERE / 'tablet.js').read_text() + '\n' + (HERE / 'armour.js').read_text() + '\n' + (HERE / 'flasks.js').read_text()).replace('__UISCREENS__', (HERE / 'uiscreens.js').read_text()).replace('__APP__', (HERE / 'app.js').read_text())
(out / 'index.html').write_text(page)
keys = [m['key'] for c in data['classes'] for m in c['cands']] + [w['key'] for w in data['tablet'] if w['key']] + [w['key'] for w in data['wonders']] + ['flask_' + e for e in ('fire', 'frost', 'shock', 'poison', 'sun', 'blood')] + ['tablet']
FRESH = HERE.parent.parent.parent / 'build' / 'preview' / 'live'   # (re-exported from the game's models now: tools/preview/export_live.py)
(out / 'live').mkdir(exist_ok=True)
texs = set()
for k in keys:
    srcj = FRESH / f'{k}.json' if (FRESH / f'{k}.json').exists() else LIVE / f'{k}.json'
    shutil.copy(srcj, out / 'live' / f'{k}.json')
    d = json.loads(srcj.read_text())
    for look in d['looks'].values():
        for s in look: texs.add(s['tex'])
    for s in d['shapes']:
        if s.get('map') and not s['map'].startswith('data:'): texs.add('MAP:' + s['map'])
(out / 'live' / 'tex').mkdir(exist_ok=True)
# the night's dead and you (Mihail's zombies, the Nord in each class set - tools/zombiepreview.py, playerpreview.py): bodies, skeletons, their textures
for k in ['zom_skel', 'plr_skel'] + ['zom_' + z for z in ('mangled', 'foul', 'putrid', 'malignant', 'relentless', 'dread_putrid')] + ['plr_' + c for c in ('alchemist', 'summoner', 'destruction', 'spellsword')]:
    shutil.copy(LIVE / f'{k}.json', out / 'live' / f'{k}.json')
    d = json.loads((LIVE / f'{k}.json').read_text())
    for sh in d.get('shapes', []):
        for f in ('map', 'nrm'):
            if sh.get(f):
                ext = sh.get('ext', 'jpg') if f == 'map' else 'jpg'
                src = LIVE / 'tex' / f"{sh[f]}.{ext}"
                if src.exists(): shutil.copy(src, out / 'live' / 'tex' / src.name)
                else: print('missing body tex', src.name)
maps = []
for t in texs:
    if t.startswith('MAP:'): maps.append(t[4:]); continue
    srct = FRESH / 'tex' / f'{t}.png' if (FRESH / 'tex' / f'{t}.png').exists() else LIVE / 'tex' / f'{t}.png'
    shutil.copy(srct, out / 'live' / 'tex' / f'{t}.png')
for m in maps:   # (a model's own texture, at the path its json names, relative to the page)
    src = LIVE.parent / m; dst = out / m; dst.parent.mkdir(parents=True, exist_ok=True)
    if src.exists(): shutil.copy(src, dst)
    else: print('missing map', m)
# (the element kits are the Curse's own 3D effects now - vfx.js - drawn live; no effect files to copy)
# the sounds the kits offer
(out / 'snd').mkdir(exist_ok=True)
for k in data['kits']:
    for s in k['sounds']:
        for o in s['opts']: shutil.copy(LIVE.parent / 'snd' / o['mp3'], out / 'snd' / o['mp3'])
(out / 'art').mkdir(exist_ok=True)
for f in (pathlib.Path(LIVE.parent.parent.parent) / 'uiart').glob('*'): shutil.copy(f, out / 'art' / f.name)
# the real stills behind the in-game UI previews (Noah's own clips, our old HUD painted out - never committed)
(out / 'frames').mkdir(exist_ok=True)
for f in (pathlib.Path(LIVE.parent.parent.parent) / 'uiframes').glob('*.jpg'): shutil.copy(f, out / 'frames' / f.name)
for c in data.get('clips', []):
    src = HERE / 'clips' / pathlib.Path(c['file']).name
    (out / 'clips').mkdir(exist_ok=True); shutil.copy(src, out / 'clips' / src.name)
n = sum(1 for _ in out.rglob('*') if _.is_file())
print('site:', out, n, 'files', round(sum(f.stat().st_size for f in out.rglob('*') if f.is_file()) / 1e6, 1), 'MB; page', round((out / 'index.html').stat().st_size / 1e3), 'KB')
