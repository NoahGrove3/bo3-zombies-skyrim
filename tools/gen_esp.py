"""Write data/BO3Zombies.esp (ESL-flagged) from the sheets and the template records in tools/templates.
Records: Perk-a-Cola magic effects (MGEF) and potions (ALCH), gun items (WEAP, stand-in crossbow models),
and activators (ACTI) for the Mystery Box, perk machines and wall-buys."""
import json, struct, pathlib
from esp_ids import ROOT, sheets, assign, PLUGIN, MASTERS, dressing, blasts
S = sheets(); IDS, NEXT = assign(S, write=True)
SELF = len(MASTERS) << 24
T = {n: json.load(open(ROOT / 'tools/templates' / (n + '.json'))) for n in ['npc_necro_tmpl', 'weap_draugr_gs', 'mgef', 'alch', 'weap_crossbow', 'weap_dwarven', 'acti', 'sndr_ui', 'sndr_3d', 'ligh', 'weap_staff', 'arma_skin', 'armo_skin', 'npc_draugr', 'proj_bullet', 'spel_firebolt', 'mgef_firebolt', 'weap_longbow', 'ammo_iron', 'proj_arrow']}
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
recs = {'WOOP': [], 'SHOU': [], 'GLOB': [], 'FACT': [], 'AMMO': [], 'PROJ': [], 'ENCH': [], 'SPEL': [], 'ARMA': [], 'ARMO': [], 'NPC_': [], 'STAT': [], 'LIGH': [], 'SNDR': [], 'MGEF': [], 'ACTI': [], 'WEAP': [], 'ALCH': []}

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
    radius = {'box': 50, 'perk': 40, 'wallbuy': 40, 'ammo': 40, 'perkmenu': 50, 'forge': 90, 'relic': 50}[o['kind']]
    verb = f"{o['activateText']} ({o['cost']})" if o['cost'] else o['activateText']
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

def cast_mgef(key, label, cast=2):
    md = bytearray(tsub('mgef_firebolt', 'DATA'))
    struct.pack_into('<I', md, 0, 0x200 | 0x400 | 0x800)  # no magnitude/duration/area; not hostile (no crime, no aggro)
    struct.pack_into('<f', md, 4, 0.0)                 # base cost
    struct.pack_into('<I', md, 24, 0); struct.pack_into('<I', md, 32, 0); struct.pack_into('<I', md, 36, 0)
    struct.pack_into('<I', md, 64, 1); struct.pack_into('<i', md, 68, -1)   # script archetype: does nothing itself
    struct.pack_into('<I', md, 72, 0)                  # no projectile: the cast is only the animation (our bolts fly)
    struct.pack_into('<I', md, 76, 0)
    struct.pack_into('<I', md, 80, cast)               # 1 fire-and-forget (one cast per click), 2 concentration
    struct.pack_into('<I', md, 84, 0)                  # self (nothing launched natively)
    for off in (92, 96, 100, 116): struct.pack_into('<I', md, off, 0)  # casting art set at runtime from the matching vanilla spell
    recs['MGEF'].append(record('MGEF', fid('MGEF', key), [
        sub('EDID', z('UN_' + key)), sub('FULL', z(label)), sub('MDOB', tsub('mgef_firebolt', 'MDOB')), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
cast_mgef('wandcast', 'Wand Cast', 1)

# ---- the cities' shouts (sheet: shouts): our own words of power, a voice spell per word count (it does nothing
# itself: Gun::Shout does the effect when the spell is cast), recovery per word count
cast_mgef('shoutcast', 'Thu\'um', 1)
for h in S['shouts']['rows']:
    for n in range(3):
        recs['WOOP'].append(record('WOOP', fid('WOOP', f"shout_{h['id']}_{n}"), [
            sub('EDID', z(f"UN_Word_{h['id']}_{n}")), sub('FULL', z(h['words'][n])), sub('TNAM', z(h['translations'][n]))]))
        sp = struct.pack('<IIIfIIffI', 0, 0x1, 11, 0.0, 1, 0, 0.0, 0.0, 0)   # free, voice, fire-and-forget, self
        recs['SPEL'].append(record('SPEL', fid('SPEL', f"shout_{h['id']}_{n}"), [
            sub('EDID', z(f"UN_ShoutSpell_{h['id']}_{n}")), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z(h['label'])),
            sub('ETYP', struct.pack('<I', 0x025BEE)), sub('DESC', z('')), sub('SPIT', sp),
            sub('EFID', struct.pack('<I', fid('MGEF', 'shoutcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
    snam = [sub('SNAM', struct.pack('<IIf', fid('WOOP', f"shout_{h['id']}_{n}"), fid('SPEL', f"shout_{h['id']}_{n}"), float(h['recovery'][n]))) for n in range(3)]
    recs['SHOU'].append(record('SHOU', fid('SHOU', h['id']), [sub('EDID', z('UN_Shout_' + h['id'])), sub('FULL', z(h['label'])), sub('DESC', z(h['text'] + '.'))] + snam))


for w in S['weapons']['rows']:
    kind = w.get('kind', 'wand')
    if kind == 'wand':
        # enchantment: concentration, aimed, staff-type, free (infinite casts)
        # staff cast (point and release), aimed: Skyrim launches the wand's own element bolt from the tip
        enit = struct.pack('<iIIiIIfII', 0, 0x1, 1, 3000, 0, 0x0C, 0.0, 0, 0)  # fire-and-forget, self (nothing launched natively), staff enchantment, free
        recs['ENCH'].append(record('ENCH', fid('ENCH', w['id']), [
            sub('EDID', z('UN_WandEnch_' + w['id'])), sub('OBND', bytes(12)), sub('FULL', z(w['label'])), sub('ENIT', enit),
            sub('EFID', struct.pack('<I', fid('MGEF', 'wandcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))  # staff cast look only: the bolt is ours (one per click)
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
            # DNAM stays Skyrim's staff (a mace grip left the staff at the feet in third person, unseen in first):
            # Skyrim's own staff cast never plays; our code animates the charge, the thrust and the shot.
            subs.append(sub(name, b))
        recs['WEAP'].append(record('WEAP', fid('WEAP', w['id']), subs))
    elif kind == 'bow':
        # elemental bows: Skyrim's bow (draw animation, nocked arrow) with our model; arrows fly as elemental bolts.
        # Damage 0 on bow and arrow: our code does the hits (the arrow is the look).
        pd = bytearray(tsub('proj_arrow', 'DATA'))
        struct.pack_into('<f', pd, 4, 0.0)                 # no drop: flies straight where you aim
        struct.pack_into('<f', pd, 8, 5200.0)              # speed
        struct.pack_into('<I', pd, 16, int(w['arrowLight'], 16))   # glows in flight
        recs['PROJ'].append(record('PROJ', fid('PROJ', 'arrow_' + w['id']), [
            sub('EDID', z('UN_ArrowProj_' + w['id'])), sub('OBND', tsub('proj_arrow', 'OBND')), sub('FULL', z(w['label'] + ' Arrow')),
            sub('MODL', z(w['arrowFlight'])), sub('DATA', bytes(pd)), sub('NAM1', b'\0'), sub('VNAM', tsub('proj_arrow', 'VNAM'))]))
        ad = struct.pack('<IIfIf', fid('PROJ', 'arrow_' + w['id']), 0x4, 0.0, 0, 0.0)   # projectile, flags (arrow, not bolt), damage, value, weight
        recs['AMMO'].append(record('AMMO', fid('AMMO', 'arrow_' + w['id']), [
            sub('EDID', z('UN_Arrow_' + w['id'])), sub('OBND', tsub('ammo_iron', 'OBND')), sub('FULL', z(w['label'] + ' Arrow')),
            sub('MODL', z(w['arrowModel'])), sub('YNAM', tsub('ammo_iron', 'YNAM')), sub('ZNAM', tsub('ammo_iron', 'ZNAM')),
            sub('KSIZ', tsub('ammo_iron', 'KSIZ')), sub('KWDA', tsub('ammo_iron', 'KWDA')), sub('DATA', ad)]))
        subs = []
        for name, hx in T['weap_longbow']['subs']:
            b = bytes.fromhex(hx)
            if name == 'EDID': b = z('BO3Z_Bow_' + w['id'])
            elif name == 'FULL': b = z(w['label'])
            elif name == 'MODL': b = z(w['standinModel'])
            elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_' + w['id']))
            elif name in ('MODT', 'DESC'): continue
            elif name == 'DATA': b = struct.pack('<IfH', 0, 1.0, 0)
            subs.append(sub(name, b))
        recs['WEAP'].append(record('WEAP', fid('WEAP', w['id']), subs))
    else:
        # tomes and grimoires: a spell in the hand (fire-and-forget), magicka is spent by our code per shot
        cast_mgef('hand_' + w['id'], w['label'], 1)
        sp = bytearray(tsub('spel_firebolt', 'SPIT'))
        struct.pack_into('<IIIfIIffI', sp, 0, 0, 0x1, 0, 0.0, 1, 0, 0.0, 0.0, 0)  # free, fire-and-forget (one push per click), self
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
    files = s.get('files') or [s['file']]
    # Skyrim's own effects pitched down (frequency shift, percent) with a touch of variance; ours as they are
    bnam = struct.pack('<bBBBH', int(s.get('shift', 0)), 4 if s.get('files') else 0, 128, 0, 0)
    recs['SNDR'].append(record('SNDR', fid('SNDR', s['id']), [
        sub('EDID', z(s['editorId'])), sub('CNAM', tsub(tpl, 'CNAM')), sub('GNAM', tsub(tpl, 'GNAM'))] +
        [sub('ANAM', z(fl)) for fl in files] + [sub('ONAM', tsub(tpl, 'ONAM')), sub('LNAM', tsub(tpl, 'LNAM')), sub('BNAM', bnam)]))

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
    struct.pack_into('<H', pd, 0, 0x0)                # plain magic missile: not a supersonic bullet (those draw no model)
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

RACES = {'draugr': 0x000D53, 'breton': 0x013741}
# the Iron Reaper's weapon: a draugr greatsword's two-handed swings, no visible blade (the scythe is part of its body)
ws = []
for name, hx in T['weap_draugr_gs']['subs']:
    b = bytes.fromhex(hx)
    if name == 'EDID': b = z('UN_ReaperScythe')
    elif name == 'FULL': b = z('Reaper Scythe')
    elif name == 'MODL': b = z('UndyingNights\\reaper\\scythe_hidden.nif')
    elif name in ('MODT', 'DESC'): continue
    ws.append(sub(name, b))
recs['WEAP'].append(record('WEAP', fid('WEAP', 'reaper_scythe'), ws))
# Grimmvar's claws (one-handed rakes: a draugr's one-hand swings with nothing in the hand read as claw swipes) and the Gloomstalker's (quick dagger slashes): hidden weapons
# that give each its own native swing set; the scripted moves are in Director.cpp
for key, edid, label, anim, speed, reach, dmg in (('wyrm_claws', 'UN_WyrmClaws', 'Wyrm Claws', 1, 0.9, 1.6, 20),
                                                  ('shadow_claws', 'UN_ShadowClaws', 'Shadow Claws', 2, 1.45, 1.1, 14),
                                                  ('zombie_claws', 'UN_ZombieClaws', 'Grave Hands', 1, 1.15, 0.85, 6)):
    ws = []
    for name, hx in T['weap_draugr_gs']['subs']:
        b = bytearray(bytes.fromhex(hx))
        if name == 'EDID': b = z(edid)
        elif name == 'FULL': b = z(label)
        elif name == 'MODL': b = z('UndyingNights\\reaper\\scythe_hidden.nif')
        elif name in ('MODT', 'DESC'): continue
        elif name == 'DNAM': b[0] = anim; struct.pack_into('<ff', b, 4, speed, reach)
        elif name == 'DATA': struct.pack_into('<H', b, 8, dmg)
        ws.append(sub(name, bytes(b)))
    recs['WEAP'].append(record('WEAP', fid('WEAP', key), ws))

def tmpl_sub(t, name):
    return [bytes.fromhex(h) for n, h in T[t]['subs'] if n == name]

for c in S['creatures']['rows']:
    model = z(c['model'])
    race = RACES[c.get('race', 'draugr')]
    human = c.get('race', 'draugr') != 'draugr'
    bodt = bytearray(tsub('arma_skin', 'BODT'))
    if human:  # the whole figure is the skin: head, hair, body, hands, feet (the race's own parts stay hidden)
        struct.pack_into('<I', bodt, 0, 0x1 | 0x2 | 0x4 | 0x8 | 0x80)
    recs['ARMA'].append(record('ARMA', fid('ARMA', c['id']), [
        sub('EDID', z('UN_' + c['id'] + 'AA')), sub('BODT', bytes(bodt)), sub('RNAM', struct.pack('<I', race)),
        sub('DNAM', tsub('arma_skin', 'DNAM')), sub('MOD2', model), sub('MOD3', model)]))
    recs['ARMO'].append(record('ARMO', fid('ARMO', c['id']), [
        sub('EDID', z('UN_Skin_' + c['id'])), sub('OBND', tsub('armo_skin', 'OBND')), sub('BODT', bytes(bodt)),
        sub('RNAM', struct.pack('<I', race)), sub('MODL', struct.pack('<I', fid('ARMA', c['id']))), sub('DATA', tsub('armo_skin', 'DATA')), sub('DNAM', tsub('armo_skin', 'DNAM'))]))
    src = 'npc_necro_tmpl' if human else 'npc_draugr'
    acbs = bytearray(tsub(src, 'ACBS'))
    struct.pack_into('<H', acbs, 8, int(c['level']))   # level
    struct.pack_into('<H', acbs, 18, 0)                # no template: everything comes from this record
    struct.pack_into('<I', acbs, 0, struct.unpack_from('<I', acbs, 0)[0] & ~0x1)  # male (DraugrRace has no female body)
    aidt = bytearray(tsub(src, 'AIDT')); aidt[0] = 2; aidt[1] = 4   # very aggressive, foolhardy
    subs = [sub('EDID', z('UN_' + c['id'])), sub('OBND', tsub(src, 'OBND')), sub('ACBS', bytes(acbs)),
            sub('VTCK', tsub('npc_draugr', 'VTCK')), sub('RNAM', struct.pack('<I', race)), sub('WNAM', struct.pack('<I', fid('ARMO', c['id'])))]
    if human:  # the necromancer keeps a Skyrim necromancer's spells (frost, conjuration) and caster combat style
        spl = tmpl_sub(src, 'SPLO')
        subs += [sub('SPCT', struct.pack('<I', len(spl)))] + [sub('SPLO', b) for b in spl]
    wkey = {'scythe': 'reaper_scythe', 'wyrmclaws': 'wyrm_claws', 'shadowclaws': 'shadow_claws'}.get(c.get('weapon', 'none'))
    if wkey:
        subs += [sub('COCT', struct.pack('<I', 1)), sub('CNTO', struct.pack('<Ii', fid('WEAP', wkey), 1))]
    subs += [sub('AIDT', bytes(aidt)), sub('CNAM', tsub(src, 'CNAM')), sub('FULL', z(c['label'])), sub('DATA', b''),
             sub('DNAM', tsub(src, 'DNAM')), sub('ZNAM', tsub(src, 'ZNAM')), sub('NAM5', tsub(src, 'NAM5')),
             sub('NAM6', struct.pack('<f', float(c['heightScale']))), sub('NAM7', tsub(src, 'NAM7')), sub('NAM8', tsub(src, 'NAM8')),
             sub('QNAM', tsub(src, 'QNAM'))]
    recs['NPC_'].append(record('NPC_', fid('NPC_', c['id']), subs))

# the night's undead: one faction, allies to each other (no infighting), enemies of the player
recs['FACT'].append(record('FACT', fid('FACT', 'undead'), [
    sub('EDID', z('UN_UndeadFaction')), sub('FULL', z('Undying')),
    sub('XNAM', struct.pack('<IiI', fid('FACT', 'undead'), 0, 2)),     # self: ally
    sub('XNAM', struct.pack('<IiI', 0x00000DB1, -100, 1)),             # PlayerFaction: enemy
    sub('DATA', struct.pack('<I', 0))]))

# the Undying Dirge: each city's progress (step*1000 + progress) and whether the prologue was read; saved with the game
for key, edid in [('curse_' + c['town'], 'UN_Curse_' + c['town']) for c in S['curse']['cities']] + [('curse_intro', 'UN_CurseIntro'), ('stand_layout', 'UN_StandLayout')]:
    recs['GLOB'].append(record('GLOB', fid('GLOB', key), [sub('EDID', z(edid)), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))

# The Undying Dirge: the lore book (given at the first night; the Shrine of the Dirge gives it again)
def dirge_text():
    img = lambda n, w, h: f"<p align='center'><img src='img://Textures/UndyingNights/Book/{n}.png' width='{w}' height='{h}'></p>"
    cu = S['curse']
    towns = []
    for l in S['locations']['rows']:
        if l['town'] not in towns: towns.append(l['town'])
    land = {c['town']: c['landmark'] for c in cu['cities']}
    relic = {w['relicTown']: w for w in S['weapons']['rows'] if w.get('relicTown', 'none') != 'none'}
    gen_city = lambda t: t.replace('{city}', 'the city').replace("{city}'s", "the city's").replace('{landmark}', 'its oldest holy place').replace('{n}', '')
    out = [f"<font face='$HandwrittenFont'>", img('sigil', 200, 200),
           "<p align='center'><font size='30'>THE UNDYING DIRGE</font></p>",
           "<p align='center'>Being a true account of the curse that walks by night, set down by one who lived through Helgen</p>",
           "[pagebreak]", "<p align='center'><font size='24'>I. Helgen</font></p>", img('helgen', 300, 176),
           f"<p>{cu['prologue']}</p>",
           "<p>The priests say the Dirge is no ordinary shout. It was sung, not spoken: a slow word that sank into the earth and stayed there. "
           "By day it sleeps. When the sun goes down it wakes, and every grave in the holds remembers it.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>II. The Nights</font></p>", img('graves', 300, 176),
           "<p>From dusk until the first light the dead climb out of the earth: the Risen first, then the Iron Reapers with their great blades, "
           "then the Voidbound who raise skeletons to fight for them. Each night they come in greater number and harder to kill. "
           "At dawn they crumble to ash.</p>",
           "<p>In the heart of every city stands a shrine raised against them. At its alcoves you will find Sheogorath's Coffer, which gives a spell to anyone willing to gamble for it; "
           "the Brew Machine, whose brews make the drinker stronger; and the Atronach Reforge, which makes any spell fiercer. A spell reforged three times becomes Ascendant, and its great strike falls twice.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>III. Breaking the Dirge</font></p>", img('wards', 300, 176),
           "<p>The Dirge has taken root beneath each city, and each city must be freed its own way: four rites, one after another. Most must be done by night.</p>"]
    for t in towns:
        out.append(f"<p><b>{t}</b></p>")
        for k, r in enumerate(cu['rites'][t]):
            txt = r['text'].replace('{city}', t).replace('{n}', str(r['target']))
            out.append(f"<p>{k + 1}. <i>{r['title'].replace('{city}', t)}.</i> {txt}</p>")
        if t == 'Riverwood':
            out.append(img('herald', 300, 176))
    out += ["<p>When all four rites are done, the Dirge loses its hold on that city. Its dead sleep again, and none will rise within its bounds.</p>",
            "[pagebreak]", "<p align='center'><font size='24'>IV. The Voices of the Holds</font></p>",
            "<p>Each freed city keeps a Thu'um of its own, sleeping in the Shrine of the Dirge at its station. Go to the shrine and it is yours. Speak one word, or hold your breath and speak all three.</p>"]
    for h in S['shouts']['rows']:
        out.append(f"<p><b>{h['town']}</b>: <i>{h['label']}</i> ({' '.join(h['words'])}, '{' '.join(h['translations'])}'). {h['text']}.</p>")
    out += ["[pagebreak]", "<p align='center'><font size='24'>V. Nights of Omen</font></p>", img('omen', 300, 152),
            "<p>Some nights are worse than others. Under the <b>Blood Moon</b> the dead rise faster. On a <b>Fog Night</b> the mist hides a great wyrm of bone, Grimmvar, Herald of the Dirge. "
            "In an <b>Ash Storm</b> the dead run, and every kill is worth twice as much. On the <b>Night of Iron</b> the Reapers and the Voidbound march in force.</p>",
            "<p>Watch the dead for tokens. A skull gives Insta-Kill. A coin gives Double Points. A falling star is the Nuke: it destroys every risen corpse and buys a little quiet. "
            "A chest is a Fire Sale, when the Coffer asks almost nothing.</p>",
            f"<p>{cu['freedAll']}</p>", "</font>"]
    return '\n'.join(out)
recs.setdefault('BOOK', []).append(record('BOOK', fid('BOOK', 'dirge'), [
    sub('EDID', z('UN_UndyingDirgeBook')), sub('OBND', bytes.fromhex('f5fff1fffdff0b000f000300')), sub('FULL', z('The Undying Dirge')),
    sub('MODL', z('Clutter\\Books\\Book02LowPoly02.nif')), sub('DESC', z(dirge_text())),
    sub('KSIZ', struct.pack('<I', 1)), sub('KWDA', struct.pack('<I', 0x000937A2)),
    sub('DATA', struct.pack('<BBHIIf', 0, 0, 0, 0, 25, 1.0)), sub('INAM', struct.pack('<I', 0x0008ADAE)), sub('CNAM', struct.pack('<I', 0))]))

# (spell blasts are cloned from Skyrim's explosions at runtime: EXPL records in an ESL crashed the game at load)
count = sum(len(v) for v in recs.values())
hdr = [sub('HEDR', struct.pack('<fiI', 1.71, count, NEXT)), sub('CNAM', z('Noah Grove')), sub('SNAM', z('Undying Nights: round-based undead survival for Skyrim'))]
for m in MASTERS: hdr += [sub('MAST', z(m)), sub('DATA', struct.pack('<Q', 0))]
out = record('TES4', 0, hdr, flags=0x200)  # ESL flag
for k in ['GLOB', 'FACT', 'MGEF', 'BOOK', 'ENCH', 'SPEL', 'WOOP', 'SHOU', 'ACTI', 'ARMO', 'LIGH', 'STAT', 'WEAP', 'AMMO', 'NPC_', 'ALCH', 'PROJ', 'ARMA', 'SNDR']: out += group(k, recs[k])  # Bethesda's group order: effects load before the spells that use them
(ROOT / 'data').mkdir(exist_ok=True)
(ROOT / 'data' / PLUGIN).write_bytes(out)
print('wrote', PLUGIN, len(out), 'bytes,', count, 'records')
