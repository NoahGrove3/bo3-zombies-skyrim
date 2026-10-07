"""Write data/BO3Zombies.esp (ESL-flagged) from the sheets and the template records in tools/templates.
Records: Perk-a-Cola magic effects (MGEF) and potions (ALCH), gun items (WEAP, stand-in crossbow models),
and activators (ACTI) for the Mystery Box, perk machines and wall-buys."""
import json, struct, pathlib
from esp_ids import ROOT, sheets, assign, PLUGIN, MASTERS, dressing
S = sheets(); IDS, NEXT = assign(S, write=True)
SELF = len(MASTERS) << 24
T = {n: json.load(open(ROOT / 'tools/templates' / (n + '.json'))) for n in ['mgef', 'alch', 'weap_crossbow', 'weap_dwarven', 'acti', 'sndr_ui', 'sndr_3d', 'ligh', 'weap_staff']}
def tsub(t, name): return bytes.fromhex(dict(T[t]['subs'])[name])
def z(s): return s.encode('cp1252') + b'\0'
def sub(name, data):
    assert len(data) < 65536
    return name.encode() + struct.pack('<H', len(data)) + data
def record(rtype, fid, subs, flags=0):
    body = b''.join(subs)
    return rtype.encode() + struct.pack('<IIIIHH', len(body), flags, fid, 0, 44, 0) + body
def group(label, recs):
    data = b''.join(recs)
    return b'GRUP' + struct.pack('<I', 24 + len(data)) + label.encode() + struct.pack('<iHHHH', 0, 0, 0, 44, 0) + data
fid = lambda kind, key: SELF | IDS[(kind, key)]
recs = {'STAT': [], 'LIGH': [], 'SNDR': [], 'MGEF': [], 'ACTI': [], 'WEAP': [], 'ALCH': []}

for p in S['perks']['rows']:
    data = bytearray(tsub('mgef', 'DATA'))
    struct.pack_into('<I', data, 0, 0x200 | 0x400 | 0x800)  # no duration / magnitude / area
    struct.pack_into('<I', data, 32, 0)                      # no effect shader
    struct.pack_into('<I', data, 64, 1)                      # archetype: Script (no script attached: a marker effect)
    struct.pack_into('<i', data, 68, -1)                     # no actor value
    struct.pack_into('<I', data, 96, 0)                      # no hit art
    recs['MGEF'].append(record('MGEF', fid('MGEF', p['id']), [
        sub('EDID', z('BO3Z_Perk_' + p['id'])), sub('FULL', z(p['label'])), sub('MDOB', tsub('mgef', 'MDOB')),
        sub('DATA', bytes(data)), sub('SNDD', tsub('mgef', 'SNDD')), sub('DNAM', z(p['description']))]))

def obnd(r): return struct.pack('<6h', -r, -r, 0, r, r, r * 2)
for o in S['objects']['rows']:
    radius = {'box': 50, 'perk': 40, 'wallbuy': 40, 'ammo': 40, 'perkmenu': 50}[o['kind']]
    verb = f"{o['activateText']} ({o['cost']})"
    recs['ACTI'].append(record('ACTI', fid('ACTI', o['id']), [
        sub('EDID', z('BO3Z_' + o['id'])), sub('OBND', obnd(radius)), sub('FULL', z(o['label'])),
        sub('MODL', z(o['model'])), sub('RNAM', z(verb)), sub('FNAM', b'\0\0')]))

for w in S['weapons']['rows']:
    tpl = 'weap_staff' if w.get('template') == 'staff' else ('weap_crossbow' if 'Weapons\\Crossbow' in w['standinModel'] else 'weap_dwarven')
    subs = []
    for name, hx in T[tpl]['subs']:
        b = bytes.fromhex(hx)
        if name == 'EDID': b = z('BO3Z_Gun_' + w['id'])
        elif name == 'FULL': b = z(w['label'])
        elif name == 'MODL': b = z(w['standinModel'])
        elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_' + w['id']))  # first-person model: our own mesh
        elif name in ('MODT', 'DESC', 'EITM', 'EAMT'): continue
        elif name == 'DATA': b = struct.pack('<IfH', 0, 4.0, 0)
        subs.append(sub(name, b))
    recs['WEAP'].append(record('WEAP', fid('WEAP', w['id']), subs))

for p in S['perks']['rows']:
    enit = bytearray(tsub('alch', 'ENIT'))
    struct.pack_into('<iIIf', enit, 0, 0, 0x1, 0, 0.0)       # value 0, no auto-calc, no addiction
    recs['ALCH'].append(record('ALCH', fid('ALCH', p['id']), [
        sub('EDID', z('BO3Z_PerkCola_' + p['id'])), sub('OBND', tsub('alch', 'OBND')), sub('FULL', z(p['label'])),
        sub('KSIZ', tsub('alch', 'KSIZ')), sub('KWDA', tsub('alch', 'KWDA')), sub('MODL', z(p['potionModel'])),
        sub('YNAM', tsub('alch', 'YNAM')), sub('ZNAM', tsub('alch', 'ZNAM')), sub('DATA', struct.pack('<f', 0.5)),
        sub('ENIT', bytes(enit)), sub('EFID', struct.pack('<I', fid('MGEF', p['id']))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))

for s in S['sounds']['rows']:
    if s['plugin'] != PLUGIN: continue
    tpl = 'sndr_3d' if s['space'] == '3d' else 'sndr_ui'
    bnam = struct.pack('<BBBBH', 0, 0, 128, 0, 0)  # no pitch/volume variance, priority 128, no extra attenuation
    recs['SNDR'].append(record('SNDR', fid('SNDR', s['id']), [
        sub('EDID', z(s['editorId'])), sub('CNAM', tsub(tpl, 'CNAM')), sub('GNAM', tsub(tpl, 'GNAM')),
        sub('ANAM', z(s['file'])), sub('ONAM', tsub(tpl, 'ONAM')), sub('LNAM', tsub(tpl, 'LNAM')), sub('BNAM', bnam)]))

DREC, _ = dressing(S)
for (kind, key), r in DREC.items():
    if kind == 'STAT':
        recs['STAT'].append(record('STAT', fid('STAT', key), [
            sub('EDID', z('UN_' + key)), sub('OBND', struct.pack('<6h', -30, -30, 0, 30, 30, 60)),
            sub('MODL', z(r['model'])), sub('DNAM', struct.pack('<fI', 90.0, 0))]))
    else:
        data = bytearray(tsub('ligh', 'DATA'))
        c = int(r['color'], 16)
        struct.pack_into('<I', data, 4, int(r['radius']))
        struct.pack_into('<BBBB', data, 8, c >> 16, (c >> 8) & 255, c & 255, 0)
        recs['LIGH'].append(record('LIGH', fid('LIGH', key), [
            sub('EDID', z('UN_' + key)), sub('OBND', tsub('ligh', 'OBND')), sub('DATA', bytes(data)), sub('FNAM', struct.pack('<f', 1.0))]))

count = sum(len(v) for v in recs.values())
hdr = [sub('HEDR', struct.pack('<fiI', 1.71, count, NEXT)), sub('CNAM', z('Noah Grove')), sub('SNAM', z('Undying Nights: round-based undead survival for Skyrim'))]
for m in MASTERS: hdr += [sub('MAST', z(m)), sub('DATA', struct.pack('<Q', 0))]
out = record('TES4', 0, hdr, flags=0x200)  # ESL flag
for k in ['STAT', 'LIGH', 'SNDR', 'MGEF', 'ACTI', 'WEAP', 'ALCH']: out += group(k, recs[k])
(ROOT / 'data').mkdir(exist_ok=True)
(ROOT / 'data' / PLUGIN).write_bytes(out)
print('wrote', PLUGIN, len(out), 'bytes,', count, 'records')
