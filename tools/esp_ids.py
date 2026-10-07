"""Form IDs for BO3Zombies.esp, assigned in sheet order (light plugin: object ids 0x800+)."""
import json, pathlib
ROOT = pathlib.Path(__file__).parent.parent
def sheets():
    return {json.load(open(p))['sheet']: json.load(open(p)) for p in sorted((ROOT / 'sheets').glob('*.json'))}
def assign(S=None):
    S = S or sheets()
    ids = {}; n = 0x800
    def take(kind, key):
        nonlocal n
        ids[(kind, key)] = n; n += 1
    for p in S['perks']['rows']: take('MGEF', p['id'])
    for o in S['objects']['rows']: take('ACTI', o['id'])
    for w in S['weapons']['rows']: take('WEAP', w['id'])
    for p in S['perks']['rows']: take('ALCH', p['id'])
    return ids, n
PLUGIN = 'BO3Zombies.esp'
MASTERS = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm']
