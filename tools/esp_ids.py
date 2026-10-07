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
             [('SNDR', s['id']) for s in S['sounds']['rows'] if s['plugin'] == PLUGIN]
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
