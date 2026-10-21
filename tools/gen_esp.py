"""Write data/CurseOfAlduin.esp (ESL-flagged) from the sheets and the template records in tools/templates.
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
recs = {'WOOP': [], 'SHOU': [], 'GLOB': [], 'FACT': [], 'AMMO': [], 'PROJ': [], 'ENCH': [], 'SPEL': [], 'ARMA': [], 'ARMO': [], 'NPC_': [], 'STAT': [], 'LIGH': [], 'SNDR': [], 'MGEF': [], 'ACTI': [], 'WEAP': [], 'ALCH': [], 'MUSC': [], 'MUST': [], 'VTYP': [], 'FSTS': [], 'LSCR': [], 'ARTO': [], 'QUST': []}

for p in S['perks']['rows']:
    data = bytearray(tsub('mgef', 'DATA'))
    struct.pack_into('<I', data, 0, 0x200 | 0x400 | 0x800)  # no duration / magnitude / area
    struct.pack_into('<I', data, 32, 0)                      # no effect shader
    struct.pack_into('<I', data, 64, 1)                      # archetype: Script (no script attached: a marker effect)
    struct.pack_into('<i', data, 68, -1)                     # no actor value
    struct.pack_into('<I', data, 96, 0)                      # no hit art
    recs['MGEF'].append(record('MGEF', fid('MGEF', p['id']), [
        sub('EDID', z('BO3Z_Perk_' + p['id'])), sub('FULL', z(p['label'])), sub('MDOB', tsub('mgef', 'MDOB')),
        sub('DATA', bytes(data)), sub('DNAM', z(p['description']))]))  # (no SNDD: no Skyrim sounds on the brews)

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

# ---- the Echo Altar's roll (src/Shops.cpp): each column's rune stone strikes the heart stone with Skyrim's own lightning
# bolt beam (Skyrim.esm ShockBoltAim, its MAGShock02 impacts), cast from a marker at the stone to a marker in the heart;
# ShockDamageFFAimed's data with no sounds (Shops plays a vanilla trap bolt once, on each strike), no harm, no hit art
md = bytearray(bytes.fromhex('050820100000c03f00000000140000002a00000000000000687c05000ad7233c677c05000000000019000000000000000000003f00000000cdcccc3d0000003f05000000180000009c8e0500000000000100000002000000190000009b8e050000000000d99e05000000803f000000000000803f000000000000000000000000000000000000000000000000000000000000000000000000'))
struct.pack_into('<I', md, 0, 0x200 | 0x400 | 0x800)  # not hostile, no magnitude / duration / area
struct.pack_into('<f', md, 4, 0.0)
struct.pack_into('<I', md, 32, 0); struct.pack_into('<I', md, 36, 0)   # no shaders on what it touches
struct.pack_into('<I', md, 64, 1); struct.pack_into('<i', md, 68, -1)  # script archetype: does nothing itself
struct.pack_into('<I', md, 80, 1); struct.pack_into('<I', md, 84, 2)    # fire-and-forget, aimed
for off in (88, 92, 96): struct.pack_into('<I', md, off, 0)             # no second value, casting art or hit art
recs['MGEF'].append(record('MGEF', fid('MGEF', 'altarbeam'), [
    sub('EDID', z('UN_AltarBeam')), sub('FULL', z('Altar Beam')), sub('MDOB', bytes.fromhex('a5350400')), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
recs['SPEL'].append(record('SPEL', fid('SPEL', 'altarbeam'), [
    sub('EDID', z('UN_AltarBeamSpell')), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z('Altar Beam')),
    sub('MDOB', bytes.fromhex('a5350400')), sub('ETYP', tsub('spel_firebolt', 'ETYP')), sub('DESC', z('')),
    sub('SPIT', struct.pack('<IIIfIIffI', 0, 0x1, 0, 0.0, 1, 2, 0.0, 0.0, 0)), sub('EFID', struct.pack('<I', fid('MGEF', 'altarbeam'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))

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

# ---- Bond II's shout (src/Movement.cpp Updraft): a whirlwind lifts you high, then you open the glider. Every word
# count does the same; taught whole when the bond is bought
UPDRAFT_WORDS = [('VEN', 'Wind'), ('GAAR', 'Unleash'), ('NOS', 'Strike')]
for n, (w, tr) in enumerate(UPDRAFT_WORDS):
    recs['WOOP'].append(record('WOOP', fid('WOOP', f'updraft_{n}'), [sub('EDID', z(f'UN_Word_updraft_{n}')), sub('FULL', z(w)), sub('TNAM', z(tr))]))
    sp = struct.pack('<IIIfIIffI', 0, 0x1, 11, 0.0, 1, 0, 0.0, 0.0, 0)
    recs['SPEL'].append(record('SPEL', fid('SPEL', f'updraft_{n}'), [
        sub('EDID', z(f'UN_ShoutSpell_updraft_{n}')), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z("Dragon's Updraft")),
        sub('ETYP', struct.pack('<I', 0x025BEE)), sub('DESC', z('')), sub('SPIT', sp),
        sub('EFID', struct.pack('<I', fid('MGEF', 'shoutcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
recs['SHOU'].append(record('SHOU', fid('SHOU', 'updraft'), [sub('EDID', z('UN_Shout_updraft')), sub('FULL', z("Dragon's Updraft")),
    sub('DESC', z('A whirlwind lifts you high into the air - open your glider and your dragon takes you.'))] +
    [sub('SNAM', struct.pack('<IIf', fid('WOOP', f'updraft_{n}'), fid('SPEL', f'updraft_{n}'), 5.0)) for n in range(3)]))

SCRAFT = {r['id']: r for r in S['spellcraft']['rows']}
def arto(key, model, kind):
    recs['ARTO'].append(record('ARTO', fid('ARTO', key), [sub('EDID', z('UN_Art_' + key)), sub('OBND', struct.pack('<6h', -20, -20, -20, 20, 20, 20)),
        sub('MODL', z(model)), sub('DNAM', struct.pack('<I', kind))]))
for r in SCRAFT.values():
    arto('fx_' + r['id'] + '_hand', 'CurseOfAlduin\\fx\\' + r['id'] + '_hand.nif', 0)          # magic casting art
    recs['STAT'].append(record('STAT', fid('STAT', 'fx_' + r['id'] + '_orb'), [sub('EDID', z('UN_Orb_' + r['id'])), sub('OBND', struct.pack('<6h', -30, -30, -30, 30, 30, 30)),
        sub('MODL', z('CurseOfAlduin\\fx\\' + r['id'] + '_icon.nif')), sub('DNAM', struct.pack('<fI', 90.0, 0))]))  # the Magic menu shows the spell's logo
    data = bytearray(tsub('ligh', 'DATA')); c = int(r['color'], 16)
    struct.pack_into('<I', data, 4, 160); struct.pack_into('<BBBB', data, 8, c >> 16, (c >> 8) & 255, c & 255, 0)
    recs['LIGH'].append(record('LIGH', fid('LIGH', 'fx_' + r['id'] + '_light'), [sub('EDID', z('UN_SpellLight_' + r['id'])), sub('OBND', tsub('ligh', 'OBND')), sub('DATA', bytes(data)), sub('FNAM', struct.pack('<f', 1.0))]))
for n in ('iceshell', 'roots', 'burn', 'poison'):
    arto('fx_' + n, 'CurseOfAlduin\\fx\\status_' + n + '.nif', 1)                             # magic hit effect art
for w in S['weapons']['rows']:
    kind = w.get('kind', 'wand')
    if kind == 'wand':
        # enchantment: concentration, aimed, staff-type, free (infinite casts)
        # staff cast (point and release), aimed: Skyrim launches the wand's own element bolt from the tip
        if w.get('nativeEnch'):
            # (Noah: the Falmer staffs cast for real) Skyrim's own staff spell - its projectile, its staff casting animation -
            # with the vanilla staff's effects (Skyrim.esm MGEFs), free of charge; our code adds its numbers on the hit
            enit = struct.pack('<iIIiIIfII', 0, 0x1, 1, 3000, 2, 0x0C, 0.5, 0, 0)  # fire-and-forget, aimed, staff enchantment, free
            effs = []
            for mg, mag, area, dur in w['nativeEnch']:
                effs += [sub('EFID', struct.pack('<I', int(mg, 16))), sub('EFIT', struct.pack('<fII', float(mag), area, dur))]
            recs['ENCH'].append(record('ENCH', fid('ENCH', w['id']), [
                sub('EDID', z('UN_WandEnch_' + w['id'])), sub('OBND', bytes(12)), sub('FULL', z(w['label'])), sub('ENIT', enit)] + effs))
        else:
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
        if w['id'] in SCRAFT:  # the ten spells: their own glow on the hands, light and spell-menu orb (tools/spellfx.py)
            m = recs['MGEF'][-1]
            body = bytearray(m[24:])
            i = 0
            while i < len(body):
                name = bytes(body[i:i + 4]); ln = struct.unpack_from('<H', body, i + 4)[0]
                if name == b'DATA':
                    struct.pack_into('<I', body, i + 6 + 24, fid('LIGH', 'fx_' + w['id'] + '_light'))   # casting light
                    struct.pack_into('<I', body, i + 6 + 92, 0)     # no casting art: the code turns a ball of the spell in each hand (Spellcraft.inl)
                    struct.pack_into('<I', body, i + 6 + 108, 0)    # (nor dual-cast art)
                if name == b'MDOB':
                    struct.pack_into('<I', body, i + 6, fid('STAT', 'fx_' + w['id'] + '_orb'))          # the spell menu shows its logo
                i += 6 + ln
            recs['MGEF'][-1] = m[:24] + bytes(body)
        sp = bytearray(tsub('spel_firebolt', 'SPIT'))
        struct.pack_into('<IIIfIIffI', sp, 0, int(round(w.get('mana', 0) or 0)), 0x1, 0, 0.0, 1, 0, 0.0, 0.0, 0)  # its real magicka cost shown in the Magic menu (the code spends it; the engine's own charge is handed back), fire-and-forget (one push per click), self
        recs['SPEL'].append(record('SPEL', fid('SPEL', 'hand_' + w['id']), [
            sub('EDID', z('UN_Hand_' + w['id'])), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z(w['label'])),
            sub('MDOB', struct.pack('<I', fid('STAT', 'fx_' + w['id'] + '_orb')) if w['id'] in SCRAFT else tsub('spel_firebolt', 'MDOB')), sub('ETYP', tsub('spel_firebolt', 'ETYP')), sub('DESC', z('')),
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

# the Echo Altar's bows (sheets/bows.json): Skyrim's bow (its draw animation - and any bow animation mod), our model, an
# arrow on the string; the shot that does the work is ours (src/Bows.inl: the element's energy arrow, its trail and burst),
# the bow's own arrow (no damage) flies beside it so there is an arrow to see
# (Noah's download: Elemental Arrows, by its author) each bow's arrow: the quiver model on the string and at the hip, the
# flight model the one you see fly - it flies with our energy shot (same speed, no drop), harmless, so the arrow is seen
BOW_TIERS = {  # what each reforge adds (src/Bows.inl OnHit/Impact; every tier's impact is the last one, grown)
    'fire': 'Base: burns. I Flare Shot: bursts into a fireball. II Cinder Trail: the ground behind it burns. III Fiery Torrent: a full draw seeks the nearest of the dead and bursts into a torrent of fire.',
    'frost': 'Base: slows. I Deep Freeze: freezes solid. II Shatter: the frozen burst into shards. III Throat of the World: a full draw seeks the nearest of the dead and bursts into killing cold.',
    'shock': 'Base: leaps to one more. I Chain Lightning: leaps to three. II Storm Mark: the struck shock those round them. III Storm Ball: a full draw leaves a storm ball hanging where it strikes, zapping everything near it.',
    'wind': 'Base: knocks back. I Gale Burst: the dead round its impact are hurled back. II Cyclone Arrow: a whirlwind where it lands. III Arcane Cyclone: a full draw bursts into a cyclone that hurls every one of the dead round it far away.',
    'sun': 'Base: sears and staggers. I Radiant Burst: light bursts round it. II Solar Flare: its burst of light grows wider. III Sunfire Tornado: a tornado of sunfire hunts the dead.',
    'blood': 'Base: heals you a little. I Hemorrhage: they bleed. II Blood Tether: a tether feeds you. III Crimson Harvest: kills burst into orbs of blood - health and power.'}
NOCK = {'fire': 'flaming', 'frost': 'frost', 'shock': 'thunder', 'wind': 'swarm', 'sun': 'gust', 'blood': 'crimson'}
for bw in S['bows']['rows']:
    pd = bytearray(tsub('proj_arrow', 'DATA'))
    struct.pack_into('<f', pd, 4, 0.0)                 # no drop
    struct.pack_into('<f', pd, 8, 4800.0)              # speed: alongside our energy shot (src/Bows.inl Fire)
    struct.pack_into('<f', pd, 12, 9000.0)             # range
    struct.pack_into('<I', pd, 16, 0)                  # no light
    recs['PROJ'].append(record('PROJ', fid('PROJ', 'bowarrow_' + bw['id']), [
        sub('EDID', z('UN_BowArrowProj_' + bw['id'])), sub('OBND', tsub('proj_arrow', 'OBND')), sub('FULL', z(bw['label'] + ' Arrow')),
        sub('MODL', z('CurseOfAlduin\\arrows\\arrow' + NOCK[bw['element']] + 'flight.nif')), sub('DATA', bytes(pd)), sub('NAM1', b'\0'), sub('VNAM', tsub('proj_arrow', 'VNAM'))]))
    ad = struct.pack('<IIfIf', fid('PROJ', 'bowarrow_' + bw['id']), 0x4, 0.0, 0, 0.0)   # projectile, flags (an arrow, not a bolt), damage 0
    recs['AMMO'].append(record('AMMO', fid('AMMO', 'bowarrow_' + bw['id']), [
        sub('EDID', z('UN_BowArrow_' + bw['id'])), sub('OBND', tsub('ammo_iron', 'OBND')), sub('FULL', z(bw['label'] + ' Arrow')),
        sub('MODL', z('CurseOfAlduin\\arrows\\arrow' + NOCK[bw['element']] + 'quiver.nif')), sub('YNAM', tsub('ammo_iron', 'YNAM')), sub('ZNAM', tsub('ammo_iron', 'ZNAM')),
        sub('KSIZ', tsub('ammo_iron', 'KSIZ')), sub('KWDA', tsub('ammo_iron', 'KWDA')), sub('DATA', ad)]))
    subs = []
    for name, hx in T['weap_longbow']['subs']:
        b = bytes.fromhex(hx)
        if name == 'EDID': b = z('UN_Bow_' + bw['id'])
        elif name == 'FULL': b = z(bw['label'])
        elif name == 'MODL': b = z('CurseOfAlduin\\bow\\' + bw['id'] + '.nif')
        elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_bow_' + bw['id']))
        elif name == 'MODT': continue
        elif name == 'DESC': b = z(bw['text'] + ' ' + BOW_TIERS[bw['element']])  # (Noah: what the bow does at each tier)
        elif name == 'DATA': b = struct.pack('<IfH', 0, 1.0, 0)
        subs.append(sub(name, b))
    recs['WEAP'].append(record('WEAP', fid('WEAP', 'bow_' + bw['id']), subs))

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
    # category: UI sounds in Skyrim's UI category, everything else in SFX (so the effects slider governs it)
    gnam = tsub('sndr_ui', 'GNAM') if s.get('cat') == 'ui' else struct.pack('<I', 0x000172A1)
    lnam = bytearray(tsub(tpl, 'LNAM'))
    if s.get('loop'):
        lnam[1] = 0x08                                  # loop (charge hums, the ritual circle)
    # engine pitch shift (percent) for variety; random variance only when there are several takes
    bnam = struct.pack('<bBBBH', int(s.get('shift', 0)), 6 if len(files) > 1 else 3, 128, 0, int(float(s.get('atten', 0)) * 100))  # static attenuation in dB*100
    recs['SNDR'].append(record('SNDR', fid('SNDR', s['id']), [
        sub('EDID', z(s['editorId'])), sub('CNAM', tsub(tpl, 'CNAM')), sub('GNAM', gnam)] +
        [sub('ANAM', z(fl)) for fl in files] + [sub('ONAM', tsub(tpl, 'ONAM')), sub('LNAM', bytes(lnam)), sub('BNAM', bnam)]))

# music: Grimmvar's fight and a city freed play through Skyrim's own music system (music slider, ducking, no clash
# with the combat music: higher priority), from tracks cut out of the supplied library
for key, edid, track, flags, prio, fade in (('boss', 'UN_MUSBoss', 'Data\\Music\\CurseOfAlduin\\mus_boss.wav', 0x24, 1, 3.0),
                                           ('freed', 'UN_MUSFreed', 'Data\\Music\\CurseOfAlduin\\mus_freed.wav', 0x01, 1, 1.5),
                                           ('intro', 'UN_MUSIntro', 'Data\\Music\\CurseOfAlduin\\mus_intro.wav', 0x21, 1, 2.0)):
    recs['MUST'].append(record('MUST', fid('MUST', key), [sub('EDID', z(edid + 'Track')), sub('CNAM', struct.pack('<I', 0x6ED7E048)), sub('ANAM', z(track))]))
    recs['MUSC'].append(record('MUSC', fid('MUSC', key), [sub('EDID', z(edid)), sub('FNAM', struct.pack('<I', flags)),
        sub('PNAM', struct.pack('<HH', prio, 10000)), sub('WNAM', struct.pack('<f', fade)), sub('TNAM', struct.pack('<I', fid('MUST', key)))]))
# the undead step only with the mod's own footsteps (Audio.cpp plays them on each footfall): their skins carry an empty
# footstep set, so the race's own (vanilla) steps stay quiet
recs['FSTS'].append(record('FSTS', fid('FSTS', 'silent'), [sub('EDID', z('UN_SilentFootsteps')), sub('XCNT', struct.pack('<5I', 0, 0, 0, 0, 0)), sub('DATA', b'')]))
# loading screens of our own among Skyrim's: the shrine or the lore book turning, and a hint
LOADING_TIPS = [
    "Alduin shouted three words into the earth at Helgen - GRAH DIR LEIN, Battle, Death, World - and every barrow answered.",
    "An Echo Altar stands in every city. Call a spell, reforge the one in your hands or take a perk - the night does not wait while you bargain.",
    "Fen-Wailers call the horde to them with their scream. Kill them first.",
    "Sewn Thralls burst when they fall. Strike them from afar.",
    "Hold right mouse to gather a spell's full power in both hands. Let go too soon and the power gutters out.",
    "When the runic circle wakes round the altar, stand inside it until its count runs out. The dead come harder while you do.",
    "You carry five perks at most. Choose them well.",
    "Wake a city's Echo Altar and its dead sleep again. Its Thu'um waits for you there.",
    "A spell reforged three times is Ascendant: its great strike falls twice.",
    "Watch the dead for tokens: a skull, a coin, a falling star, a chest.",
    "The altars take no gold - only the septims the dead leave behind.",
    "Every spell the Echo Altar gives is the echo of a fallen mage's last. Carry it well.",
    "The violet fire in the altar is Alduin's own Thu'um, caught and turned back. The blue runes hold it there.",
    "Win Whiterun's altar and a dragon will answer your call: jump, then Activate in mid-air.",
]
for key, model in (('ls_shrine', 'CurseOfAlduin\\shrine\\shrine.nif'), ('ls_book', 'Clutter\\Books\\Book02LowPoly02.nif')):
    recs['STAT'].append(record('STAT', fid('STAT', key), [sub('EDID', z('UN_LoadScreen_' + key)), sub('OBND', struct.pack('<6h', -60, -60, 0, 60, 60, 200)),
        sub('MODL', z(model)), sub('DNAM', struct.pack('<fI', 90.0, 0))]))
for i, tip in enumerate(LOADING_TIPS):
    shrine = i % 2 == 0
    recs['LSCR'].append(record('LSCR', fid('LSCR', f'ls_{i}'), [sub('EDID', z(f'UN_LoadScreen{i:02d}')), sub('DESC', z(tip)),
        sub('NNAM', struct.pack('<I', fid('STAT', 'ls_shrine' if shrine else 'ls_book'))), sub('SNAM', struct.pack('<f', 0.55 if shrine else 2.2)),
        sub('RNAM', struct.pack('<3h', -10 if shrine else 20, 0, 0)), sub('ONAM', struct.pack('<2h', -180, 180)),
        sub('XNAM', struct.pack('<3f', 0.0, 0.0, -40.0 if shrine else 0.0))]))
# the undead speak only with the mod's own voice: a voice type no dialogue uses (Skyrim's draugr groans stay out)
recs['VTYP'].append(record('VTYP', fid('VTYP', 'silent'), [sub('EDID', z('UN_SilentVoice')), sub('DNAM', b'\0')]))

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
    struct.pack_into('<I', pd, 56, 0); struct.pack_into('<I', pd, 60, 0)   # no countdown / disable sounds
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
    elif name == 'MODL': b = z('CurseOfAlduin\\reaper\\scythe_hidden.nif')
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
        elif name == 'MODL': b = z('CurseOfAlduin\\reaper\\scythe_hidden.nif')
        elif name in ('MODT', 'DESC'): continue
        elif name == 'DNAM': b[0] = anim; struct.pack_into('<ff', b, 4, speed, reach)
        elif name == 'DATA': struct.pack_into('<H', b, 8, dmg)
        elif name in ('TNAM', 'NAM9', 'NAM8'): b = struct.pack('<I', 0)   # no Skyrim weapon sounds
        elif name == 'INAM': b = struct.pack('<I', 0)                     # no Skyrim impact set (its sounds)
        ws.append(sub(name, bytes(b)))
    ws.insert(ws.index(next(x for x in ws if x[:4] == b'WNAM')) + 1, sub('SNAM', struct.pack('<I', fid('SNDR', 'claw_swing'))))  # the swing: ours
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
        sub('DNAM', tsub('arma_skin', 'DNAM')), sub('MOD2', model), sub('MOD3', model), sub('SNDD', struct.pack('<I', fid('FSTS', 'silent')))]))
    # glowing eyes (tools/eyes.py): a second addon in slot FX01, skinned to the head
    eyes = z(f"CurseOfAlduin\\eyes\\{c['id']}_eyes.nif")
    ebodt = bytearray(tsub('arma_skin', 'BODT')); struct.pack_into('<I', ebodt, 0, 1 << 31)
    recs['ARMA'].append(record('ARMA', fid('ARMA', 'eyes_' + c['id']), [
        sub('EDID', z('UN_' + c['id'] + 'EyesAA')), sub('BODT', bytes(ebodt)), sub('RNAM', struct.pack('<I', race)),
        sub('DNAM', tsub('arma_skin', 'DNAM')), sub('MOD2', eyes), sub('MOD3', eyes)]))
    abodt = bytearray(bodt); struct.pack_into('<I', abodt, 0, struct.unpack_from('<I', abodt, 0)[0] | (1 << 31))
    recs['ARMO'].append(record('ARMO', fid('ARMO', c['id']), [
        sub('EDID', z('UN_Skin_' + c['id'])), sub('OBND', tsub('armo_skin', 'OBND')), sub('BODT', bytes(abodt)),
        sub('RNAM', struct.pack('<I', race)), sub('MODL', struct.pack('<I', fid('ARMA', c['id']))), sub('MODL', struct.pack('<I', fid('ARMA', 'eyes_' + c['id']))),
        sub('DATA', tsub('armo_skin', 'DATA')), sub('DNAM', tsub('armo_skin', 'DNAM'))]))
    src = 'npc_necro_tmpl' if human else 'npc_draugr'
    acbs = bytearray(tsub(src, 'ACBS'))
    struct.pack_into('<H', acbs, 8, int(c['level']))   # level
    struct.pack_into('<H', acbs, 18, 0)                # no template: everything comes from this record
    struct.pack_into('<I', acbs, 0, struct.unpack_from('<I', acbs, 0)[0] & ~0x1)  # male (DraugrRace has no female body)
    aidt = bytearray(tsub(src, 'AIDT')); aidt[0] = 2; aidt[1] = 4   # very aggressive, foolhardy
    subs = [sub('EDID', z('UN_' + c['id'])), sub('OBND', tsub(src, 'OBND')), sub('ACBS', bytes(acbs)),
            sub('VTCK', struct.pack('<I', fid('VTYP', 'silent'))), sub('RNAM', struct.pack('<I', race)), sub('WNAM', struct.pack('<I', fid('ARMO', c['id'])))]
    if False:  # (no Skyrim spells on our undead: their sounds are Skyrim's; the Voidbound cast our Void Hex instead) (frost, conjuration) and caster combat style
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

# The Mod Configuration menu's page (SkyUI + MCM Helper): a quest running from the start with our MCM script
# (CurseOfAlduin_MCM, a bare MCM_ConfigBase: tools/mcm.py) and SkyUI's reload watcher on the player. The page itself
# is Data/MCM/Config/CurseOfAlduin/config.json.
def wstr(t): b = t.encode('cp1252'); return struct.pack('<H', len(b)) + b
vmad = (struct.pack('<hhH', 5, 2, 1) + wstr('CurseOfAlduin_MCM') + b'\x01' + struct.pack('<H', 1) + wstr('ModName') + b'\x02\x01' + wstr('The Curse of Alduin') +
        b'\x02\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x08\x00\x01\x05\x00\x02\x00\x01\x00' + wstr('SKI_PlayerLoadGameAlias') + b'\x00\x00\x00')
recs['QUST'].append(record('QUST', fid('QUST', 'mcm'), [
    sub('EDID', z('CurseOfAlduin_MCM_Quest')), sub('VMAD', vmad), sub('FULL', z('The Curse of Alduin MCM')),
    sub('DNAM', bytes.fromhex('110100ff0000000000000000')), sub('NEXT', b''), sub('ANAM', struct.pack('<I', 1)),
    sub('ALST', struct.pack('<I', 0)), sub('ALID', z('PlayerAlias')), sub('FNAM', struct.pack('<I', 0)),
    sub('ALFR', struct.pack('<I', 0x14)), sub('VTCK', struct.pack('<I', 0)), sub('ALED', b'')]))

# Each city's Echo Altar in Skyrim's Quests journal: one quest per city (Dawnguard's crest). Stage 10 tells the altar's
# call; stage 20+i the story of trial i when it is taken up (any three, in any order); stage 100 the city's Barrow-Jarl; stage
# 200 when the city is freed. Objectives: 0 choose a trial at the altar, 1-5 the trials, 6 slay the Barrow-Jarl (src/Curse.cpp
# Journal shows and ticks them). One alias, the Target, which the game points at where the trial wants you: its compass and
# map marker. (Counts and seconds follow Skyrim's difficulty, so the log gives them in the trial's own HUD line instead.)
CU = S['curse']
def fill(t, c):
    return t.replace('{city}', c['town']).replace('{landmark}', c.get('landmark', ''))
def unnumbered(t):  # the journal can't know the difficulty: "{n} of the risen" -> "the risen", "{s} seconds" -> "the time the altar asks"
    return t.replace('Put {n} of the risen', 'Put the risen').replace('Slay {n} of the risen', 'Slay the risen').replace('{n} waves', 'Wave upon wave').replace('Clear {n} whole waves', 'Clear whole waves').replace('clear {n} waves', 'clear the waves') \
            .replace('before {s} seconds pass', 'before the altar\'s time runs out').replace('Hold the circle for {s} seconds.', 'Hold the circle until the runes blaze.')
for c in CU['cities']:
    boss = CU['bosses'][c['town']]
    subs = [sub('EDID', z('UN_Rites_' + c['town'])), sub('FULL', z('The Echo Altar: ' + c['town'])),
            sub('DNAM', struct.pack('<HBBII', 0x0000, 60, 0xFF, 0, 10)), sub('NEXT', b'')]
    stages = [(10, f"The Echo Altar of {c['town']} has woken, but only half: Alduin's echo still holds the city. The altar offers five trials. "
                   f"Any three will do, in any order - and when three are done, the Barrow-Jarl who silenced the altar, {boss['name']}, will rise to stop me.")]
    stages += [(20 + i, f"{tr['title']}: " + fill(unnumbered(tr['text']), c)) for i, tr in enumerate(CU['trials'])]
    stages += [(100, f"{boss['name']}: {boss['text']} It walks the night near the Echo Altar. Strike it down and the altar wakes.")]
    for st, text in stages:
        assert '{' not in text, text
        subs += [sub('INDX', struct.pack('<HBB', st, 0, 0)), sub('QSDT', b'\x00'), sub('CNAM', z(text))]
    subs += [sub('INDX', struct.pack('<HBB', 200, 0, 0)), sub('QSDT', b'\x01'), sub('CNAM', z(c['town'] + ' is free. ' + CU['liftedText'].replace('{city}', c['town'])))]
    objectives = [f"Choose a trial at the Echo Altar of {c['town']}"] + [fill(tr['goal'], c) for tr in CU['trials']] + [f"Slay {boss['name']}"]
    for i, text in enumerate(objectives):
        subs += [sub('QOBJ', struct.pack('<H', i)), sub('FNAM', struct.pack('<I', 0)), sub('NNAM', z(text)),
                 sub('QSTA', struct.pack('<iI', 0, 1))]
    subs += [sub('ANAM', struct.pack('<I', 1)), sub('ALST', struct.pack('<I', 0)), sub('ALID', z('Target')),
             sub('FNAM', struct.pack('<I', 0x2 | 0x10)), sub('VTCK', struct.pack('<I', 0)), sub('ALED', b'')]
    recs['QUST'].append(record('QUST', fid('QUST', 'rites_' + c['town']), subs))

# the night's undead: one faction, allies to each other (no infighting), enemies of the player
recs['FACT'].append(record('FACT', fid('FACT', 'undead'), [
    sub('EDID', z('UN_UndeadFaction')), sub('FULL', z('Undying')),
    sub('XNAM', struct.pack('<IiI', fid('FACT', 'undead'), 0, 2)),     # self: ally
    sub('XNAM', struct.pack('<IiI', 0x00000DB1, -100, 1)),             # PlayerFaction: enemy
    sub('DATA', struct.pack('<I', 0))]))

# the Curse of Alduin: each city's progress (step*1000 + progress) and whether the prologue was read; saved with the game
for key, edid in [('curse_' + c['town'], 'UN_Curse_' + c['town']) for c in S['curse']['cities']] + [('curse_intro', 'UN_CurseIntro'), ('stand_layout', 'UN_StandLayout')]:
    recs['GLOB'].append(record('GLOB', fid('GLOB', key), [sub('EDID', z(edid)), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))

# The Curse of Alduin: the lore book, and A Survivor's Guide (both in the pack when a character is made)
def lore_text():
    img = lambda n, w, h: f"<p align='center'><img src='img://Textures/CurseOfAlduin/Book/{n}.png' width='{w}' height='{h}'></p>"
    cu = S['curse']
    out = [f"<font face='$HandwrittenFont'>", img('sigil', 200, 200),
           "<p align='center'><font size='30'>THE CURSE OF ALDUIN</font></p>",
           "<p align='center'>Set down at High Hrothgar by a Greybeard who has broken his silence, for whoever lived through Helgen</p>",
           "[pagebreak]", "<p align='center'><font size='24'>I. Three Words</font></p>", img('helgen', 300, 176),
           f"<p>{cu['prologue']}</p>",
           "<p>We heard it on the mountain. Not a shout to break stone, but a Word sung slow, that sank into the earth and stayed there: "
           "<b>Grah</b>, battle. <b>Dir</b>, death. <b>Lein</b>, the world. By day it sleeps in the ground. When the sun goes down it wakes, "
           "and every barrow, every crypt and every churchyard in the holds remembers that it is owed a war.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>II. The Dead Who Answer</font></p>", img('graves', 300, 176),
           "<p>From dusk until first light the dead climb out of the earth, more each night and harder to kill. At dawn they fall back to dust. "
           "The Nords of old had names for them, and the names are true again:</p>",
           "<p><b>Haugbui</b>, the mound-dwellers - slow, many, never tired. <b>Pyre-Thralls</b>, the burned dead, still smouldering. "
           "<b>Gravegnawers</b>, quick and starved. <b>Barrow Archers</b> on the walls.</p>",
           "<p>The <b>Tongues</b> - Ash, Rime and Storm - were shouters once; their dead mouths still hold a word or two. "
           "<b>Fen-Wailers</b> scream, and the horde comes to the scream: kill them first. <b>Sewn Thralls</b> burst when they fall. "
           "<b>Grave-Hounds</b> run in packs, and the <b>Bone Jotunn</b> is the skeleton of a giant, walking.</p>",
           "<p>Over them all ride the great ones: the <b>Thanes of the Mound</b>, the <b>Mother of Mounds</b>, <b>Haugrisi the Mound-Giant</b>, "
           "<b>the Elk-Crowned</b>, and <b>Grahzah</b>, Alduin's Grave-Wing, a dragon of bone. Look at a wounded thing and a pale bar shows how much life it has left; "
           "the great ones' bars burn gold and bear their names.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>III. The Echo Altars</font></p>",
           "<p>This is not the first time Alduin has shouted the dead awake. In the Dragon War he did it once over the barrows of the Atmorans, and the Tongues - "
           "the first men to learn the Voice - answered him. They raised an altar in every hold where the dead marched: a star of violet fire caught in a ring of "
           "floating stones, bound with runes of blue, standing on a floor of black stone inlaid with gold.</p>",
           "<p>The fire is Alduin's own Thu'um, caught and turned back on itself. The runes are the Tongues' answer, holding it there. Every mage who fell in that war "
           "gave their last spell to the star, and the star kept it: that is why the altars have so many to give, and why the holds call them <b>Echo Altars</b>.</p>",
           "<p>When Alduin was cast out of time the altars went dark and sank into the earth, and towns grew over them. At Helgen he returned, and the altars woke - "
           "but only half. Each one has been silenced by a dead jarl of old, raised to guard it against the living: a <b>Barrow-Jarl</b>.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>IV. Waking an Altar</font></p>", img('wards', 300, 176),
           "<p>The altars answer to you. Perhaps it is because you heard the three Words at Helgen and lived; perhaps it is something older. "
           "Stand before one and it will set you <b>trials</b> - hold its runic circle while the dead come for you, cull the risen in its streets - "
           "and when they are done its Barrow-Jarl rises to stop you. Strike him down and the altar rings with the Tongues' answer. Its city becomes a sanctuary: "
           "no dead rise within its walls, and the altar gives you the city's own <b>Thu'um</b>.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>V. The Voices of the Holds</font></p>"]
    for h in S['shouts']['rows']:
        if h.get('town'):
            out.append(f"<p><b>{h['town']}</b>: <i>{h['label']}</i> ({' '.join(h['words'])}, '{' '.join(h['translations'])}'). {h['text']}.</p>")
    out += ["<p><b>Whiterun</b>: <i>Dragon's Updraft</i>. A whirlwind lifts you high - and with it, a dragon that will carry you.</p>",
            "[pagebreak]", f"<p>{cu['freedAll']}</p>", "<p align='right'><i>- written in the silence of High Hrothgar</i></p>", "</font>"]
    return '\n'.join(out)

def guide_text():
    p = lambda t: f"<p>{t}</p>"
    h = lambda t: f"<p align='center'><font size='24'>{t}</font></p>"
    out = ["<font face='$HandwrittenFont'>", "<p align='center'><font size='28'>A SURVIVOR'S GUIDE TO THE LONG NIGHT</font></p>",
           "<p align='center'>Notes left at the Sleeping Giant Inn by one who made it this far. Read them before dusk.</p>",
           "[pagebreak]", h("Day and Night"),
           p("A day lasts about fifteen minutes. By day the dead sleep: rest, explore, spend what you earned. "
             "At dusk they rise in <b>waves</b>, clawing up out of the ground at the same places every night. Each wave brings twenty more than the last: twenty, forty, sixty... "
             "The first night has five waves; every night adds one more (never more than 2,000 dead a night for each player). The day lasts about 18 minutes. The night does not end until the last wave is down - then dawn breaks."),
           p("You cannot sleep or wait while the dead walk."),
           "[pagebreak]", h("Septims"),
           p("Every risen thing you kill leaves septims. A headshot leaves more, and a kill with a blade or a spell more still. "
             "Septims are the only coin the Echo Altars take."),
           p("Watch the dead for tokens: a skull (Insta-Kill), a coin (Double Points), a falling star (it destroys every corpse risen tonight) and a chest (a Fire Sale at the altar)."),
           "[pagebreak]", h("The Echo Altar"),
           p("Every city has one. Look into its star and press Activate. Its menu does not stop the night. Move through it with the D-pad (or W and S) and take a row with A (or Enter); B leaves."),
           p("<b>Call a spell</b> - the altar turns and gives you one of the spells of the fallen. Each call costs more than the last until dawn. You carry two of its spells; a third asks which one it replaces."),
           p("<b>Call a bow</b> - the altar's second wheel. Six elemental bows: fire, frost, storm, wind, sun and blood. You carry one; a new one takes its place."),
           p("<b>Reforge</b> - make the spell or bow in your hands stronger, three times over. Every tier gives a bow a new attack: the fire bow's arrows burst, then leave burning ground, then a full draw calls down a volley of meteors."),
           p("<b>Perks</b> - lasting gifts. You carry five at most."),
           p("<b>Trials</b> - the quest that wakes the altar. It offers five; take up any three, in any order (step over to them with D-pad left or right, or A and D). "
             "When three are done the city's Barrow-Jarl rises at night near the altar. Slay it and the city is safe for good. Skyrim's difficulty sets how hard the trials are."),
           "[pagebreak]", h("The Five Trials"),
           p("<b>Ward of the Tongues</b> - stand in the runic circle round the altar while the dead come faster. Step out and the count waits for you."),
           p("<b>The Culling</b> - kill a number of the risen in the city."),
           p("<b>The Unbroken</b> - clear whole waves without falling below half health. Fall below and that wave does not count."),
           p("<b>Trial of Steel</b> - only kills with a melee weapon or your fists count."),
           p("<b>The Long Night's Breath</b> - stay by the altar while the night's waves are cleared."),
           "[pagebreak]", h("Spells"),
           p("Tap the attack to throw a bolt. Hold it, and both hands gather the spell's full power: the longer you hold, the harder it strikes. "
             "Let go too soon and the power gutters out. Magicka comes back fast."),
           "[pagebreak]", h("Shouts"),
           p("A woken altar teaches you its city's Thu'um. Equip it like any shout. Its cooldown shows under your spells, bottom right."),
           "[pagebreak]", h("The Dragon"),
           p("Wake Whiterun's altar and a dragon will answer you. Jump and press Activate in mid-air: it swoops down and you take hold of its harness."),
           p("Steer with the stick or WASD. Forward dives, back climbs. Attack to make it breathe fire on the dead below - only the dead, never your friends. "
             "At the altar you can make deeper bonds with it."),
           "[pagebreak]", h("Echo Stones and the Reliquary"),
           p("When a city's altar is freed and its night won, its heart stone turns to blue light. Step onto the altar and it gives you a page: <i>The Broken Words</i>, one for each city. "
             "Each page says plainly where a part of a <b>wonder weapon</b> waits and how to take it, and your compass marks the place."),
           p("Some parts fall from the Barrow-Jarls. Some wait in reliquaries at old barrows, guarded, and only wake at night. Some must be earned by killing the dead near a barrow's door."),
           p("Take them to <b>High Hrothgar</b>: a sealed door in the great hall leads down to the Reliquary, where each wonder is forged on its pedestal once its three parts are found."),
           "[pagebreak]", h("The Wonder Weapons"),
           p("A forged wonder is taken up from its pedestal and cast from your hands like the altar's spells: tap to strike, hold both hands to unleash it. One at a time - return it to take another."),
           p("<b>Gravecaller</b> - grave-wisps hunt the dead and feed you their life. Unleashed: three bone warriors claw out of the earth to fight for you."),
           p("<b>Frostbinder Gauntlets</b> - ice binds what it strikes; strike a bound foe and it shatters, cutting those round it. Unleashed: everything near you freezes, then bursts."),
           p("<b>Wolf Queen's Crown</b> - spirit-wolves run down the dead in pairs. Unleashed: the Queen howls and her spectral pack runs at your side."),
           p("<b>Horn of the Barrow-Jarls</b> - a war-cry hurls the dead back. Unleashed: the old war-host charges down the line in front of you."),
           "[pagebreak]", h("The Three Words"),
           p("When all four wonders are forged, Alduin's three words wake one after another: <b>GRAH</b> at the Word Wall of Grah in the Reach, <b>DIR</b> in the courtyard of Labyrinthian, "
             "<b>LEIN</b> on the summit of the Throat of the World. Your compass marks the word that is awake."),
           p("Go there at night. Its dead rise round you in waves; when the last wave falls, the word's bearer rises. Kill it and the word is silenced for good, and its power stays with you: "
             "<b>GRAH</b> - everything you wield hits 15% harder; <b>DIR</b> - the night's blows hurt you 20% less; <b>LEIN</b> - your magicka returns half again as fast."),
           p("Leave the place, or let dawn come, and the word falls quiet until you return. Silence all three and the curse of Alduin is broken."),
           "[pagebreak]", h("The Hall of the Awakened"),
           p("With the curse broken, the Reliquary under High Hrothgar becomes the <b>Hall of the Awakened</b>. Its stations:"),
           p("<b>Alduin's Bane</b> - the story's last weapon, a greatsword forged from the World-Eater's defeat: dragonfire on every blow, a crescent of golden fire on a power attack, and when its soul meter fills, Alduin's shade rains meteors round you. While you hold it, kills heal you and your shouts return twice as fast. "
             "<b>Robes of the First Dragonborn</b> - wear them and spells and wonders cost a quarter less magicka, and it returns faster. "
             "<b>The Bone-Wyrm's Skull</b> - your glider's dragon wears Alduin's bones (touch it again to change back)."),
           p("<b>Brazier of the Endless Night</b> - while it burns, the night never ends: wave after wave, until you put it out. "
             "<b>Seat of the Awakened</b> - begin again: every city falls back under the curse and the nights start over, the dead half again as tough each cycle. "
             "Your wonders, bows, powers and the words' gifts stay yours."),
           "[pagebreak]", h("Whispers of the Altars"),
           p("Every freed altar keeps a secret. Nobody will tell you what it is. Stand at a freed altar and its <b>whispers</b> show in its menu - another line for each step you find. These are the first:"),
           ] + [p(f"<b>{e['town']}</b> - {e['hints'][0]}") for e in S['eggs']['rows']] + [
           "[pagebreak]", h("Event Nights and Themed Waves"),
           p("From night 8, about one night in eight is an <b>event night</b>: a Goblin Raid, a Harpy Swarm, Armored Champions or a Blood Fog. The whole night belongs to it - and it pays <b>double septims</b>, with the dead coming twice as fast."),
           p("On ordinary nights, from the third wave, about one wave in six is <b>themed</b> the same way. Its name shows on the wave dial under the compass."),
           p("About one night in five, halfway through, <b>something stirs in the dark</b>: a mini-boss. It always drops a wonder weapon part - the next one you're missing - for everyone in the party."),
           "[pagebreak]", h("Falling"),
           p("At every dawn, and whenever an altar wakes, the day is saved. If you fall, you wake at that dawn. In a party, stand over a fallen friend and hold Activate to lift them."),
           p("Good luck. Stay out of the barrows after dark."), "</font>"]
    return '\n'.join(out)

for key, edid, name, text, model in (('dirge', 'UN_UndyingDirgeBook', 'The Curse of Alduin', lore_text(), 'Clutter\\Books\\Book02LowPoly02.nif'),
                                     ('guide', 'UN_SurvivorsGuide', "A Survivor's Guide to the Long Night", guide_text(), 'Clutter\\Books\\Book02LowPoly02.nif')):
    recs.setdefault('BOOK', []).append(record('BOOK', fid('BOOK', key), [
        sub('EDID', z(edid)), sub('OBND', bytes.fromhex('f5fff1fffdff0b000f000300')), sub('FULL', z(name)),
        sub('MODL', z(model)), sub('DESC', z(text)),
        sub('KSIZ', struct.pack('<I', 1)), sub('KWDA', struct.pack('<I', 0x000937A2)),
        sub('DATA', struct.pack('<BBHIIf', 0, 0, 0, 0, 25, 1.0)), sub('INAM', struct.pack('<I', 0x0008ADAE)), sub('CNAM', struct.pack('<I', 0))]))

# ---- Stage 2 (sheet: echo, src/Echo.cpp): the Echo Stones' pages, the hunt's globals, the Reliquary under High Hrothgar
E = S['echo']
WON = {w['id']: w for w in E['wonders']}
for key in ['echo_pages', 'echo_parts', 'echo_forged', 'echo_held', 'echo_words'] + ['echo_kill_' + q['id'] for q in E['parts'] if q['how'] == 'kill']:
    recs['GLOB'].append(record('GLOB', fid('GLOB', key), [sub('EDID', z('UN_' + ''.join(w.capitalize() for w in key.split('_')))), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))

# ---- (Noah) the hidden quests (sheet: eggs, src/Eggs.cpp): a global per quest (its step and progress), one for the gifts
# won, and a lesser power for each summon (and Winterhold's beam) - the power does nothing itself: Eggs::OnSpellCast does
for e in S['eggs']['rows']:
    recs['GLOB'].append(record('GLOB', fid('GLOB', 'egg_' + e['id']), [sub('EDID', z('UN_Egg_' + e['id'])), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))
    if e['reward'] in ('summon', 'spell'):
        label = e['rewardText'].split(':')[0]
        sp = struct.pack('<IIIfIIffI', 0, 0x1, 3, 0.0, 1, 0, 0.0, 0.0, 0)   # free, a lesser power, fire-and-forget, self
        recs['SPEL'].append(record('SPEL', fid('SPEL', 'egg_power_' + e['id']), [
            sub('EDID', z('UN_EggPower_' + e['id'])), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z(label)),
            sub('ETYP', struct.pack('<I', 0x025BEE)), sub('DESC', z(e['rewardText'])), sub('SPIT', sp),
            sub('EFID', struct.pack('<I', fid('MGEF', 'shoutcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
recs['GLOB'].append(record('GLOB', fid('GLOB', 'egg_gifts'), [sub('EDID', z('UN_EggGifts')), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))
for k, e in (('ritual_souls', 'UN_RitualSouls'), ('ritual_stand', 'UN_RitualStand')):   # (the staffs' last step: souls fed to each, the one on its stand)
    recs['GLOB'].append(record('GLOB', fid('GLOB', k), [sub('EDID', z(e)), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))
recs['GLOB'].append(record('GLOB', fid('GLOB', 'test_kit'), [sub('EDID', z('UN_TestKit')), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))  # (Noah's test characters)

def how_text(q):
    w = WON[q['wonder']]['label']
    if q['how'] == 'boss':
        return f"<b>{q['label']}</b> (for {w}): it falls from {q['town']}'s Barrow-Jarl when the altar there is freed."
    if q['how'] == 'relic':
        return (f"<b>{q['label']}</b> (for {w}): a reliquary burning with blue Tongue-fire waits at <b>{q['site']}</b>. It wakes only at night: "
                f"come near after dusk and its guardians rise. Kill them all, then walk up to the reliquary and the part is yours.")
    return (f"<b>{q['label']}</b> (for {w}): at <b>{q['site']}</b>, at night, kill the dead that come for you near its door. "
            f"A counter shows how many more it needs; when it is full, the part is yours.")

def page_text(town):
    pg = E['pages'][town]
    parts = [q for q in E['parts'] if q['page'] == town]
    out = ["<font face='$HandwrittenFont'>", f"<p align='center'><font size='26'>THE BROKEN WORDS</font></p><p align='center'><font size='22'>{town}</font></p>",
           f"<p><i>{pg['echo']}</i></p>", "<p align='center'>~</p>", f"<p><b>Where:</b> {pg['where']}</p>"]
    out += [f"<p>{how_text(q)}</p>" for q in parts]
    out += ["[pagebreak]", "<p align='center'><font size='22'>The Reliquary</font></p>",
            "<p>Your compass now marks every part this page speaks of - follow the blue mark with the part's name over it.</p>",
            "<p>Take the parts to <b>High Hrothgar</b>. In the great hall, by the entrance, a <b>sealed door</b> carved with the Tongues' sigil "
            "now opens for you. Behind it is the Reliquary: four pedestals, one for each wonder weapon. Each part you find lights a crystal on its pedestal; "
            "when all three burn, stand at the pedestal and press Activate to forge the weapon. Activate it again to take the wonder up; "
            "only one answers to you at a time, and in a party each wonder has one wielder - give it back to its pedestal to take another.</p>",
            "<p>Four wonders, three parts each: " + '; '.join(f"<b>{w['label']}</b> - " + ', '.join(q['label'] for q in E['parts'] if q['wonder'] == w['id']) for w in E['wonders']) + ".</p>",
            "<p align='right'><i>- an echo of the Tongues</i></p>", "</font>"]
    return '\n'.join(out)

for c in S['curse']['cities']:
    t = c['town']
    recs.setdefault('BOOK', []).append(record('BOOK', fid('BOOK', 'page_' + t), [
        # (Noah: the game crashed opening a page) a two-page note crashes the note menu: the pages are small books now,
        # read like the guide (book type, its inventory art)
        sub('EDID', z('UN_EchoPage_' + t)), sub('OBND', bytes.fromhex('f5fff1fffdff0b000f000300')), sub('FULL', z('The Broken Words: ' + t)),
        sub('MODL', z('Clutter\\Books\\Book02LowPoly02.nif')), sub('DESC', z(page_text(t))),
        sub('KSIZ', struct.pack('<I', 1)), sub('KWDA', struct.pack('<I', 0x000937A2)),
        sub('DATA', struct.pack('<BBHIIf', 0, 0, 0, 0, 0, 0.5)), sub('INAM', struct.pack('<I', 0x0008ADAE)), sub('CNAM', struct.pack('<I', 0))]))

RQ = 'CurseOfAlduin\\reliquary\\'
def rel_acti(key, name, verb, model, r):
    recs['ACTI'].append(record('ACTI', fid('ACTI', key), [sub('EDID', z('UN_' + key)), sub('OBND', struct.pack('<6h', -r, -r // 3, 0, r, r // 3, int(r * 1.4))),
        sub('FULL', z(name)), sub('MODL', z(RQ + model)), sub('RNAM', z(verb)), sub('FNAM', b'\0\0')]))
rel_acti('rel_door_in', 'Sealed Door of the Tongues', 'Enter', 'door.nif', 190)
rel_acti('rel_door_out', 'Sealed Door of the Tongues', 'Leave', 'door.nif', 190)
for w in E['wonders']:
    rel_acti('rel_ped_' + w['id'], w['label'] + "'s Pedestal", 'Forge', 'pedestal.nif', 60)
for key, model, r in [('rel_room', 'room.nif', 640), ('rel_relic', 'relic.nif', 80), ('rel_brazier', 'tonguefire.nif', 60), ('rel_banner', 'banner.nif', 120)] + [('rel_gem_' + w['id'], f"gem_{w['id']}.nif", 8) for w in E['wonders']]:
    recs['STAT'].append(record('STAT', fid('STAT', key), [sub('EDID', z('UN_' + key)), sub('OBND', struct.pack('<6h', -r, -r, 0, r, r, r)),
        sub('MODL', z(RQ + model)), sub('DNAM', struct.pack('<fI', 90.0, 0))]))
# Stage 5 (src/Echo.cpp Hall): the Hall of the Awakened's stations, and Alduin's Fang
for key, name, verb in (('hall_fang', "Alduin's Bane", 'Take'), ('hall_robes', 'Robes of the First Dragonborn', 'Take'),
                        ('hall_skull', "The Bone-Wyrm's Skull", 'Bind'), ('hall_endless', 'Brazier of the Endless Night', 'Kindle'),
                        ('hall_throne', 'Seat of the Awakened', 'Begin again')):
    rel_acti(key, name, verb, 'pedestal.nif', 60)
for key, model, r in (('hall_fang_show', 'CurseOfAlduin\\wonder\\bane.nif', 70), ('1st_fang', 'CurseOfAlduin\\wonder\\bane.nif', 70)):
    recs['STAT'].append(record('STAT', fid('STAT', key), [sub('EDID', z('UN_' + key)), sub('OBND', struct.pack('<6h', -r, -r, 0, r, r, r)),
        sub('MODL', z(model)), sub('DNAM', struct.pack('<fI', 90.0, 0))]))
for key, model in (('summon', 'emptysummonfx_instant.nif'), ('crystal', 'diamondwallspawnexp.nif'), ('orb', 'lightningrail_orb.nif'),
                   ('storm', 'elementalmasterystormhazard.nif'), ('tornado', 'unused\\tornado.nif')):
    recs['ACTI'].append(record('ACTI', fid('ACTI', 'emm_' + key), [sub('EDID', z('UN_emm_' + key)), sub('OBND', struct.pack('<6h', -120, -120, 0, 120, 120, 300)),
        sub('MODL', z('kittyspellpack02\\' + model)), sub('FNAM', b'\0\0')]))
for key in ('frost', 'shock', 'wind', 'sun', 'blood'):   # the bows' tornadoes: Elemental Mastery Magic's, recoloured (tools/modelconv/fxrepal)
    recs['ACTI'].append(record('ACTI', fid('ACTI', 'tornado_' + key), [sub('EDID', z('UN_tornado_' + key)), sub('OBND', struct.pack('<6h', -120, -120, 0, 120, 120, 300)),
        sub('MODL', z('CurseOfAlduin\\fx\\tornado_' + key + '.nif')), sub('FNAM', b'\0\0')]))
ws = []
for name, hx in T['weap_draugr_gs']['subs']:
    b = bytearray(bytes.fromhex(hx))
    if name == 'EDID': b = z('UN_AlduinsFang')
    elif name == 'FULL': b = z("Alduin's Bane")
    elif name == 'MODL': b = z('CurseOfAlduin\\wonder\\bane.nif')
    elif name == 'MODT': continue
    elif name == 'DESC': b = z("Forged from the World-Eater's own defeat. Every blow burns with dragonfire and its kills fill it with souls; a power attack looses a crescent of golden fire. Full, it calls Alduin's shade to rain meteors round you. While you hold it, kills heal you and your shouts return twice as fast.")
    elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_fang'))
    elif name == 'DATA': b = bytearray(struct.pack('<IfH', 5000, 16.0, 38))
    ws.append(sub(name, bytes(b)))
recs['WEAP'].append(record('WEAP', fid('WEAP', 'fang'), ws))
for key, col, radius in (('rel_blue', '5AA8FF', 620), ('rel_warm', 'FFD9A0', 1100), ('rel_relic', '6AB8FF', 700)):
    data = bytearray(tsub('ligh', 'DATA')); c = int(col, 16)
    struct.pack_into('<I', data, 4, radius); struct.pack_into('<BBBB', data, 8, c >> 16, (c >> 8) & 255, c & 255, 0)
    recs['LIGH'].append(record('LIGH', fid('LIGH', key), [sub('EDID', z('UN_' + key)), sub('OBND', tsub('ligh', 'OBND')), sub('DATA', bytes(data)), sub('FNAM', struct.pack('<f', 1.0))]))

# (spell blasts are cloned from Skyrim's explosions at runtime: EXPL records in an ESL crashed the game at load)
count = sum(len(v) for v in recs.values())
hdr = [sub('HEDR', struct.pack('<fiI', 1.71, count, NEXT)), sub('CNAM', z('Noah Grove')), sub('SNAM', z('The Curse of Alduin: round-based undead survival for Skyrim'))]
for m in MASTERS: hdr += [sub('MAST', z(m)), sub('DATA', struct.pack('<Q', 0))]
out = record('TES4', 0, hdr, flags=0x200)  # ESL flag
for k in ['GLOB', 'FACT', 'VTYP', 'FSTS', 'LSCR', 'MUSC', 'MUST', 'ARTO', 'MGEF', 'BOOK', 'ENCH', 'SPEL', 'WOOP', 'SHOU', 'ACTI', 'ARMO', 'LIGH', 'STAT', 'WEAP', 'AMMO', 'NPC_', 'ALCH', 'PROJ', 'ARMA', 'SNDR', 'QUST']: out += group(k, recs[k])  # Bethesda's group order: effects load before the spells that use them
(ROOT / 'data').mkdir(exist_ok=True)
(ROOT / 'data' / PLUGIN).write_bytes(out)
print('wrote', PLUGIN, len(out), 'bytes,', count, 'records')

# ---- (Noah: the lore and the guide read in a window of their own, not a book) src/Codex.h: both books as chapters
# (title, picture, paragraphs with <b>/<i> kept) for the Codex (src/Hud.cpp DrawCodex), shown when either book is read
import re as _re
def codex_chapters(html, first, last):
    pages = html.replace('</font>', '').split('[pagebreak]')
    out = []
    for n, pg in enumerate(pages):
        title, image, paras = None, '', []
        for m in _re.finditer(r"<p[^>]*>(.*?)</p>", pg, _re.S):
            body = m.group(1)
            im = _re.search(r"img://Textures/CurseOfAlduin/Book/(\w+)\.png", body)
            if im:
                image = image or im.group(1)
                continue
            ft = _re.match(r"\s*<font size='(\d+)'>(.*)$", body, _re.S)
            if ft and title is None and 'align=' in m.group(0):
                title = _re.sub(r'<[^>]+>', '', ft.group(2)).strip()
                continue
            body = _re.sub(r"<font[^>]*>", '', body).strip()
            if body:
                paras.append(body)
        if not paras and not image:
            continue
        if title is None or title.isupper():  # (the title page: the book's own name is the window's tab)
            title = first if n == 0 else last
        out.append((title, image, paras))
    return out
def _cs(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', ' ') + '"'
_cx = ['// generated by tools/gen_esp.py from the lore book and the guide - do not edit', '#pragma once', '#include <array>', '#include <string_view>', '#include <vector>',
       'namespace bo3z::codex', '{', '\tstruct Chapter { int book; std::string_view title, image; std::vector<std::string_view> paras; };',
       '\tinline constexpr std::array<std::string_view, 2> kBooks{ "The Curse of Alduin", "A Survivor\'s Guide" };',
       '\tinline const std::vector<Chapter> kChapters{']
for b, (html, first, last) in enumerate([(lore_text(), 'The Greybeard\'s Account', 'Epilogue'), (guide_text(), 'Before Dusk', 'Good Luck')]):
    for title, image, paras in codex_chapters(html, first, last):
        _cx.append('\t\t{ %d, %s, %s, { %s } },' % (b, _cs(title), _cs(image), ', '.join(_cs(p) for p in paras)))
_cx += ['\t};', '}', '']
(ROOT / 'src' / 'Codex.h').write_text('\n'.join(_cx), encoding='utf-8')
print('wrote src/Codex.h')
