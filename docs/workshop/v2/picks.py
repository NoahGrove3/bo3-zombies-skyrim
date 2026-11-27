# Noah's picks and decisions, round by round (Nov 27: "include my changes in the history tab along with my notes ... so you dont assume
# any old information"): reads snapshots of the workshop's database (one folder per round: <collection>/<id>.json) and writes
# picks_log.json, in words, for the History tab. Each round lists what changed since the round before; the last also lists everything
# as it stands.
#   python3 picks.py "label|YYYY-MM-DD|dir" ...      (oldest first)
import json, re, sys
from pathlib import Path

HERE = Path(__file__).parent
D = json.loads((HERE / 'data.json').read_text())
UIS = {m[0]: m[1] for m in re.findall(r"\['([a-z_0-9]+)', '((?:[^'\\]|\\.)*)'", (HERE / 'uiscreens.js').read_text())}
DIRS = {'tally': 'Ember Tally', 'bone': 'Dragonbone', 'glass': 'Echo Glass'}
CLS = {c['id']: c for c in D['classes']}
KIT = {k['id']: k for k in D['kits']}
NEW = {w['id']: w for w in D['tabnew'] + D['tabnew2']}
TAB = {w['id']: w for w in D['tablet']}
DEC = {x['id']: x for x in D['decisions']}
ROLL = {r['id']: r['name'] for r in D['rolls']}
LORE = {'cities': 'the cities', 'classes': 'the classes', 'curse': 'the Curse', 'eggs': 'the Easter eggs', 'end': 'the ending', 'felldir': 'Felldir', 'trials': 'the trials'}
WORD = {'keep': 'keep', 'cut': 'cut', 'rework': 'rework', 'add': 'add', 'no': 'no', 'maybe': 'maybe', 'ok': 'approved', 'change': 'change', 'pick': 'picked'}


def load(d):
    out = {}
    for f in Path(d).glob('*/*.json'):
        out[(f.parent.name, f.stem)] = {k: v for k, v in json.loads(f.read_text()).items() if k not in ('savedAt', 'note')}
    return out


def look(c, o):
    if not o: return ''
    n = next((x['name'] for x in c['looks'] if x['id'] == o.get('look')), o.get('look', ''))
    if o.get('blaze'): n += ' at its brightest'
    if o.get('layer'): n += ' + ' + next((x['name'] for x in c['looks'] if x['id'] == o['layer']), o['layer'])
    return n


def say(col, id, v):   # one row in words: (where, what)
    if col == 'cwpick' and id in CLS:
        c = CLS[id]; return f'{c["name"]}: model', next((m['name'] for m in c['cands'] if m['key'] == v.get('model')), v.get('model') or 'none')
    if col == 'tierlook' and id in CLS:
        c = CLS[id]; return f'{c["name"]}: tier looks', '; '.join(f'{t}: {look(c, v.get(k))}' for k, t in [('t1', 'I'), ('t2', 'II'), ('t3', 'III'), ('t4', 'Legendary')] if v.get(k))
    if col == 'abil' and id in D['moves']:
        M = D['moves'][id]; parts = []
        if v.get('el'): parts.append('element ' + KIT.get(v['el'], {}).get('name', v['el']))
        for w, wn, opts in M['moves']:
            p = v.get('mv_' + w)
            parts.append(f'{w}: ' + (next((o[1] for o in opts if o[0] == p), p) if p else 'not picked'))
        return f'{CLS[id]["name"] if id in CLS else id}: moves', '; '.join(parts)
    if col == 'kits' and id in KIT:
        k = KIT[id]; parts = []
        for p in k['parts']:
            if v.get('p_' + p['id']): parts.append(p['name'] + ' ' + next((o['label'] for o in p['opts'] if o['id'] == v['p_' + p['id']]), v['p_' + p['id']]))
        for s in k['sounds']:
            if v.get('s_' + s['id']): parts.append(s['name'] + ' sound ' + next((o['name'] for o in s['opts'] if o['id'] == v['s_' + s['id']]), v['s_' + s['id']]))
        return f'{k["name"]} kit', '; '.join(parts) or 'nothing picked'
    if col == 'decisions' and id in DEC:
        x = DEC[id]; return 'Decision: ' + x['q'], next((o['name'] for o in x['opts'] if o['id'] == v.get('pick')), v.get('pick') or 'none')
    if col == 'tablet':
        if id == '_ideas': return None
        return f'Tablet: {TAB.get(id, {}).get("name", id)}', WORD.get(v.get('choice'), v.get('choice') or 'mark cleared')
    if col == 'tabnew': return f'New Tablet weapon: {NEW.get(id, {}).get("name", id)}', WORD.get(v.get('choice'), v.get('choice') or 'mark cleared')
    if col == 'tabroll': return 'Tablet roll', ROLL.get(v.get('pick'), v.get('pick') or 'none')
    if col == 'tabrar':
        if id == 'odds': return 'Tablet odds', ', '.join(f'{k} {v[k]}%' for k in ('rare', 'epic', 'legendary') if k in v)
        return 'Tablet rarity changes', ', '.join(f'{NEW.get(a, TAB.get(a, {})).get("name", a)} {b}' for a, b in v.items()) or 'my defaults'
    if col == 'uicursor': return 'UI cursor', {'skyrim': 'Skyrim\'s own', 'talon': 'Ember Talon', 'rune': 'Rune Arrow', 'spark': 'Soul Spark', 'bone': 'Dragonbone Point', 'crystal': 'Echo Shard'}.get(v.get('pick'), v.get('pick') or 'none')
    if col == 'uidir': return 'UI direction', DIRS.get(v.get('pick'), v.get('pick'))
    if col == 'uiscreens': return f'UI: {UIS.get(id, id)}', WORD.get(v.get('choice'), v.get('choice') or 'mark cleared')
    if col == 'lore': return f'Lore: {LORE.get(id, id)}', WORD.get(v.get('choice'), v.get('choice') or 'mark cleared')
    if col == 'armourlook' and id in CLS:
        al = {'sheen': 'Sheen', 'wisps': 'Rising wisps', 'embers': 'Embers', 'runes': 'Rune circle', 'mantle': 'Mantle'}
        t4 = v.get('t4') or ''; t4 = 'at its brightest' if t4 == 'blaze' else ('+ ' + al.get(t4[6:], t4[6:])) if t4 else 'not picked'
        return f'{CLS[id]["name"]}: armour look', f'{al.get(v.get("look"), v.get("look") or "not picked")}; Legendary {t4}'
    if col == 'flasks':
        nm = {'fire': 'Fire Flask', 'frost': 'Frost Flask', 'shock': 'Shock Flask', 'poison': 'Acid Flask', 'sun': 'Holy Water', 'blood': 'Voidglass Flask', 'philosopher': 'Philosopher\'s Flask'}
        return nm.get(id, id), f"liquid {v.get('liquid', '-')}; cork {v.get('cork', '-')}; impact {v.get('impact', '-')} at {round(100 * v['size']) if 'size' in v else '-'}%"
    if col == 'wonders':
        w = next((x for x in D['wonders'] if x['id'] == id), None)
        if not w: return None
        ln = {l['id']: l['name'] for l in w['looks']}; parts = []
        if v.get('look'): parts.append('look ' + ln.get(v['look'], v['look']))
        if v.get('str'): parts.append('strength ' + (('+ ' + ln.get(v['str'][6:], v['str'][6:])) if v['str'].startswith('layer:') else v['str']))
        if v.get('el'): parts.append('element ' + KIT.get(v['el'], {}).get('name', v['el']))
        for sl, sn, opts in w['slots']:
            if v.get('mv_' + sl): parts.append(sn + ': ' + next((o[1] for o in opts if o[0] == v['mv_' + sl]), v['mv_' + sl]))
        if v.get('choice'): parts.append(WORD.get(v['choice'], v['choice']))
        return f'Wonder: {w["name"]}', '; '.join(parts) or 'mark cleared'
    return None


def rows(snap, keys):
    out = []
    for k in sorted(keys):
        if k[0] in ('marks', 'groups'): continue
        r = say(k[0], k[1], snap[k])
        if r: out.append(dict(where=r[0], pick=r[1]))
    return out


def main(args):
    rounds, prev = [], {}
    for i, a in enumerate(args):
        label, date, d = a.split('|')
        snap = load(d)
        changed = [k for k in snap if snap[k] != prev.get(k)]
        r = dict(round=label, date=date, changed=rows(snap, changed))
        if i == len(args) - 1: r['now'] = rows(snap, snap.keys())
        rounds.append(r); prev = snap
    rounds.reverse()   # (newest first, as History reads)
    (HERE / 'picks_log.json').write_text(json.dumps(rounds, indent=1, ensure_ascii=False) + '\n')
    print('picks_log.json:', ', '.join(f'{r["round"]} {len(r["changed"])}' for r in rounds))


if __name__ == '__main__':
    main(sys.argv[1:])
