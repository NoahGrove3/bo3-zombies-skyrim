"""Form IDs for BO3Zombies.esp. Frozen in data/form_ids.json (saves refer to them); new rows get the next free id."""
import json, pathlib
ROOT = pathlib.Path(__file__).parent.parent
IDFILE = ROOT / 'data' / 'form_ids.json'
def sheets():
    return {json.load(open(p))['sheet']: json.load(open(p)) for p in sorted((ROOT / 'sheets').glob('*.json'))}
def assign(S=None, write=False):
    S = S or sheets()
    store = json.load(open(IDFILE)) if IDFILE.exists() else {"about": "", "ids": {}}
    frozen = {k: int(v, 16) for k, v in store['ids'].items()}
    nxt = max(frozen.values(), default=0x7FF) + 1
    ids = {}
    wanted = [('MGEF', p['id']) for p in S['perks']['rows']] + [('ACTI', o['id']) for o in S['objects']['rows']] + \
             [('WEAP', w['id']) for w in S['weapons']['rows']] + [('ALCH', p['id']) for p in S['perks']['rows']] + \
             [('SNDR', s['id']) for s in S['sounds']['rows'] if s['plugin'] == PLUGIN] + \
             list(dressing(S)[0].keys())
    for kind, key in wanted:
        k = f'{kind}:{key}'
        if k not in frozen:
            frozen[k] = nxt; nxt += 1
        ids[(kind, key)] = frozen[k]
    if write:
        store['ids'] = {k: hex(v) for k, v in frozen.items()}
        IDFILE.write_text(json.dumps(store, indent=1))
    return ids, nxt
PLUGIN = 'BO3Zombies.esp'
MASTERS = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm']

def dressing(S):
    """Expand the dressing sheet into concrete pieces: one per (dressing row x town object it applies to)
    for '$' rows, one shared record otherwise. Returns (records, pieces):
    records: {(kind, key): {'model'|'color'|'radius'}}; pieces: [(object_index, kind, key, row)]."""
    perks = {p['id']: p for p in S['perks']['rows']}
    records, pieces = {}, []
    for oi, o in enumerate(S['objects']['rows']):
        for r in S['dressing']['rows']:
            if r['for'] not in (o['kind'], 'objects:' + o['id']): continue
            perk = perks.get(o['refs'].split(':')[1]) if o['refs'].startswith('perks:') else None
            per_obj = r['model'] == '$potion' or r['color'] == '$perk'
            key = f"{r['id']}_{o['id']}" if per_obj else r['id']
            kind = 'STAT' if r['type'] == 'stat' else 'LIGH'
            records[(kind, key)] = {'model': perk['potionModel'] if r['model'] == '$potion' else r['model'],
                                    'color': perk['color'] if r['color'] == '$perk' else r['color'], 'radius': r['radius']}
            pieces.append((oi, kind, key, r))
    for w in S['weapons']['rows']:
        records[('STAT', '1st_' + w['id'])] = {'model': w['standinModel'], 'color': '', 'radius': 0}
        records[('STAT', 'show_' + w['id'])] = {'model': w['standinModel'], 'color': '', 'radius': 0}
    records[('STAT', 'show_cheese')] = {'model': S['dressing']['show']['cheeseModel'], 'color': '', 'radius': 0}
    records[('STAT', 'rise_fx')] = {'model': S['dressing']['show']['riseFxModel'], 'color': '', 'radius': 0}
    return records, pieces
