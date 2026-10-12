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
             list(dressing(S)[0].keys()) + \
             [(k, c['id']) for c in S['creatures']['rows'] for k in ('ARMA', 'ARMO', 'NPC_')] + [('ARMA', 'eyes_' + c['id']) for c in S['creatures']['rows']] + [('WEAP', 'reaper_scythe'), ('WEAP', 'wyrm_claws'), ('WEAP', 'shadow_claws'), ('WEAP', 'zombie_claws')] + \
             [(k, 'bullet_' + p['id']) for p in S['projectiles']['rows'] for k in ('PROJ', 'MGEF', 'SPEL')] + [('LIGH', 'muzzle'), ('PROJ', 'castflame'), ('MGEF', 'wandcast'), ('FACT', 'undead')] + \
             [('ENCH', w['id']) for w in S['weapons']['rows'] if w.get('kind') == 'wand'] + \
             [(k, 'hand_' + w['id']) for w in S['weapons']['rows'] if w.get('kind') in ('tome', 'wonder') for k in ('SPEL', 'MGEF')] + \
             [(k, 'arrow_' + w['id']) for w in S['weapons']['rows'] if w.get('kind') == 'bow' for k in ('AMMO', 'PROJ')] + \
             [(k, 'shout_' + h['id'] + '_' + str(n)) for h in S['shouts']['rows'] for n in range(3) for k in ('WOOP', 'SPEL')] + [('SHOU', h['id']) for h in S['shouts']['rows']] + [('MGEF', 'shoutcast')] + [('GLOB', 'curse_' + c['town']) for c in S['curse']['cities']] + [('GLOB', 'curse_intro'), ('GLOB', 'stand_layout'), ('BOOK', 'dirge')] + \
             [('VTYP', 'silent'), ('MUST', 'boss'), ('MUSC', 'boss'), ('MUST', 'freed'), ('MUSC', 'freed'), ('MUST', 'intro'), ('MUSC', 'intro'), ('FSTS', 'silent'), ('STAT', 'ls_shrine'), ('STAT', 'ls_book')] + [('LSCR', f'ls_{i}') for i in range(10)]
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
def blasts():
    """Spell blasts: no-damage copies of Skyrim's explosions (tools/templates/expl/<name>.json)."""
    return sorted(q.stem for q in (ROOT / 'tools' / 'templates' / 'expl').glob('*.json'))
MASTERS = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm']

POWERUPS = [('instakill', 'FF3030'), ('double', 'FFD040'), ('kaboom', 'FF8A20'), ('firesale', 'B060FF')]

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
    for k, col in POWERUPS:   # BO3 power-up drops: a glowing token and its light
        records[('STAT', 'pu_' + k)] = {'model': f'UndyingNights\\powerup\\{k}.nif', 'color': '', 'radius': 0}
        records[('LIGH', 'pu_' + k)] = {'model': '', 'color': col, 'radius': 260}
    return records, pieces
