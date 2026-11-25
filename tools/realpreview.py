"""The workshop's close pictures of the real parts (Noah, 2026-11-25: "Build from real parts" - each ability's preview rebuilt
from its real parts' own textures and particle settings, read from his game).

Reads each real part's NIF (staged from Noah's game: Skyrim's, Dawnguard's, Dragonborn's, Apocalypse's and Mysticism's own
effects, never committed) with tools/modelconv/nifpsys.cpp - its node tree, particle systems (emitters, modifiers), effect
meshes, shaders and the controllers' keys (the sequence the game plays on an explosion, SpecialIdle_AreaEffect) - and writes
the page's data: build/preview/real/parts.json, its textures small (real/tex/) and our ramps (real/pal/), for the page's
player (the workshop's real.js), which plays them as the game does: each particle born, moved, grown, coloured and faded by
its own system's settings.

    python3 tools/realpreview.py
"""
import json, pathlib, shutil, subprocess, sys
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
RP = ROOT / 'build' / 'realparts'
OUT = ROOT / 'build' / 'preview' / 'real'
TOOLS = pathlib.Path('/home/claude/tools')


def tool():
    exe = RP / 'tool' / 'nifpsys'
    if not exe.exists():
        exe.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['g++', '-std=c++20', '-O1', f'-I{TOOLS}/nifly/include', f'-I{TOOLS}/nifly/external', str(ROOT / 'tools' / 'modelconv' / 'nifpsys.cpp'),
                        str(TOOLS / 'nifly' / 'build' / 'src' / 'libnifly.a'), '-o', str(exe)], check=True)
    return exe


def texkey(p):
    p = (p or '').lower().replace('/', '\\').strip('\\')
    if p.startswith('textures\\'): p = p[9:]
    return p


TEXUSED = {}


def tex(p):
    k = texkey(p)
    if not k: return ''
    src = RP / 'png' / (k.replace('\\', '/')[:-4] + '.png')
    if not src.exists(): return ''
    name = k.replace('\\', '_').replace(' ', '_')[:-4] + '.png'
    TEXUSED[name] = src
    return name


def shader(sh):
    if not sh or 'tex' not in sh: return None
    a = sh.get('alpha', 4333)
    f1 = sh.get('f1', 0)
    return dict(tex=tex(sh['tex']), grey=tex(sh.get('grey')), color=sh['color'], cs=sh['cscale'],
                palC=bool(f1 & 0x10), palA=bool(f1 & 0x20), valpha=bool(f1 & 0x8), vcol=bool(sh.get('f2', 0) & 0x20), fall=bool(f1 & 0x40),
                falloff=sh.get('falloff'), uvoff=sh.get('uvoff'), uvscale=sh.get('uvscale'),
                add=((a >> 5) & 15) == 0, blend=bool(a & 1), test=bool(a & 0x200), clamp=sh.get('clamp', 0))


def tracks_of(ctl, target, seqlinks=None):
    """a controller chain's animation: [{k: kind, target, mod, keys, v, start, stop, cycle, freq}]"""
    out = []
    for c in ctl:
        t = c['type']; it = c.get('interp') or {}
        if it.get('k') not in (None, 'float', 'bool', 'xf', 'p3'): continue   # (a blend: the sequence drives it)
        base = dict(target=target, start=c['start'], stop=c['stop'], freq=c['freq'], cycle=(c['flags'] >> 1) & 3)
        if t in ('NiPSysEmitterCtlr', 'BSPSysMultiTargetEmitterCtlr'):
            if it.get('k') == 'float' or it.get('keys'): out.append(dict(base, k='rate', mod=c.get('mod'), v=it.get('v', 0), keys=it.get('keys')))
            vis = c.get('vis') or {}
            if vis.get('k') in (None, 'bool'): out.append(dict(base, k='emit', mod=c.get('mod'), v=vis.get('v', 1), keys=vis.get('keys')))
        elif t == 'NiPSysGravityStrengthCtlr': out.append(dict(base, k='grav', mod=c.get('mod'), v=it.get('v'), keys=it.get('keys')))
        elif t == 'NiPSysEmitterSpeedCtlr': out.append(dict(base, k='speed', mod=c.get('mod'), v=it.get('v'), keys=it.get('keys')))
        elif t == 'NiPSysEmitterLifeSpanCtlr': out.append(dict(base, k='life', mod=c.get('mod'), v=it.get('v'), keys=it.get('keys')))
        elif t == 'NiPSysModifierActiveCtlr': out.append(dict(base, k='active', mod=c.get('mod'), v=it.get('v'), keys=it.get('keys')))
        elif t == 'BSEffectShaderPropertyFloatController': out.append(dict(base, k='sf', var=c.get('var'), v=it.get('v'), keys=it.get('keys')))
        elif t == 'BSEffectShaderPropertyColorController': out.append(dict(base, k='sc', v=it.get('v'), keys=it.get('keys')))
        elif t in ('NiTransformController', 'NiKeyframeController') and it.get('k') == 'xf': out.append(dict(base, k='xf', x=it))
        elif t == 'NiVisController': out.append(dict(base, k='vis', v=it.get('v', 1), keys=it.get('keys')))
    return out


def seq_tracks(seq):
    """the sequence the game plays: SpecialIdle_AreaEffect when there is one"""
    if not seq: return [], None
    # (a projectile's own loop while it flies - mLoop - not its mBegin: Noah, 2026-11-25, the bolts in flight looked pale)
    s = next((q for q in seq if q['name'] == 'SpecialIdle_AreaEffect'), None) or next((q for q in seq if q['name'] == 'mLoop'), None) \
        or next((q for q in seq if 'idle' in q['name'].lower()), seq[0])
    out = []
    base = dict(start=s['start'], stop=s['stop'], freq=1, cycle=s['cycle'])
    for l in s['links']:
        it = l.get('interp') or {}
        c, tgt = l['ctrl'], l['node']
        if c == 'NiPSysEmitterCtlr':
            out.append(dict(base, target=tgt, k='emit' if it.get('k') == 'bool' else 'rate', mod=l['id'], v=it.get('v', 0), keys=it.get('keys')))
        elif c == 'NiPSysGravityStrengthCtlr': out.append(dict(base, target=tgt, k='grav', mod=l['id'], v=it.get('v'), keys=it.get('keys')))
        elif c == 'NiPSysEmitterSpeedCtlr': out.append(dict(base, target=tgt, k='speed', mod=l['id'], v=it.get('v'), keys=it.get('keys')))
        elif c == 'NiPSysEmitterLifeSpanCtlr': out.append(dict(base, target=tgt, k='life', mod=l['id'], v=it.get('v'), keys=it.get('keys')))
        elif c == 'NiPSysModifierActiveCtlr': out.append(dict(base, target=tgt, k='active', mod=l['id'], v=it.get('v'), keys=it.get('keys')))
        elif c == 'BSEffectShaderPropertyFloatController': out.append(dict(base, target=tgt, k='sf', var=int(l['id'] or 0), v=it.get('v'), keys=it.get('keys')))
        elif c == 'BSEffectShaderPropertyColorController': out.append(dict(base, target=tgt, k='sc', v=it.get('v'), keys=it.get('keys')))
        elif c in ('NiTransformController', 'NiKeyframeController') and it.get('k') == 'xf': out.append(dict(base, target=tgt, k='xf', x=it))
        elif c == 'NiVisController': out.append(dict(base, target=tgt, k='vis', v=it.get('v', 1), keys=it.get('keys')))
    return out, s


def model(path):
    d = json.loads(subprocess.run([str(tool()), str(path)], capture_output=True, text=True, check=True).stdout)
    tracks, s = seq_tracks(d['seq'])
    for p in d['psys']: tracks += tracks_of(p['ctl'], p['name'])
    for sh in d['shapes']:
        tracks += tracks_of(sh['ctl'], sh['name'])
        tracks += tracks_of((sh['shader'] or {}).get('ctl', []), sh['name'])
    for n in d['nodes']: tracks += tracks_of(n['ctl'], n['name'])
    psys = []
    for p in d['psys']:
        sh = shader(p['shader'])
        if not sh: continue
        q = dict(name=p['name'], max=p.get('max', 100), ws=p['ws'], sub=p.get('sub') or [], aspect=p.get('aspect', 0), sh=sh, mods=[])
        for m in p['mods']:
            t = m['type']
            if t in ('NiPSysAgeDeathModifier', 'BSPSysLODModifier', 'NiPSysPositionModifier', 'NiPSysBoundUpdateModifier', 'BSPSysStripUpdateModifier'): continue
            q['mods'].append(m)
        psys.append(q)
    emitmeshes = {n for p in d['psys'] for m in p['mods'] for n in (m.get('meshes') or [])}
    shapes = []
    for sh in d['shapes']:
        s2 = shader(sh['shader'])
        geo = dict(v=[round(x, 2) for x in sh['v']], uv=[round(x, 4) for x in sh['uv']], t=sh['t'], col=[round(x, 3) for x in sh['col']])
        if sh['name'] in emitmeshes: shapes.append(dict(name=sh['name'], emit=True, v=geo['v'], t=geo['t']))
        if s2 and not sh['hidden']: shapes.append(dict(name=sh['name'], sh=s2, **geo))
    tree = [dict(name=o['name'], type=o['type'], parent=o['parent'], t=o['local']['t'], x=o['local']['x'], y=o['local']['y'], z=o['local']['z'], s=o['local']['s'], hidden=o['hidden'])
            for o in d['tree']]
    dur = max([t.get('stop', 0) for t in tracks if t.get('stop', 0) < 1e6] + [s['stop'] if s else 0, 0.5])
    return dict(tree=tree, psys=psys, shapes=shapes, tracks=tracks, dur=min(dur, 6.0))


def palettes():
    import realfire
    from PIL import Image
    alpha = np.load(ROOT / 'tools' / 'data' / 'pal_alpha.npy').astype(float) / 255
    x = (np.arange(256) + 0.5) / 256
    v = (np.arange(64) + 0.5) / 64
    (OUT / 'pal').mkdir(parents=True, exist_ok=True)
    for name, stops in realfire.RAMPS.items():
        pos = x[None, :] * (0.4 + 0.6 * v[:, None])
        img = np.zeros((64, 256, 4), np.uint8)
        for c in range(3): img[..., c] = np.interp(pos, [s_[0] for s_ in stops], [s_[1][c] for s_ in stops]).astype(np.uint8)
        img[..., 3] = (np.clip(alpha, 0, 1) * 255).astype(np.uint8)
        Image.fromarray(img, 'RGBA').resize((128, 32)).save(OUT / 'pal' / f'{name}.png')
    return sorted(realfire.RAMPS)


def build():
    import realfx
    OUT.mkdir(parents=True, exist_ok=True)
    models = {p['model'] for t in realfx.PARTS.values() for r, p in t.items() if r != 'native'} | {realfx.RING['model'], 'magic\\summontargetfx.nif'}
    import docshow
    models |= set(docshow.models())   # (the shows': bolts in flight, the sunbeam, the javelin's bounce - tools/docshow.py)
    import abilityopts
    models |= set(abilityopts.models())   # (each ability's five options - tools/abilityopts.py)
    data = {}
    for m in sorted(models):
        src = RP / 'meshes' / ('meshes/' + m.replace('\\', '/')) if (RP / 'meshes' / 'meshes').exists() else RP / 'meshes' / m.replace('\\', '/')
        if not src.exists(): src = RP / 'meshes' / m.replace('\\', '/')
        if not src.exists(): print('missing', m); continue
        data[m] = model(src)
    # (the addon nodes: Skyrim's own shared particle systems, ADDN records - read from the game's plugins - that its impacts and
    # explosions carry at their AddOnNode points)
    addn = json.loads((RP / 'addn' / 'addn.json').read_text())
    for i, m in addn.items():
        src = RP / 'addn' / 'meshes' / m.lower().replace('\\', '/')
        if src.exists(): data['addn:' + i] = model(src)
        else: print('missing addon', i, m)
    (OUT / 'tex').mkdir(parents=True, exist_ok=True)
    for n, s in TEXUSED.items(): shutil.copy(s, OUT / 'tex' / n)
    pals = palettes()
    (OUT / 'parts.json').write_text(json.dumps(data, separators=(',', ':')))
    print(len(data), 'parts,', len(TEXUSED), 'textures,', len(pals), 'ramps;', (OUT / 'parts.json').stat().st_size // 1024, 'KB')


if __name__ == '__main__':
    build()
