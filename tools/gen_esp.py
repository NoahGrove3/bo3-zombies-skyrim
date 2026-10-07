"""Write data/BO3Zombies.esp (ESL-flagged) from the sheets and the template records in tools/templates.
Records: Perk-a-Cola magic effects (MGEF) and potions (ALCH), gun items (WEAP, stand-in crossbow models),
and activators (ACTI) for the Mystery Box, perk machines and wall-buys."""
import json, struct, pathlib
from esp_ids import ROOT, sheets, assign, PLUGIN, MASTERS, dressing
S = sheets(); IDS, NEXT = assign(S, write=True)
SELF = len(MASTERS) << 24
T = {n: json.load(open(ROOT / 'tools/templates' / (n + '.json'))) for n in ['mgef', 'alch', 'weap_crossbow', 'weap_dwarven', 'acti', 'sndr_ui', 'sndr_3d', 'ligh', 'weap_staff', 'arma_skin', 'armo_skin', 'npc_draugr', 'proj_bullet', 'spel_firebolt', 'mgef_firebolt']}
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
recs = {'AMMO': [], 'PROJ': [], 'ENCH': [], 'SPEL': [], 'ARMA': [], 'ARMO': [], 'NPC_': [], 'STAT': [], 'LIGH': [], 'SNDR': [], 'MGEF': [], 'ACTI': [], 'WEAP': [], 'ALCH': []}

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
    radius = {'box': 50, 'perk': 40, 'wallbuy': 40, 'ammo': 40, 'perkmenu': 50, 'forge': 90}[o['kind']]
    verb = f"{o['activateText']} ({o['cost']})"
    recs['ACTI'].append(record('ACTI', fid('ACTI', o['id']), [
        sub('EDID', z('BO3Z_' + o['id'])), sub('OBND', obnd(radius)), sub('FULL', z(o['label'])),
        sub('MODL', z(o['model'])), sub('RNAM', z(verb)), sub('FNAM', b'\0\0')]))

# ---- casting: every weapon is cast natively (staff or hand animation), our code does the hits ----
# carrier projectile for the concentration casts: flame type, no model, no effect (our bolts are the visuals)
pd = bytearray(tsub('proj_bullet', 'DATA'))
struct.pack_into('<H', pd, 2, 0x08)                    # type: flame (concentration)
struct.pack_into('<f', pd, 4, 0.0); struct.pack_into('<f', pd, 8, 1500.0); struct.pack_into('<f', pd, 12, 300.0)
struct.pack_into('<I', pd, 16, 0); struct.pack_into('<I', pd, 20, 0)   # no light, no muzzle light
struct.pack_into('<I', pd, 36, 0); struct.pack_into('<I', pd, 40, 0)
recs['PROJ'].append(record('PROJ', fid('PROJ', 'castflame'), [
    sub('EDID', z('UN_CastCarrier')), sub('OBND', tsub('proj_bullet', 'OBND')), sub('MODL', z('')),
    sub('DATA', bytes(pd)), sub('NAM1', b'\0'), sub('VNAM', struct.pack('<I', 0))]))

def cast_mgef(key, label):
    md = bytearray(tsub('mgef_firebolt', 'DATA'))
    struct.pack_into('<I', md, 0, 0x200 | 0x400 | 0x800)  # no magnitude/duration/area; not hostile (no crime, no aggro)
    struct.pack_into('<f', md, 4, 0.0)                 # base cost
    struct.pack_into('<I', md, 24, 0); struct.pack_into('<I', md, 32, 0); struct.pack_into('<I', md, 36, 0)
    struct.pack_into('<I', md, 64, 1); struct.pack_into('<i', md, 68, -1)   # script archetype: does nothing itself
    struct.pack_into('<I', md, 72, fid('PROJ', 'castflame'))
    struct.pack_into('<I', md, 76, 0)
    struct.pack_into('<I', md, 80, 2)                  # concentration
    struct.pack_into('<I', md, 84, 2)                  # aimed
    for off in (92, 96, 100, 116): struct.pack_into('<I', md, off, 0)  # casting art set at runtime from the matching vanilla spell
    recs['MGEF'].append(record('MGEF', fid('MGEF', key), [
        sub('EDID', z('UN_' + key)), sub('FULL', z(label)), sub('MDOB', tsub('mgef_firebolt', 'MDOB')), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
cast_mgef('wandcast', 'Wand Cast')

for w in S['weapons']['rows']:
    kind = w.get('kind', 'wand')
    if kind == 'wand':
        # enchantment: concentration, aimed, staff-type, free (infinite casts)
        enit = struct.pack('<iIIiIIfII', 0, 0x1, 2, 3000, 2, 0x0C, 0.0, 0, 0)
        recs['ENCH'].append(record('ENCH', fid('ENCH', w['id']), [
            sub('EDID', z('UN_WandEnch_' + w['id'])), sub('OBND', bytes(12)), sub('FULL', z(w['label'])), sub('ENIT', enit),
            sub('EFID', struct.pack('<I', fid('MGEF', 'wandcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
        subs = []
        for name, hx in T['weap_staff']['subs']:
            b = bytes.fromhex(hx)
            if name == 'EDID': b = z('BO3Z_Wand_' + w['id'])
            elif name == 'FULL': b = z(w['label'])
            elif name == 'MODL': b = z(w['standinModel'])
            elif name == 'EITM': b = struct.pack('<I', fid('ENCH', w['id']))
            elif name == 'EAMT': b = struct.pack('<H', 3000)
            elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_' + w['id']))
            elif name in ('MODT', 'DESC'): continue
            elif name == 'DATA': b = struct.pack('<IfH', 0, 1.0, 0)
            subs.append(sub(name, b))
        recs['WEAP'].append(record('WEAP', fid('WEAP', w['id']), subs))
    else:
        # tomes and grimoires: a spell in the hand (concentration), magicka is spent by our code per shot
        cast_mgef('hand_' + w['id'], w['label'])
        sp = bytearray(tsub('spel_firebolt', 'SPIT'))
        struct.pack_into('<IIIfIIffI', sp, 0, 0, 0x1, 0, 0.0, 2, 2, 0.0, 0.0, 0)
        recs['SPEL'].append(record('SPEL', fid('SPEL', 'hand_' + w['id']), [
            sub('EDID', z('UN_Hand_' + w['id'])), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z(w['label'])),
            sub('MDOB', tsub('spel_firebolt', 'MDOB')), sub('ETYP', tsub('spel_firebolt', 'ETYP')), sub('DESC', z('')),
            sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', 'hand_' + w['id']))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
        # a WEAP still exists (kept form id, box/wall display); never handed out
        subs = []
        for name, hx in T['weap_staff']['subs']:
            b = bytes.fromhex(hx)
            if name == 'EDID': b = z('BO3Z_Tome_' + w['id'])
            elif name == 'FULL': b = z(w['label'])
            elif name == 'MODL': b = z(w['standinModel'])
            elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_' + w['id']))
            elif name in ('MODT', 'DESC', 'EITM', 'EAMT'): continue
            elif name == 'DATA': b = struct.pack('<IfH', 0, 1.0, 0)
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

# bullet tracers: fast missiles that do nothing themselves (hitscan does the damage)
for p in S['projectiles']['rows']:
    key = 'bullet_' + p['id']
    pd = bytearray(tsub('proj_bullet', 'DATA'))
    struct.pack_into('<f', pd, 4, 0.0)                # no drop
    struct.pack_into('<f', pd, 8, float(p['speed']))  # speed
    struct.pack_into('<I', pd, 36, 0)                 # no explosion
    struct.pack_into('<I', pd, 40, 0)                 # no flight sound
    recs['PROJ'].append(record('PROJ', fid('PROJ', key), [
        sub('EDID', z('UN_' + key)), sub('OBND', tsub('proj_bullet', 'OBND')), sub('MODL', z(p['model'])),
        sub('DATA', bytes(pd)), sub('NAM1', b'\0'), sub('VNAM', struct.pack('<I', 0))]))
    md = bytearray(tsub('mgef_firebolt', 'DATA'))
    struct.pack_into('<I', md, 0, 0x200 | 0x400 | 0x800)  # not hostile: a stray bolt is no crime
    struct.pack_into('<I', md, 24, 0)                 # no light
    struct.pack_into('<I', md, 32, 0)                 # no hit shader
    struct.pack_into('<I', md, 64, 1)                 # archetype: script (no effect)
    struct.pack_into('<i', md, 68, -1)
    struct.pack_into('<I', md, 72, fid('PROJ', key))  # our tracer
    struct.pack_into('<I', md, 76, 0)                 # no explosion
    struct.pack_into('<I', md, 92, 0)                 # no casting art in the hand
    struct.pack_into('<I', md, 96, 0)                 # no hit art
    struct.pack_into('<I', md, 100, 0)                # no impact data
    recs['MGEF'].append(record('MGEF', fid('MGEF', key), [
        sub('EDID', z('UN_' + key + 'Effect')), sub('MDOB', tsub('mgef_firebolt', 'MDOB')), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
    sp = bytearray(tsub('spel_firebolt', 'SPIT'))
    struct.pack_into('<I', sp, 0, 0)                  # cost 0
    struct.pack_into('<I', sp, 4, 0x1)                # manual cost, no auto-calc
    recs['SPEL'].append(record('SPEL', fid('SPEL', key), [
        sub('EDID', z('UN_' + key + 'Spell')), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('ETYP', tsub('spel_firebolt', 'ETYP')),
        sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', key))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
# muzzle flash light
ld = bytearray(tsub('ligh', 'DATA'))
struct.pack_into('<I', ld, 4, 320); struct.pack_into('<BBBB', ld, 8, 255, 170, 70, 0)
recs['LIGH'].append(record('LIGH', fid('LIGH', 'muzzle'), [sub('EDID', z('UN_MuzzleFlash')), sub('OBND', tsub('ligh', 'OBND')), sub('DATA', bytes(ld)), sub('FNAM', struct.pack('<f', 1.0))]))

for c in S['creatures']['rows']:
    model = z(c['model'])
    recs['ARMA'].append(record('ARMA', fid('ARMA', c['id']), [
        sub('EDID', z('UN_' + c['id'] + 'AA')), sub('BODT', tsub('arma_skin', 'BODT')), sub('RNAM', struct.pack('<I', 0x000D53)),
        sub('DNAM', tsub('arma_skin', 'DNAM')), sub('MOD2', model), sub('MOD3', model)]))
    recs['ARMO'].append(record('ARMO', fid('ARMO', c['id']), [
        sub('EDID', z('UN_Skin_' + c['id'])), sub('OBND', tsub('armo_skin', 'OBND')), sub('BODT', tsub('armo_skin', 'BODT')),
        sub('RNAM', struct.pack('<I', 0x000D53)), sub('MODL', struct.pack('<I', fid('ARMA', c['id']))), sub('DATA', tsub('armo_skin', 'DATA')), sub('DNAM', tsub('armo_skin', 'DNAM'))]))
    acbs = bytearray(tsub('npc_draugr', 'ACBS'))
    struct.pack_into('<H', acbs, 8, int(c['level']))   # level
    struct.pack_into('<H', acbs, 18, 0)                # no template: everything comes from this record
    struct.pack_into('<I', acbs, 0, struct.unpack_from('<I', acbs, 0)[0] & ~0x1)  # male: DraugrRace has no female body/skeleton (female = invisible)
    aidt = bytearray(tsub('npc_draugr', 'AIDT')); aidt[0] = 2; aidt[1] = 4   # very aggressive, foolhardy
    recs['NPC_'].append(record('NPC_', fid('NPC_', c['id']), [
        sub('EDID', z('UN_' + c['id'])), sub('OBND', tsub('npc_draugr', 'OBND')), sub('ACBS', bytes(acbs)),
        sub('VTCK', tsub('npc_draugr', 'VTCK')), sub('RNAM', struct.pack('<I', 0x000D53)), sub('WNAM', struct.pack('<I', fid('ARMO', c['id']))),
        sub('AIDT', bytes(aidt)), sub('CNAM', tsub('npc_draugr', 'CNAM')), sub('FULL', z(c['label'])), sub('DATA', b''),
        sub('DNAM', tsub('npc_draugr', 'DNAM')), sub('ZNAM', tsub('npc_draugr', 'ZNAM')), sub('NAM5', tsub('npc_draugr', 'NAM5')),
        sub('NAM6', struct.pack('<f', float(c['heightScale']))), sub('NAM7', tsub('npc_draugr', 'NAM7')), sub('NAM8', tsub('npc_draugr', 'NAM8')),
        sub('QNAM', tsub('npc_draugr', 'QNAM'))]))

count = sum(len(v) for v in recs.values())
hdr = [sub('HEDR', struct.pack('<fiI', 1.71, count, NEXT)), sub('CNAM', z('Noah Grove')), sub('SNAM', z('Undying Nights: round-based undead survival for Skyrim'))]
for m in MASTERS: hdr += [sub('MAST', z(m)), sub('DATA', struct.pack('<Q', 0))]
out = record('TES4', 0, hdr, flags=0x200)  # ESL flag
for k in ['AMMO', 'PROJ', 'ENCH', 'SPEL', 'ARMA', 'ARMO', 'NPC_', 'STAT', 'LIGH', 'SNDR', 'MGEF', 'ACTI', 'WEAP', 'ALCH']: out += group(k, recs[k])
(ROOT / 'data').mkdir(exist_ok=True)
(ROOT / 'data' / PLUGIN).write_bytes(out)
print('wrote', PLUGIN, len(out), 'bytes,', count, 'records')
