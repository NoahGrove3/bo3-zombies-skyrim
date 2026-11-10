"""Write data/CurseOfAlduin.esp (ESL-flagged) from the sheets and the template records in tools/templates.
Records: Perk-a-Cola magic effects (MGEF) and potions (ALCH), gun items (WEAP, stand-in crossbow models),
and activators (ACTI) for the Mystery Box, perk machines and wall-buys."""
import json, struct, pathlib
from esp_ids import ROOT, sheets, assign, PLUGIN, MASTERS, dressing, blasts
S = sheets(); IDS, NEXT = assign(S, write=True)
SELF = len(MASTERS) << 24
T = {n: json.load(open(ROOT / 'tools/templates' / (n + '.json'))) for n in ['npc_necro_tmpl', 'weap_draugr_gs', 'mgef', 'alch', 'weap_crossbow', 'weap_dwarven', 'acti', 'sndr_ui', 'sndr_3d', 'ligh', 'weap_staff', 'arma_skin', 'armo_skin', 'npc_draugr', 'proj_bullet', 'spel_firebolt', 'mgef_firebolt', 'weap_longbow', 'ammo_iron', 'proj_arrow', 'weap_daedricdagger', 'efsh_enchfire', 'mgef_enchfire', 'ench_fire05', 'weap_madness', 'stat_madness1st', 'efsh_enchpurple', 'npc_mihailzombie', 'weap_daedricmace']}
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
recs = {'EFSH': [], 'WOOP': [], 'SHOU': [], 'GLOB': [], 'FACT': [], 'AMMO': [], 'PROJ': [], 'ENCH': [], 'SPEL': [], 'ARMA': [], 'ARMO': [], 'NPC_': [], 'STAT': [], 'LIGH': [], 'SNDR': [], 'MGEF': [], 'ACTI': [], 'WEAP': [], 'ALCH': [], 'MUSC': [], 'MUST': [], 'VTYP': [], 'FSTS': [], 'LSCR': [], 'ARTO': [], 'QUST': [], 'PERK': []}

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
    radius = {'box': 50, 'perk': 40, 'wallbuy': 40, 'ammo': 40, 'perkmenu': 50, 'forge': 90, 'relic': 50, 'tablet': 70}[o['kind']]
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

def cast_mgef(key, label, cast=2, aimed=False):
    md = bytearray(tsub('mgef_firebolt', 'DATA'))
    struct.pack_into('<I', md, 0, 0x200 | 0x400 | 0x800)  # no magnitude/duration/area; not hostile (no crime, no aggro)
    struct.pack_into('<f', md, 4, 0.0)                 # base cost
    struct.pack_into('<I', md, 24, 0); struct.pack_into('<I', md, 32, 0); struct.pack_into('<I', md, 36, 0)
    struct.pack_into('<I', md, 64, 1); struct.pack_into('<i', md, 68, -1)   # script archetype: does nothing itself
    struct.pack_into('<I', md, 72, 0)                  # no projectile: the cast is only the animation (our bolts fly)
    struct.pack_into('<I', md, 76, 0)
    struct.pack_into('<I', md, 80, cast)               # 1 fire-and-forget (one cast per click), 2 concentration
    struct.pack_into('<I', md, 84, 0)                  # self (nothing launched natively)
    if aimed:   # (a staff's concentration aimed: the staff pointed, not the hand's cast - the carrier flies, unseen and harmless)
        struct.pack_into('<I', md, 72, fid('PROJ', 'castflame')); struct.pack_into('<I', md, 84, 2)
    for off in (92, 96, 100, 116): struct.pack_into('<I', md, off, 0)  # casting art set at runtime from the matching vanilla spell
    recs['MGEF'].append(record('MGEF', fid('MGEF', key), [
        sub('EDID', z('UN_' + key)), sub('FULL', z(label)), sub('MDOB', tsub('mgef_firebolt', 'MDOB')), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
cast_mgef('wandcast', 'Wand Cast', 1)
# (Noah: the Carian Wand "point like a staff ... holding the attack should cast automatically every 2s") held: Skyrim's
# staff concentration keeps the wand pointed while the attack is held; src/Classes.cpp TickWand looses the bolts
cast_mgef('wandhold', 'Carian Bolt', 2, aimed=True)
# (Noah: "lets just make it a summon spell") the Call of the Violet Wyrm's effect: a Conjuration cast (its look and sound
# taken at runtime from Skyrim's Conjure Familiar - src/Classes.cpp), the wyrm called by src/WyrmHorn.inl WyrmCast
cast_mgef('wyrmcall', 'Call of the Violet Wyrm', 1)

# (Noah: "replace the crossbows with ... throwing daggers") the Ranger's Throwing Daggers: a hand spell (fire-and-forget,
# self: only the throw's animation) - on each cast src/Classes.cpp ThrowDaggers launches the knives themselves: a spinning
# steel knife (tools/throwdagger.py) carrying an ember strike (Firebolt's burn on what it hits; its magnitude set by the
# daggers' tier at each throw, on the one damage curve every spell shares)
pd = bytearray(tsub('proj_bullet', 'DATA'))
struct.pack_into('<H', pd, 0, 0x0)                    # a plain magic missile
struct.pack_into('<f', pd, 4, 0.25)                   # a little drop
struct.pack_into('<f', pd, 8, 3800.0)                 # speed
struct.pack_into('<I', pd, 16, 0); struct.pack_into('<I', pd, 20, 0)
struct.pack_into('<I', pd, 36, 0); struct.pack_into('<I', pd, 40, 0)
struct.pack_into('<I', pd, 56, 0); struct.pack_into('<I', pd, 60, 0)
recs['PROJ'].append(record('PROJ', fid('PROJ', 'throwdagger'), [
    sub('EDID', z('UN_ThrowDaggerProj')), sub('OBND', tsub('proj_bullet', 'OBND')), sub('MODL', z('CurseOfAlduin\\ranger\\throwdagger.nif')),
    sub('DATA', bytes(pd)), sub('NAM1', b'\0'), sub('VNAM', struct.pack('<I', 0))]))
md = bytearray(tsub('mgef_firebolt', 'DATA'))
struct.pack_into('<I', md, 16, 0xFFFFFFFF)            # no resistance: steel and ember
struct.pack_into('<I', md, 24, 0)                     # no light in the hand
struct.pack_into('<I', md, 72, fid('PROJ', 'throwdagger'))
struct.pack_into('<I', md, 76, 0)
struct.pack_into('<I', md, 92, 0)                     # no casting art
recs['MGEF'].append(record('MGEF', fid('MGEF', 'throwdagger'), [
    sub('EDID', z('UN_ThrowDaggerHit')), sub('FULL', z('Throwing Dagger')), sub('MDOB', tsub('mgef_firebolt', 'MDOB')), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
sp = bytearray(tsub('spel_firebolt', 'SPIT'))
struct.pack_into('<I', sp, 0, 0); struct.pack_into('<I', sp, 4, 0x1)
recs['SPEL'].append(record('SPEL', fid('SPEL', 'throwdagger'), [
    sub('EDID', z('UN_ThrowDaggerHitSpell')), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('ETYP', tsub('spel_firebolt', 'ETYP')),
    sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', 'throwdagger'))), sub('EFIT', struct.pack('<fII', 20.0, 0, 0))]))
cast_mgef('throwdaggers', 'Throwing Daggers', 1)

# (Noah: "replace the Ranger class with the Alchemist") the Alchemist's flasks: each a hand spell (fire-and-forget, self:
# only the throw's animation) - src/Classes.cpp ThrowFlask lobs the flask itself (a tumbling glass flask, tools/flasks.py,
# carrying nothing: where it comes down our code bursts it); the Philosopher's Flask is the class's wonder
cast_mgef('flaskcast', 'Flask', 1)
FLASKS = [('fire', 'Fire Flask', 'A flask of liquid fire. It bursts where it lands, burning everything round it.'),
          ('frost', 'Frost Flask', 'A flask of frozen brine. It bursts into killing cold where it lands.'),
          ('shock', 'Shock Flask', 'A flask of bottled storm. It bursts into arcing lightning where it lands.'),
          ('poison', 'Acid Flask', 'A flask of green acid. It bursts into a corroding splash where it lands.'),
          ('sun', 'Holy Water', 'Blessed water of Stendarr. It bursts into searing light where it lands; the dead burn in it.'),
          ('blood', 'Voidglass Flask', 'A flask of bottled void. It shatters and drags the dead in to where it broke.'),
          ('philosopher', "Mutagen Staff", 'The Alchemist\'s wonder: a staff whose orb is fed by six vials of the elements. Hold attack to loose a beam from the orb that pierces the dead, pouring each vial in turn for 5 seconds; let go and the emptied vials refill.')]
FLASK_TIERS = ' The glass shatters, its element bursts, and a pool of it lingers on the ground. Tier I: a burst half again as wide. Tier II: a chain reaction - the dead it kills burst in its element too. Legendary: a cyclone of its element rises where it breaks and rolls into the dead.'
for e, label, text in FLASKS:
    pd = bytearray(tsub('proj_bullet', 'DATA'))
    struct.pack_into('<H', pd, 0, 0x0)                    # a plain magic missile
    struct.pack_into('<f', pd, 4, 1.0)                    # a full lob: it arcs
    struct.pack_into('<f', pd, 8, 1700.0 if e != 'philosopher' else 1500.0)
    struct.pack_into('<I', pd, 16, 0); struct.pack_into('<I', pd, 20, 0)
    struct.pack_into('<I', pd, 36, 0); struct.pack_into('<I', pd, 40, 0)
    struct.pack_into('<I', pd, 56, 0); struct.pack_into('<I', pd, 60, 0)
    recs['PROJ'].append(record('PROJ', fid('PROJ', 'flask_' + e), [
        sub('EDID', z('UN_FlaskProj_' + e)), sub('OBND', tsub('proj_bullet', 'OBND')), sub('MODL', z('CurseOfAlduin\\alchemist\\flask_' + e + '.nif')),
        sub('DATA', bytes(pd)), sub('NAM1', b'\0'), sub('VNAM', struct.pack('<I', 0))]))
    md = bytearray(tsub('mgef_firebolt', 'DATA'))
    struct.pack_into('<I', md, 0, 0x200 | 0x400 | 0x800)  # not hostile: the flask itself does nothing (its burst is ours)
    struct.pack_into('<I', md, 24, 0); struct.pack_into('<I', md, 32, 0)
    struct.pack_into('<I', md, 64, 1); struct.pack_into('<i', md, 68, -1)
    struct.pack_into('<I', md, 72, fid('PROJ', 'flask_' + e))
    for off in (76, 92, 96, 100): struct.pack_into('<I', md, off, 0)
    recs['MGEF'].append(record('MGEF', fid('MGEF', 'flask_' + e), [
        sub('EDID', z('UN_FlaskFlight_' + e)), sub('MDOB', tsub('mgef_firebolt', 'MDOB')), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
    sp = bytearray(tsub('spel_firebolt', 'SPIT'))
    struct.pack_into('<I', sp, 0, 0); struct.pack_into('<I', sp, 4, 0x1)
    recs['SPEL'].append(record('SPEL', fid('SPEL', 'flask_' + e), [
        sub('EDID', z('UN_FlaskFlightSpell_' + e)), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('ETYP', tsub('spel_firebolt', 'ETYP')),
        sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', 'flask_' + e))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
    sp = bytearray(tsub('spel_firebolt', 'SPIT'))
    struct.pack_into('<IIIfIIffI', sp, 0, 100 if e == 'philosopher' else 25, 0x1, 0, 0.0, 1, 0, 0.0, 0.0, 0)  # (its magicka the code spends: the class spells' one cost)
    recs['SPEL'].append(record('SPEL', fid('SPEL', 'throw_' + e), [
        sub('EDID', z('UN_Throw_' + e)), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z(label)),
        sub('MDOB', tsub('spel_firebolt', 'MDOB')), sub('ETYP', tsub('spel_firebolt', 'ETYP')),
        sub('DESC', z(text)),
        sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', 'flaskcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
# (Noah, 2026-11-05: "Arcane / Spectral" in Blood's place, blue-white) the Destruction ladder's first three rungs and the
# Spellsword's arcane blade: hand spells that only play the cast (cast_mgef) - src/Classes.cpp ArcaneCast does the rest
ARCANE = [('bolt', 'Arcane Bolt', 'A bolt of blue-white arcane light.', 30), ('orb', 'Mana Void', 'An orb of the void: it bursts where it lands and drags the dead in.', 60),
          ('tide', 'Lunar Tide', 'A ring of arcane light bursts out round you and throws the dead back.', 90),
          ('blade1', 'Arcane Edge', 'Your blade bursts in arcane light on each hit.', 20), ('blade2', 'Arcane Brand', 'Your blade bursts in wide arcane light on each hit.', 30),
          ('blade3', 'Arcane Cascade', 'Your blade bursts in arcane light on each hit, and the burst leaps to two more of the dead.', 40)]
for key, label, text, cost in ARCANE:
    cast_mgef('arcane_' + key, label, 1)
    sp = bytearray(tsub('spel_firebolt', 'SPIT'))
    struct.pack_into('<IIIfIIffI', sp, 0, cost, 0x1, 0, 0.0, 1, 0, 0.5, 0.0, 0)
    recs['SPEL'].append(record('SPEL', fid('SPEL', 'arcane_' + key), [
        sub('EDID', z('UN_Arcane_' + key)), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z(label)),
        sub('MDOB', tsub('spel_firebolt', 'MDOB')), sub('ETYP', tsub('spel_firebolt', 'ETYP')), sub('DESC', z(text)),
        sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', 'arcane_' + key))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))

# (Noah: "I want the flasks to not use mana, and I want it to be an item that you actually throw, not a destruction spell")
# each flask an item in your pack (a potion to Skyrim, so a favourite and a hotkey): used, it is thrown - src/Classes.cpp
# sees it leave your pack, lobs it, and puts it back (an Alchemist's flasks never run out; a short breath between throws)
for e, label, text in FLASKS:
    enit = bytearray(tsub('alch', 'ENIT'))
    struct.pack_into('<iIIfI', enit, 0, 0, 0x1, 0, 0.0, 0)   # value 0, no auto-calc, no addiction, no drinking sound
    recs['ALCH'].append(record('ALCH', fid('ALCH', 'flaskitem_' + e), [
        sub('EDID', z('UN_FlaskItem_' + e)), sub('OBND', tsub('alch', 'OBND')), sub('FULL', z(label)),
        sub('KSIZ', tsub('alch', 'KSIZ')), sub('KWDA', tsub('alch', 'KWDA')), sub('MODL', z('CurseOfAlduin\\alchemist\\flask_' + e + '.nif')),
        sub('YNAM', tsub('alch', 'YNAM')), sub('ZNAM', tsub('alch', 'ZNAM')), sub('DATA', struct.pack('<f', 0.0)),
        sub('ENIT', bytes(enit)), sub('EFID', struct.pack('<I', fid('MGEF', 'flaskcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
# (Noah: "I want to be able to hold it in my left or right hand with an underhand throwing animation. It should feel like a
# weapon") each flask a weapon held in either hand (a dagger's record, the still flask its model): its attack throws it
# underhand (src/Gun.cpp catches the button and the arm's swing; src/Classes.cpp lobs it) - never used up
# (Noah: the Mutagen Gauntlet made a staff - "Hold attack, staff thrust") its enchantment: concentration, on yourself (nothing
# launched natively), a staff's, free - only so Skyrim plays its staff cast while the attack is held; the beam is ours
cast_mgef('mutagencast', 'Mutagen Beam', 2)
recs['ENCH'].append(record('ENCH', fid('ENCH', 'mutagenstaff'), [
    sub('EDID', z('UN_MutagenStaffEnch')), sub('OBND', bytes(12)), sub('FULL', z('Mutagen Beam')),
    sub('ENIT', struct.pack('<iIIiIIfII', 0, 0x1, 2, 3000, 0, 0x0C, 0.0, 0, 0)),
    sub('EFID', struct.pack('<I', fid('MGEF', 'mutagencast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
for e, label, text in FLASKS:
    subs = []
    if e == 'philosopher':   # the staff: Skyrim's staff record, our model and enchantment (the same form: old saves keep it)
        for name, hx in T['weap_staff']['subs']:
            b = bytes.fromhex(hx)
            if name == 'EDID': b = z('UN_Flask_' + e)
            elif name == 'FULL': b = z('Mutagen Staff')
            elif name == 'MODL': b = z('CurseOfAlduin\\alchemist\\mutagen_staff.nif')
            elif name == 'EITM': b = struct.pack('<I', fid('ENCH', 'mutagenstaff'))
            elif name == 'EAMT': b = struct.pack('<H', 3000)
            elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_flask_' + e))
            elif name == 'DESC': b = z(text)
            elif name == 'MODT': continue
            elif name == 'DATA': b = struct.pack('<IfH', 0, 1.0, 0)
            subs.append(sub(name, b))
        recs['WEAP'].append(record('WEAP', fid('WEAP', 'flaskweap_' + e), subs))
        continue
    for name, hx in T['weap_daedricdagger']['subs']:
        b = bytes.fromhex(hx)
        if name == 'EDID': b = z('UN_Flask_' + e)
        elif name == 'FULL': b = z(label)
        elif name == 'MODL': b = z('CurseOfAlduin\\alchemist\\gauntlet_hold.nif' if e == 'philosopher' else 'CurseOfAlduin\\alchemist\\flask_' + e + '_held.nif')   # (Noah: the Mutagen Gauntlet - "Glowing hand only")
        elif name in ('MODT', 'EITM', 'EAMT'): continue
        elif name == 'DESC': b = z(text + ('' if e == 'philosopher' else ' Attack to throw it; it never runs out.'))   # (Noah: short - its tier's line added by src/Classes.cpp TickItemCard)
        elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_flask_' + e))
        # (Noah: "it still makes dagger sound effects when drawing ... the alchemist class should have its own combat noises")
        # glass and liquid drawing and putting away (tools/alchsfx.py), no blade's failed-swing or steel impact set
        # (Noah: "the equip and unequip sound ... should be a potion noise, not a high-pitched chime") Skyrim's own potion
        # sounds: picking a potion up and putting it down
        elif name == 'NAM9': b = tsub('alch', 'YNAM')
        elif name == 'NAM8': b = tsub('alch', 'ZNAM')
        elif name in ('TNAM', 'INAM'): b = struct.pack('<I', 0)
        elif name == 'DATA': b = struct.pack('<IfH', 0, 0.5, 0)
        subs.append(sub(name, b))
    recs['WEAP'].append(record('WEAP', fid('WEAP', 'flaskweap_' + e), subs))
# (Noah: "the 10 priest drop weapons") each Dragon Priest's weapon (esp_ids.PRIEST_WEAPONS): Skyrim's own swings for
# its kind - one-handed blades on the dagger's record with a sword's swings, two-handers on the draugr greatsword's
# with a greatsword's or a battleaxe's / warhammer's, the bows on the longbow's (their models stand on the bow rig)
from esp_ids import PRIEST_WEAPONS
PW_KIND = {   # template, animation type, speed, reach, weight, its keywords (WeapType*, VendorItemWeapon)
    'sword': ('weap_daedricdagger', 1, 1.0, 1.0, 12.0, (0x01E711, 0x08F958)),
    'dagger': ('weap_daedricdagger', 2, 1.3, 0.7, 5.0, (0x01E713, 0x08F958)),
    'greatsword': ('weap_draugr_gs', 5, 0.75, 1.3, 18.0, (0x06D931, 0x08F958)),
    'battleaxe': ('weap_draugr_gs', 6, 0.7, 1.3, 24.0, (0x06D932, 0x08F958)),
    'warhammer': ('weap_draugr_gs', 6, 0.65, 1.3, 26.0, (0x06D930, 0x08F958)),
    'bow': ('weap_longbow', 7, 1.0, 1.0, 10.0, (0x01E715, 0x08F958)),
}
for _town, (pk, plabel, pmodel, pkind, pdmg, ptext) in PRIEST_WEAPONS.items():
    tmpl, anim, spd, reach, wt, kws = PW_KIND[pkind]
    subs = []
    for name, hx in T[tmpl]['subs']:
        b = bytearray(bytes.fromhex(hx))
        if name == 'EDID': b = z('UN_Priest_' + pk)
        elif name == 'FULL': b = z(plabel)
        elif name == 'MODL': b = z('CurseOfAlduin\\priests\\' + pmodel + '.nif')
        elif name in ('MODT', 'EITM', 'EAMT'): continue
        elif name == 'KSIZ': b = struct.pack('<I', len(kws))
        elif name == 'KWDA': b = struct.pack(f'<{len(kws)}I', *kws)
        elif name == 'DESC': b = z(ptext)
        elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_priest_' + pk))
        elif name == 'DATA': b = struct.pack('<IfH', 0, wt, pdmg)
        elif name == 'DNAM':
            if pkind != 'bow': b[0] = anim; struct.pack_into('<ff', b, 4, spd, reach)
        subs.append(sub(name, bytes(b)))
    recs['WEAP'].append(record('WEAP', fid('WEAP', 'priest_' + pk), subs))
# (Noah: the Tablet of Mysteries) its donated weapons (sheet: tablet): Skyrim's own swings for each kind; the fists a hand-to-
# hand weapon (punches, nothing in the hand); the staffs on the staff's record (their casts are ours); the Codex a hand
# spell (its casts are ours, src/Tablet.cpp); the bows' arrows are their uses. Lost if you fall.
TB_KIND = dict(PW_KIND, mace=('weap_daedricmace', 4, 0.9, 1.0, 14.0, (0x01E714, 0x08F958)), fists=('weap_daedricdagger', 0, 1.0, 0.7, 1.0, (0x01E712,)),
               staff=('weap_staff', 8, 1.0, 1.0, 8.0, (0x01E716, 0x08F958)))
# the donated staffs' enchantment: fire and forget, on yourself (nothing launched natively), a staff's, free - only so Skyrim
# plays its staff cast; what it casts is ours (src/Tablet.cpp: the Lanternwick's orb, the Wandering Star)
cast_mgef('tabstaffcast', 'Donated Staff', 1)
recs['ENCH'].append(record('ENCH', fid('ENCH', 'tabstaff'), [
    sub('EDID', z('UN_TabletStaffEnch')), sub('OBND', bytes(12)), sub('FULL', z('Donated Staff')),
    sub('ENIT', struct.pack('<iIIiIIfII', 0, 0x1, 1, 3000, 0, 0x0C, 0.0, 0, 0)),
    sub('EFID', struct.pack('<I', fid('MGEF', 'tabstaffcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
# (Noah: "Destruction - Carian Wands, one per hand") the wands' enchantment: fire and forget on yourself, free - only so Skyrim
# plays the cast; its Carian bolt is ours (src/Classes.cpp WandCast)
recs['ENCH'].append(record('ENCH', fid('ENCH', 'carianwand'), [
    sub('EDID', z('UN_CarianWandEnch')), sub('OBND', bytes(12)), sub('FULL', z('Carian Bolt')),
    sub('ENIT', struct.pack('<iIIiIIfII', 0, 0x1, 2, 3000, 2, 0x0C, 0.0, 0, 0)),
    sub('EFID', struct.pack('<I', fid('MGEF', 'wandhold'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
for w in S['tablet']['rows']:
    if w['type'] == 'book': continue
    tmpl, anim, spd, reach, wt, kws = TB_KIND[w['type']]
    desc = w['power'] + ' ' + w['owner'] + ' Donated to the Tablet of Mysteries. Worn with use, and lost if you fall.'
    subs = []
    for name, hx in T[tmpl]['subs']:
        b = bytearray(bytes.fromhex(hx))
        if name == 'EDID': b = z('UN_Tablet_' + w['id'])
        elif name == 'FULL': b = z(w['label'])
        elif name == 'MODL':
            if not w['model']: continue
            b = z(w['model'])
        elif name in ('MODT', 'EAMT', 'BIDS', 'BAMT', 'INAM'): continue
        elif name == 'EITM':
            if w['type'] != 'staff': continue
            b = struct.pack('<I', fid('ENCH', 'tabstaff'))   # (its cast is ours: the orb, the star)
        elif name == 'KSIZ': b = struct.pack('<I', len(kws))
        elif name == 'KWDA': b = struct.pack(f'<{len(kws)}I', *kws)
        elif name == 'DESC': b = z(desc)
        elif name == 'WNAM': continue
        elif name == 'DATA': b = struct.pack('<IfH', 0, wt, 20)
        elif name == 'DNAM':
            if w['type'] not in ('bow', 'staff'): b[0] = anim; struct.pack_into('<ff', b, 4, spd, reach)
        subs.append(sub(name, bytes(b)))
    recs['WEAP'].append(record('WEAP', fid('WEAP', 'tab_' + w['id']), subs))
# (Noah: "real hand wraps or elemental brass knuckles") the fists' gloves: worn in the hands' slot (33) with the weapon, on
# every race the belt lantern's armour piece is, their first-person model the hands' own (tools/tabletgloves.py)
LANTERN_RACES = '40370100 3a880800 41370100 3c880800 3d7a0900 42370100 3d880800 d87c0600 ba820a00 43370100 40880800 44370100 44880800 45370100 45880800 0a761000 46370100 94870800 47370100 b9820a00 48370100 46880800 49370100 84880800'
for w in S['tablet']['rows']:
    if not w.get('gloves'): continue
    third, first = w['gloves']
    recs['ARMA'].append(record('ARMA', fid('ARMA', 'tabglove_' + w['id']), [sub('EDID', z('UN_TabletGlovesAA_' + w['id'])), sub('BOD2', struct.pack('<II', 1 << 3, 2)),
        sub('RNAM', struct.pack('<I', 0x19)), sub('DNAM', bytes.fromhex('0a0a00000000000000000000')),
        sub('MOD2', z(third)), sub('MOD3', z(third)), sub('MOD4', z(first)), sub('MOD5', z(first))] +
        [sub('MODL', bytes.fromhex(h)) for h in LANTERN_RACES.split()]))
    recs['ARMO'].append(record('ARMO', fid('ARMO', 'tabglove_' + w['id']), [sub('EDID', z('UN_TabletGloves_' + w['id'])), sub('OBND', struct.pack('<6h', -6, -6, 0, 6, 6, 6)),
        sub('FULL', z(w['label'])), sub('MOD2', z(w['show'])), sub('BOD2', struct.pack('<II', 1 << 3, 2)), sub('RNAM', struct.pack('<I', 0x19)),
        sub('KSIZ', struct.pack('<I', 1)), sub('KWDA', struct.pack('<I', 0x08F95B)),   # (ArmorClothing)
        sub('DESC', z(w['power'] + ' ' + w['owner'] + ' Donated to the Tablet of Mysteries. Worn with use, and lost if you fall.')),
        sub('MODL', struct.pack('<I', fid('ARMA', 'tabglove_' + w['id']))), sub('DATA', struct.pack('<If', 0, 0.5)), sub('DNAM', struct.pack('<I', 0))]))
pd = bytearray(tsub('proj_arrow', 'DATA'))
recs['PROJ'].append(record('PROJ', fid('PROJ', 'tab_arrow'), [
    sub('EDID', z('UN_TabletArrowProj')), sub('OBND', tsub('proj_arrow', 'OBND')), sub('FULL', z('Donated Arrow')),
    sub('MODL', z('Weapons\\Iron\\IronArrowProjectile.nif')), sub('DATA', bytes(pd)), sub('NAM1', b'\0'), sub('VNAM', tsub('proj_arrow', 'VNAM'))]))
recs['AMMO'].append(record('AMMO', fid('AMMO', 'tab_arrow'), [
    sub('EDID', z('UN_TabletArrow')), sub('OBND', tsub('ammo_iron', 'OBND')), sub('FULL', z('Donated Arrow')),
    sub('MODL', z('Weapons\\Iron\\IronArrow.nif')), sub('YNAM', tsub('ammo_iron', 'YNAM')), sub('ZNAM', tsub('ammo_iron', 'ZNAM')),
    sub('KSIZ', tsub('ammo_iron', 'KSIZ')), sub('KWDA', tsub('ammo_iron', 'KWDA')), sub('DATA', struct.pack('<IIfIf', fid('PROJ', 'tab_arrow'), 0x4, 12.0, 0, 0.0))]))
for w in S['tablet']['rows']:
    if w['type'] != 'book': continue
    md = bytearray(tsub('mgef_firebolt', 'DATA'))
    # (its chapters are ours, src/Tablet.cpp: no firebolt) cast on yourself, fire and forget, no projectile, no burst, no
    # fire in the hand or on the struck
    for off, val in ((72, 0), (76, 0), (80, 1), (84, 0), (92, 0), (96, 0), (100, 0)): struct.pack_into('<I', md, off, val)
    recs['MGEF'].append(record('MGEF', fid('MGEF', 'tab_' + w['id']), [
        sub('EDID', z('UN_TabletMgef_' + w['id'])), sub('FULL', z(w['label'])), sub('MDOB', tsub('mgef_firebolt', 'MDOB')), sub('DATA', bytes(md)),
        sub('DNAM', z(''))]))
    sp = bytearray(tsub('spel_firebolt', 'SPIT'))
    struct.pack_into('<I', sp, 0, 0)   # (no magicka: its pages are its cost)
    struct.pack_into('<II', sp, 16, 1, 0)   # (fire and forget, on yourself: the chapter goes where you aim, src/Tablet.cpp)
    recs['SPEL'].append(record('SPEL', fid('SPEL', 'tab_' + w['id']), [
        sub('EDID', z('UN_Tablet_' + w['id'])), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z(w['label'])),
        sub('MDOB', tsub('spel_firebolt', 'MDOB')), sub('ETYP', tsub('spel_firebolt', 'ETYP')),
        sub('DESC', z(w['power'] + ' ' + w['owner'] + ' Donated to the Tablet of Mysteries. Worn with use, and lost if you fall.')),
        sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', 'tab_' + w['id']))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
# (Noah: the Summoner's new wonder, "Horn of the Violet Wyrm: an undead bone dragon that lands and fights beside you") a hand spell
# at 100 magicka: src/Classes.cpp calls the dragon up itself (Skyrim's skeletal dragon) and keeps it on the ground
sp = bytearray(tsub('spel_firebolt', 'SPIT'))
struct.pack_into('<IIIfIIffI', sp, 0, 100, 0x1, 0, 0.25, 1, 0, 0.0, 0.0, 0)
recs['SPEL'].append(record('SPEL', fid('SPEL', 'bonehorn'), [
    sub('EDID', z('UN_BoneThroneHorn')), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z('Call of the Violet Wyrm')),
    sub('MDOB', tsub('spel_firebolt', 'MDOB')), sub('ETYP', tsub('spel_firebolt', 'ETYP')),
    sub('DESC', z('The Summoner\'s wonder. A violet wyrm flies in and burns the dead within 50 m of you for 90 seconds; when it departs the call rests a minute. Once the three dragons fall, a wyrmling answers beside it.')),
    sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', 'wyrmcall'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
sp = bytearray(tsub('spel_firebolt', 'SPIT'))
struct.pack_into('<IIIfIIffI', sp, 0, 0, 0x1, 0, 0.0, 1, 0, 0.0, 0.0, 0)   # no magicka (a throw spends stamina), no charge, fire-and-forget, self
recs['SPEL'].append(record('SPEL', fid('SPEL', 'throwdaggers'), [
    sub('EDID', z('UN_ThrowingDaggers')), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z('Throwing Daggers')),
    sub('MDOB', tsub('spel_firebolt', 'MDOB')), sub('ETYP', tsub('spel_firebolt', 'ETYP')),
    sub('DESC', z('The Ranger\'s steel: each throw looses ember-hot knives at what you aim at, for a little stamina. Tier I throws three in a fan, Tier II five, Legendary a second volley on the first.')),
    sub('SPIT', bytes(sp)), sub('EFID', struct.pack('<I', fid('MGEF', 'throwdaggers'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))

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

# ---- (Noah: each class its own wonder) the Destruction class's Tome of the Six Fires: a spell that does nothing itself -
# Classes::OnCast looses the six element ladders' top spells when it is cast
cast_mgef('tomecast', 'Six Fires', 1)   # (Noah: 100 magicka, no cooldown)
recs['SPEL'].append(record('SPEL', fid('SPEL', 'wonder_tome'), [
    sub('EDID', z('UN_WonderTomeSixFires')), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z('Tome of the Six Fires')),
    sub('MDOB', tsub('spel_firebolt', 'MDOB')), sub('ETYP', tsub('spel_firebolt', 'ETYP')),
    sub('DESC', z('Your class wonder. One cast looses the top spell of all six element ladders on the dead around you.')),
    sub('SPIT', struct.pack('<IIIfIIffI', 100, 0x1, 0, 0.25, 1, 0, 0.0, 0.0, 0)), sub('EFID', struct.pack('<I', fid('MGEF', 'tomecast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))

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

# ---- (Noah: "Make it a shout ... once per wave ... 20 seconds ... Dance Of Death") the dead within 20 m dance; every
# word count does the same (Gun::DanceOfDeath); learned when Solitude's altar wakes (the Bards' city)
DANCE_WORDS = [('DUN', 'Grace'), ('GRAH', 'Battle'), ('MAAR', 'Terror')]
for n, (w, tr) in enumerate(DANCE_WORDS):
    recs['WOOP'].append(record('WOOP', fid('WOOP', f'dance_{n}'), [sub('EDID', z(f'UN_Word_dance_{n}')), sub('FULL', z(w)), sub('TNAM', z(tr))]))
    sp = struct.pack('<IIIfIIffI', 0, 0x1, 11, 0.0, 1, 0, 0.0, 0.0, 0)
    recs['SPEL'].append(record('SPEL', fid('SPEL', f'dance_{n}'), [
        sub('EDID', z(f'UN_ShoutSpell_dance_{n}')), sub('OBND', tsub('spel_firebolt', 'OBND')), sub('FULL', z('Dance Of Death')),
        sub('ETYP', struct.pack('<I', 0x025BEE)), sub('DESC', z('')), sub('SPIT', sp),
        sub('EFID', struct.pack('<I', fid('MGEF', 'shoutcast'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))
recs['SHOU'].append(record('SHOU', fid('SHOU', 'dance'), [sub('EDID', z('UN_Shout_dance')), sub('FULL', z('Dance Of Death')),
    sub('DESC', z("The Curse's dead within twenty paces dance for twenty seconds: they cannot move or strike, and take half again as much harm. Once a wave."))] +
    [sub('SNAM', struct.pack('<IIf', fid('WOOP', f'dance_{n}'), fid('SPEL', f'dance_{n}'), 5.0)) for n in range(3)]))

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
arto('fx_portal', 'CurseOfAlduin\\fx\\portal_red.nif', 1)        # (the night's dead come through a conjuring portal: KittyVFX - Portals' summon mesh, in the Curse's red -
arto('fx_portal_big', 'CurseOfAlduin\\fx\\portal_red_big.nif', 1)  #  tools/modelconv/fxtint, shipped with the Party Pack; elites and bosses get the big one)
# (Noah: "remake the staff - a green orb, three meters of splash damage, one-time damage that sets the target for the
# summons") the Conjurer's Staff's bolt: Skyrim's own staff cast launching the frostbite spider's green spit; it does next
# to nothing itself - Classes::ConjurerStaffHit does the damage round it and marks the target
md = bytearray(tsub('mgef_firebolt', 'DATA'))
struct.pack_into('<I', md, 24, 0)                     # no light
struct.pack_into('<I', md, 32, 0)                     # no hit shader
struct.pack_into('<I', md, 72, 0x0004600A)            # MAGSpiderSpitProjectile: a green glob
struct.pack_into('<I', md, 76, 0)                     # no explosion (ours is the splash)
for off in (92, 96, 100): struct.pack_into('<I', md, off, 0)   # no fire in the hand, no fire on the target
recs['MGEF'].append(record('MGEF', fid('MGEF', 'conjurerbolt'), [
    sub('EDID', z('UN_ConjurerBolt')), sub('FULL', z("Conjurer's Bolt")), sub('MDOB', tsub('mgef_firebolt', 'MDOB')), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
for w in S['weapons']['rows']:
    kind = w.get('kind', 'wand')
    if kind == 'wand':
        # enchantment: concentration, aimed, staff-type, free (infinite casts)
        # staff cast (point and release), aimed: Skyrim launches the wand's own element bolt from the tip
        if w.get('nativeEnch'):
            # (Noah: the Falmer staffs cast for real) Skyrim's own staff spell - its projectile, its staff casting animation -
            # with the vanilla staff's effects (Skyrim.esm MGEFs), free of charge; our code adds its numbers on the hit
            enit = struct.pack('<iIIiIIfII', 0, 0x1, 1, 3000, 2, 0x0C, 0.35, 0, 0)  # fire-and-forget, aimed, staff enchantment, free; (Noah: "30% shorter charge on all staffs") 0.35 s, was 0.5
            effs = []
            for mg, mag, area, dur in w['nativeEnch']:
                mgid = fid('MGEF', mg[5:]) if mg.startswith('ours:') else int(mg, 16)
                effs += [sub('EFID', struct.pack('<I', mgid)), sub('EFIT', struct.pack('<fII', float(mag), area, dur))]
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
    'fire': 'Base: burns. I Flare Shot: bursts into a fireball. II Scorch Arrow: Scorch bursts where it strikes. III Immolation: a full draw seeks the nearest of the dead and Immolate engulfs them.',
    'frost': 'Base: slows. I Deep Freeze: freezes solid. II Flash Freeze: Flash Freeze where it strikes, and the frozen burst into shards. III Throat of the World: a full draw seeks the nearest of the dead and bursts into killing cold.',
    'shock': 'Base: leaps to one more. I Chain Lightning: leaps to three. II Finger of the Mountain: lightning from the strike, and the struck shock those round them. III Electrocution: a full draw casts Electrocute and leaves a storm ball hunting the dead.',
    'wind': 'Base: knocks back. I Gale Burst: the dead round its impact are hurled back. II Cyclone Arrow: a whirlwind where it lands. III Cinder Gale: a full draw hurls every one of the dead round it far away inside a Cinder Storm.',
    'sun': 'Base: sears and staggers. I Radiant Burst: light bursts round it. II Solar Flare: Solar Flare bursts from the strike. III Eclipse: a full draw casts Eclipse and a tornado of sunfire hunts the dead.',
    'blood': 'Base: heals you a little. I Hemorrhage: they bleed. II Vampiric Bolt: a bolt of blood feeds you from the strike. III Blood Lance: a full draw seeks the dead with Blood Lance, and kills burst into orbs of blood.'}
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

# (Noah: "replace the madness sword ... replace the rift blade with the new white sword ... change the shadowflame mace to be a
# shadowflame sword with a model from one of our mods ... destruction should get an elemental upgradable sword too") the four
# class swords are Kv's Crystal Sword (its four colours; the Party Pack carries its models and textures loose), on the
# Madness Sword's record (a one-handed sword); the Dragon Slayer is its white greatsword on the Draugr greatsword's. Their tier
# looks are the bv_* enchantments below; their abilities src/Classes.cpp (the tier ladder)
def crystal_weapon(key, edid, full, model, first, desc, data, tmpl='weap_madness', speed=None):
    subs = []
    for name, hx in T[tmpl]['subs']:
        b = bytes.fromhex(hx)
        if name == 'EDID': b = z(edid)
        elif name == 'FULL': b = z(full)
        elif name == 'MODL': b = z(model)
        elif name in ('MODT', 'EITM', 'EAMT'): continue
        elif name == 'DESC': b = z(desc)
        elif name == 'KSIZ' and tmpl == 'weap_madness': b = struct.pack('<I', 2)
        elif name == 'KWDA' and tmpl == 'weap_madness': b = bytes.fromhex(hx)[:8]
        elif name == 'WNAM': b = struct.pack('<I', fid('STAT', first))
        elif name == 'DATA': b = struct.pack('<IfH', *data)
        elif name == 'DNAM' and speed: b = bytearray(b); struct.pack_into('<f', b, 4, speed); b = bytes(b)
        subs.append(sub(name, b))
        if name == 'MODL': subs.append(sub('EAMT', struct.pack('<H', 0)))   # (no charge bar: its looks never run down)
    if tmpl == 'weap_draugr_gs' and not any(n == 'WNAM' for n, _ in T[tmpl]['subs']):
        subs.append(sub('WNAM', struct.pack('<I', fid('STAT', first))))
    recs['WEAP'].append(record('WEAP', fid('WEAP', key), subs))
KV = 'kv\\crystalsword\\'
# (Noah: "all of the swords needs real work and attention to detail for inventory view and descriptions") what each sword is
# and what each of its tiers does - its description, and (TIER_TEXT) the tier's own line on the item card
SWORD_DESC = {   # (Noah: "way too long" - the card tells what the sword is; each tier's line is added by src/Classes.cpp TickItemCard)
    'radiant': "The Summoner's blade: pale silver, burning with white sunlight.",
    'carian': "Destruction's wand, held in the right hand: hold its cast and it looses a bolt of raw sorcery every 2 seconds - fire, lightning and ice in turn.",
    'alch': "The Alchemist's blade: daedric steel wreathed in purple shadowflame.",
    'ss_none': "The Spellsword's blade: it takes the element of the spell in your other hand.",
    'slayer': "The Akaviri dragon-hunters' blade. Swings 50% faster. Light swings: golden fire, twice the damage to dragons. Heavy attacks: a cyclone of golden dragonfire.",
}
TIER_TEXT = {   # (the look's enchantment on the item card: what this tier adds)
    'radiant': ("Light swings loose white sun balls through the dead.", "Sun balls; heavy attacks call a pillar of sunlight down on the one you strike.", "Sun balls, and a great pillar of sunlight on every heavy attack."),
    'carian': ("Hold the cast: a bolt every 2 seconds - fire, lightning, ice in turn.", "Every 3rd bolt is a great bolt that pierces.", "Every 3rd bolt is a great bolt, with a crossing volley either side."),
    'alch': ("Light swings loose bolts of shadowflame that set the dead burning.", "Bolts of shadowflame; heavy attacks loose a cyclone of purple fire.", "Bolts of shadowflame, and a great cyclone of purple fire on every heavy attack."),
    'ss': ("Light swings loose bolts of your spell's element through the dead.", "Bolts of your spell's element; heavy attacks loose a cyclone of it.", "Bolts of your spell's element, and a great cyclone of it on every heavy attack."),
    'slayer': ("Golden fire: twice the damage to dragons; heavy attacks loose a cyclone of golden dragonfire.",) * 3,
}
TIER_NAME = {'radiant': 'Radiant Light', 'carian': 'Carian Sorcery', 'alch': 'Shadowflame', 'ss': 'Spellbound', 'slayer': 'Golden Dragonfire'}
# (Noah: "use the swords from Light Greatswords"; "make them required") the swords are Light Greatswords' own models, read from
# that mod (a requirement): the class swords drawn at 85% by src/Classes.cpp (one-handed), the Bane and the Slayer as they are
LGS = {'radiant': 'silver', 'carian': 'stahlrim', 'shadowflame': 'daedric', 'spellbound': 'nordic', 'bane': 'dragonbone', 'slayer': 'akaviri'}
def lgs(k): return f'Light Greatswords\\{LGS[k]}\\LightGreatsword.nif'
# (Noah: "hang them at the hip") the four class swords: our copies sheathed at the hip (src/ArmorForge.cpp BuildHipBlades)
def hip(k): return f'CurseOfAlduin\\hipblades\\{LGS[k]}.nif'
crystal_weapon('riftblade', 'UN_Riftblade', 'Radiant Quarterstaff', hip('radiant'), '1st_riftblade',
    SWORD_DESC['radiant'],
    (900, 10.0, 10))
crystal_weapon('carianblade', 'UN_CarianBlade', 'Carian Wand', hip('carian'), '1st_kvblue',
    SWORD_DESC['carian'],
    (900, 10.0, 10))

crystal_weapon('alchknife', 'UN_AlchemistKnife', 'Shadowflame Whip', hip('shadowflame'), '1st_mace',
    SWORD_DESC['alch'],
    (900, 10.0, 10))


# (Noah: "the weapon effects when upgrading should enhance the current detail - the Riftblade is black and red with a red
# ember breathing effect: make that brighter or a different colour") the Riftblade's fire enchantment's own shader,
# three times over: brighter and steadier each tier; from Tier II its embers fly off the blade; Legendary burns gold-white
# into crimson (src/Kit.cpp swaps the blade's enchantment to its tier's)
RIFT_TIERS = [
    dict(persist=0.75, pulse=0.6, particles=False),
    dict(persist=0.95, pulse=0.45, particles=True, birth=55.0, alpha=(0.05, 0.30, 0.05), scale2=8.0, keys=((255, 236, 210), (255, 190, 130), (250, 140, 80))),
    dict(persist=1.15, pulse=0.3, particles=True, birth=95.0, alpha=(0.10, 0.55, 0.10), scale2=10.0, life=0.75, keys=((255, 250, 235), (255, 196, 70), (235, 50, 40)), edge=(255, 70, 30)),
]
for n, t in enumerate(RIFT_TIERS, start=1):
    d = bytearray(tsub('efsh_enchfire', 'DATA'))
    if t['particles']: d[0] &= ~0x08                       # its embers on (the vanilla shader keeps them off)
    struct.pack_into('<f', d, 0x20, t['persist'])           # the fill's glow, brighter
    struct.pack_into('<f', d, 0x24, t['pulse'])             # its breathing, steadier
    if t['particles']:
        struct.pack_into('<f', d, 0x7C, t['birth'])
        struct.pack_into('<fff', d, 0xC8, *t['alpha'])
        struct.pack_into('<f', d, 0xB0, t['scale2'])
        for k, rgb in enumerate(t['keys']): d[0xBC + 4 * k:0xBC + 4 * k + 3] = bytes(rgb)
        if 'life' in t: struct.pack_into('<f', d, 0x84, t['life'])
    if 'edge' in t:
        d[0x38:0x3B] = bytes(t['edge'])
    subs = [sub('EDID', z(f'UN_RiftShader{n}'))]
    for name, hx in T['efsh_enchfire']['subs']:
        if name == 'EDID': continue
        subs.append(sub(name, bytes(d) if name == 'DATA' else bytes.fromhex(hx)))
    recs['EFSH'].append(record('EFSH', fid('EFSH', f'riftshader{n}'), subs))
    md = bytearray(tsub('mgef_enchfire', 'DATA'))
    struct.pack_into('<I', md, 36, fid('EFSH', f'riftshader{n}'))   # its enchantment shader: ours
    recs['MGEF'].append(record('MGEF', fid('MGEF', f'riftfire{n}'), [sub('EDID', z(f'UN_RiftFire{n}')), sub('FULL', z('Fire Damage')), sub('MDOB', tsub('mgef_enchfire', 'MDOB')),
        sub('KSIZ', tsub('mgef_enchfire', 'KSIZ')), sub('KWDA', tsub('mgef_enchfire', 'KWDA')), sub('DATA', bytes(md)), sub('DNAM', z('Burns the target for <mag> points.'))]))
    recs['ENCH'].append(record('ENCH', fid('ENCH', f'riftfire{n}'), [sub('EDID', z(f'UN_RiftEnch{n}')), sub('OBND', bytes(12)), sub('FULL', z('Fire Damage')),
        sub('ENIT', tsub('ench_fire05', 'ENIT')), sub('EFID', struct.pack('<I', fid('MGEF', f'riftfire{n}'))), sub('EFIT', tsub('ench_fire05', 'EFIT'))]))

# (Noah: "use the Madness Sword for the Spellsword's weapon") Saints & Seducers' Madness Sword as a record of our own: its
# stats and look, its first-person model a static of ours, so the class needs only the model and its textures (the
# Party Pack carries them loose), not the Creation Club plugin itself
MAD_GLOW = [  # its tiers: the blade's own fire, brighter each tier (src/Kit.cpp TickMadGlow); no damage of its own
    # (Noah: "make it look like it's flaming in a dark red color") dark red flame from the base blade up, hotter each tier
    dict(persist=1.0, pulse=0.4, particles=True, birth=70.0, alpha=(0.08, 0.45, 0.08), scale2=8.0, fill=(170, 20, 12), keys=((255, 120, 60), (200, 30, 15), (90, 0, 0)), edge=(200, 25, 10)),
    dict(persist=1.1, pulse=0.35, particles=True, birth=100.0, alpha=(0.10, 0.55, 0.10), scale2=9.5, fill=(190, 25, 14), keys=((255, 140, 70), (215, 35, 18), (100, 0, 0)), edge=(220, 30, 12)),
    dict(persist=1.3, pulse=0.3, particles=True, birth=140.0, alpha=(0.12, 0.65, 0.12), scale2=11.0, life=0.8, fill=(215, 35, 18), keys=((255, 170, 90), (230, 45, 20), (120, 0, 0)), edge=(240, 40, 15)),
]
# (the audit: the Madness Sword's first-person static pointed at Saints & Seducers' model - Creation Club content, never required: gone)
crystal_weapon('madness', 'UN_MadnessSword', 'Spellbound Rapier', hip('spellbound'), '1st_kvred',
    SWORD_DESC['ss_none'],
    (900, 10.0, 10))

# (Noah: "make the Dragon Slayer sword one-handed ... use the sword I attached") the Spellsword's wonder: a one-handed sword (the
# Madness Sword's record) with the Ayleid Crystal Saber's model (tools/wonderprops.py slayer); its spell-on-hit, lightning
# slashes and tier in src/Classes.cpp and src/WonderProps.inl
crystal_weapon('slayer', 'UN_DragonSlayer', 'Dragon Slayer', lgs('slayer'), '1st_slayer',
    SWORD_DESC['slayer'],
    (6000, 16.0, 26), 'weap_draugr_gs', 0.75 * 1.5)   # (Noah: "make 2hand attacks faster by 50% only while holding those 2 weapons")

# (Noah: "I want the armor kits for all classes to stay the same visually through all the upgrades ... the upgraded version
# to make the item look enchanted") each class's set glows with its colour from Tier I, stronger each tier (Classes::TickArmour
# lays it on you): Summoner violet, Destruction ember-orange, Alchemist green, Spellsword blue
# (Noah, 2026-11-08: "give the player an aura when the armor is upgraded" - "faint to blazing", in the class's colour) the glow
# grows into an aura: Tier I a faint sheen; Tier II wisps of the class's light rise off you; Tier III a full aura of them;
# each Ascend (up to three steps) burns brighter and thicker - Skyrim's own enchantment shader (its flame-wisp particles)
ARM_COL = [((150, 90, 255), (210, 170, 255)), ((255, 110, 30), (255, 190, 110)), ((90, 230, 60), (190, 255, 150)), ((80, 150, 255), (170, 210, 255))]
for c, (fill, edge) in enumerate(ARM_COL):
    for t in (1, 2, 3, 4, 5, 6):
        d = bytearray(tsub('efsh_enchpurple', 'DATA'))
        g = min(1.0, 0.16 + 0.11 * t)
        d[0x10:0x13] = bytes(min(255, int(x * g)) for x in fill)               # its fill, brighter each step
        d[0x38:0x3B] = bytes(min(255, int(x * min(1.0, 0.4 + 0.12 * t))) for x in edge)   # its edge glow
        struct.pack_into('<f', d, 0x20, 0.75 + 0.15 * t)                    # (persistence)
        struct.pack_into('<f', d, 0x24, max(0.1, 0.5 - 0.08 * t))           # (pulse)
        if t < 2: d[0] = d[0] | 0x08                                        # Tier I: no wisps yet
        else:
            d[0] = d[0] & ~0x08
            struct.pack_into('<f', d, 0x7C, (12.0, 40.0, 55.0, 70.0, 90.0)[t - 2])   # wisps born a second: a few, then a blaze
            struct.pack_into('<f', d, 0x84, 1.4 + 0.15 * (t - 2))           # (each lives ~1.5 s)
            struct.pack_into('<f', d, 0x88, 0.4)
            struct.pack_into('<f', d, 0x8C, 3.0)                            # (off the body)
            struct.pack_into('<3f', d, 0x94, 0.0, 0.0, 6.0 + 2.0 * t)       # (and up)
            struct.pack_into('<3f', d, 0xA0, 0.0, 0.0, 18.0 + 4.0 * t)      # (rising faster as they go)
            struct.pack_into('<2f', d, 0xAC, 1.5 + 0.3 * t, 5.0 + 1.2 * t)  # (their size: born small, swelling)
            for k, rgb in enumerate((edge, fill, tuple(x // 3 for x in fill))): d[0xBC + 4 * k:0xBC + 4 * k + 3] = bytes(rgb)
            struct.pack_into('<3f', d, 0xC8, 0.0, min(1.0, 0.35 + 0.12 * t), 0.0)   # (they fade in and out)
        subs = [sub('EDID', z(f'UN_ArmGlow_{c}_{t}'))]
        for name, hx in T['efsh_enchpurple']['subs']:
            if name == 'EDID': continue
            subs.append(sub(name, bytes(d) if name == 'DATA' else bytes.fromhex(hx)))
        recs['EFSH'].append(record('EFSH', fid('EFSH', f'armglow_{c}_{t}'), subs))
for n, t in enumerate(MAD_GLOW, start=1):
    d = bytearray(tsub('efsh_enchpurple', 'DATA'))
    d[0] = d[0] & ~0x08 if t['particles'] else d[0]
    struct.pack_into('<f', d, 0x20, t['persist'])
    struct.pack_into('<f', d, 0x24, t['pulse'])
    d[0x10:0x13] = bytes(c // 3 for c in t['fill'])       # (Skyrim's purple enchantment's fill, violet through it)
    if t['particles']:
        struct.pack_into('<f', d, 0x7C, t['birth'])
        struct.pack_into('<fff', d, 0xC8, *t['alpha'])
        struct.pack_into('<f', d, 0xB0, t['scale2'])
        for k, rgb in enumerate(t['keys']): d[0xBC + 4 * k:0xBC + 4 * k + 3] = bytes(rgb)
        if 'life' in t: struct.pack_into('<f', d, 0x84, t['life'])
    d[0x38:0x3B] = bytes(t.get('edge', t['fill']))       # its edge glow
    subs = [sub('EDID', z(f'UN_MadShader{n}'))]
    for name, hx in T['efsh_enchpurple']['subs']:
        if name == 'EDID': continue
        subs.append(sub(name, bytes(d) if name == 'DATA' else bytes.fromhex(hx)))
    recs['EFSH'].append(record('EFSH', fid('EFSH', f'madshader{n}'), subs))
    md = bytearray(tsub('mgef_enchfire', 'DATA'))
    struct.pack_into('<I', md, 24, 0)                     # no light of its own
    struct.pack_into('<I', md, 32, 0)                     # nothing on the struck
    struct.pack_into('<I', md, 36, fid('EFSH', f'madshader{n}'))
    struct.pack_into('<I', md, 96, 0)
    struct.pack_into('<I', md, 0, struct.unpack_from('<I', md, 0)[0] | 0x8000)   # hidden in the effects list
    recs['MGEF'].append(record('MGEF', fid('MGEF', f'madglow{n}'), [sub('EDID', z(f'UN_MadGlow{n}')), sub('FULL', z('Madfire')), sub('MDOB', tsub('mgef_enchfire', 'MDOB')),
        sub('DATA', bytes(md)), sub('DNAM', z(''))]))
    en = bytearray(tsub('ench_fire05', 'ENIT'))
    struct.pack_into('<I', en, 0, 0); struct.pack_into('<I', en, 4, struct.unpack_from('<I', en, 4)[0] | 0x1)   # costs no charge
    recs['ENCH'].append(record('ENCH', fid('ENCH', f'madglow{n}'), [sub('EDID', z(f'UN_MadGlowEnch{n}')), sub('OBND', bytes(12)), sub('FULL', z('Madfire')),
        sub('ENIT', bytes(en)), sub('EFID', struct.pack('<I', fid('MGEF', f'madglow{n}'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))


# (Noah: "each sword should have an elemental animation that changes and grows as the sword grows in strength from the tiers
# 1-3") each sword's look per tier: an enchantment of nothing but looks - Visual Animated Enchants' art spilling off the blade
# (set on these effects when the game loads, src/Kit.cpp: our plugin needs no master of theirs) and a glow of the sword's colour
# over it, fuller each tier; from Tier II its sparks fly off the blade
BV_COL = {   # fill, edge, spark keys (bright, mid, dark)
    'alch':     ((150, 40, 255), (200, 110, 255), ((245, 220, 255), (185, 90, 255), (90, 10, 190))),
    'carian':   ((70, 130, 255), (150, 200, 255), ((235, 245, 255), (120, 180, 255), (40, 70, 220))),
    'radiant':  ((235, 238, 255), (255, 255, 255), ((255, 255, 255), (235, 240, 255), (190, 205, 255))),
    'ss_none':  ((220, 30, 20), (255, 80, 50), ((255, 210, 180), (240, 60, 30), (140, 0, 0))),
    'ss_fire':  ((255, 110, 20), (255, 170, 60), ((255, 240, 200), (255, 150, 40), (220, 50, 10))),
    'ss_frost': ((120, 200, 255), (190, 235, 255), ((240, 250, 255), (150, 215, 255), (60, 140, 230))),
    'ss_shock': ((150, 120, 255), (200, 190, 255), ((250, 245, 255), (175, 160, 255), (90, 70, 240))),
    'ss_sun':   ((255, 210, 80), (255, 235, 150), ((255, 252, 230), (255, 215, 90), (230, 150, 20))),
    'ss_blood': ((120, 160, 255), (210, 230, 255), ((245, 250, 255), (170, 200, 255), (70, 110, 230))),   # (Blood made Arcane: blue-white)
    'slayer':   ((255, 190, 70), (255, 225, 140), ((255, 250, 220), (255, 190, 70), (220, 90, 20))),
}
for bkey, (fill, edge, keys) in BV_COL.items():
    for n in (1, 2, 3):
        d = bytearray(tsub('efsh_enchpurple', 'DATA'))
        sparks = n >= 2
        d[0] = d[0] & ~0x08 if sparks else d[0] | 0x08
        struct.pack_into('<f', d, 0x20, (0.55, 0.85, 1.15)[n - 1])     # its glow, fuller each tier
        struct.pack_into('<f', d, 0x24, (0.6, 0.45, 0.3)[n - 1])       # its breathing, steadier
        d[0x10:0x13] = bytes(int(c * (0.22, 0.32, 0.42)[n - 1]) for c in fill)
        d[0x38:0x3B] = bytes(edge)
        if sparks:
            struct.pack_into('<f', d, 0x7C, (0, 70.0, 120.0)[n - 1])
            struct.pack_into('<fff', d, 0xC8, *((0, (0.06, 0.4, 0.06), (0.12, 0.65, 0.12))[n - 1]))
            struct.pack_into('<f', d, 0xB0, (0, 8.0, 11.0)[n - 1])
            struct.pack_into('<f', d, 0x84, 0.8)
            for k, rgb in enumerate(keys): d[0xBC + 4 * k:0xBC + 4 * k + 3] = bytes(rgb)
        subs = [sub('EDID', z(f'UN_BladeLook_{bkey}{n}'))]
        for name, hx in T['efsh_enchpurple']['subs']:
            if name == 'EDID': continue
            subs.append(sub(name, bytes(d) if name == 'DATA' else bytes.fromhex(hx)))
        recs['EFSH'].append(record('EFSH', fid('EFSH', f'bv_{bkey}_{n}'), subs))
        md = bytearray(tsub('mgef_enchfire', 'DATA'))
        struct.pack_into('<I', md, 24, 0); struct.pack_into('<I', md, 32, 0); struct.pack_into('<I', md, 96, 0); struct.pack_into('<I', md, 100, 0)
        struct.pack_into('<I', md, 36, fid('EFSH', f'bv_{bkey}_{n}'))
        struct.pack_into('<I', md, 64, 1)                     # a script effect with no script: looks only, nothing done on a hit
        struct.pack_into('<I', md, 68, 0xFFFFFFFF)
        struct.pack_into('<I', md, 0, struct.unpack_from('<I', md, 0)[0] | 0x8000)   # hidden in the effects list
        # (Noah: "attention to detail for inventory view and descriptions") the look shows on the item card: its name and what
        # this tier adds (no magnitude, duration or area to print)
        tk = 'ss' if bkey.startswith('ss_') else bkey
        struct.pack_into('<I', md, 0, (struct.unpack_from('<I', md, 0)[0] & ~0x8000) | 0x200 | 0x400 | 0x800)
        recs['MGEF'].append(record('MGEF', fid('MGEF', f'bv_{bkey}_{n}'), [sub('EDID', z(f'UN_BladeLookFx_{bkey}{n}')), sub('FULL', z(TIER_NAME[tk] + ' ' + 'I' * n)), sub('MDOB', tsub('mgef_enchfire', 'MDOB')),
            sub('DATA', bytes(md)), sub('DNAM', z(TIER_TEXT[tk][n - 1]))]))
        en = bytearray(tsub('ench_fire05', 'ENIT'))
        struct.pack_into('<I', en, 0, 0); struct.pack_into('<I', en, 4, struct.unpack_from('<I', en, 4)[0] | 0x1)
        recs['ENCH'].append(record('ENCH', fid('ENCH', f'bv_{bkey}_{n}'), [sub('EDID', z(f'UN_BladeLookEnch_{bkey}{n}')), sub('OBND', bytes(12)), sub('FULL', z(TIER_NAME[tk] + ' ' + 'I' * n)),
            sub('ENIT', bytes(en)), sub('EFID', struct.pack('<I', fid('MGEF', f'bv_{bkey}_{n}'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))

# (Noah: "make them required") Vibrant Weapons' own art (its files, read from the mod): the one-handed swords its sword art, the
# Dragon Slayer its greatsword art - each look the element nearest its colour
VW_ART = {k: 'vibrant weapons\\magic\\' + v + '.nif' for k, v in {'alch': 'ShockSwordR', 'carian': 'ShockSwordR', 'radiant': 'FrostSwordR', 'ss_none': 'FireSwordR',
          'ss_fire': 'FireSwordR', 'ss_frost': 'FrostSwordR', 'ss_shock': 'ShockSwordR', 'ss_sun': 'FireSwordR', 'ss_blood': 'FrostSwordR', 'slayer': 'FireGreatsword'}.items()}
# (Noah: "for the weapon effects around the swords in hand use the effects mod we downloaded for that" - "Both layered") each
# look also wears Vibrant Weapons' art along the blade and a
# small light of its colour: an ability of a light effect carrying the art, laid on you while the sword is in your hand
# (src/Classes.cpp TickSwords) - Visual Animated Enchants' shader and art ride on the enchantment above
for bkey, (fill, edge, keys) in BV_COL.items():
    data = bytearray(tsub('ligh', 'DATA'))
    struct.pack_into('<I', data, 4, 180); struct.pack_into('<BBBB', data, 8, *edge, 0)
    recs['LIGH'].append(record('LIGH', fid('LIGH', f'vw_{bkey}'), [sub('EDID', z(f'UN_SwordLight_{bkey}')), sub('OBND', tsub('ligh', 'OBND')), sub('DATA', bytes(data)), sub('FNAM', struct.pack('<f', 0.6))]))
    arto(f'vw_{bkey}', VW_ART[bkey], 1)   # (a hit-effect art, as theirs)
    md = bytearray(bytes.fromhex('001040000000003f58fa030012000000ffffffff00000000091f040000000000000000000000000000000000000000000000003f0000000000000000000000000c000000ffffffff00000000000000000100000000000000ffffffff081f0400061f0400000000008fc2f53d000000000000803f000000000000000000000000000000000000000000000000000000000000000000000000'))
    struct.pack_into('<I', md, 0, struct.unpack_from('<I', md, 0)[0] | 0x8000)   # hidden
    struct.pack_into('<I', md, 8, fid('LIGH', f'vw_{bkey}'))
    struct.pack_into('<I', md, 24, 0)
    struct.pack_into('<I', md, 80, 0)                                              # constant effect
    struct.pack_into('<I', md, 84, 0)                                              # on yourself
    struct.pack_into('<I', md, 92, 0)
    struct.pack_into('<I', md, 96, fid('ARTO', f'vw_{bkey}'))                      # the art on you (Vibrant's: hung on the WEAPON node)
    recs['MGEF'].append(record('MGEF', fid('MGEF', f'vw_{bkey}'), [sub('EDID', z(f'UN_SwordArtFx_{bkey}')), sub('FULL', z('Blade')), sub('MDOB', struct.pack('<I', 0)),
        sub('DATA', bytes(md)), sub('DNAM', z(''))]))
    recs['SPEL'].append(record('SPEL', fid('SPEL', f'vw_{bkey}'), [sub('EDID', z(f'UN_SwordArt_{bkey}')), sub('OBND', bytes(12)), sub('FULL', z('Blade')),
        sub('DESC', z('')), sub('SPIT', struct.pack('<IIIfIIffI', 0, 0, 4, 0.0, 0, 0, 0.0, 0.0, 0)),   # an ability, constant, on yourself
        sub('EFID', struct.pack('<I', fid('MGEF', f'vw_{bkey}'))), sub('EFIT', struct.pack('<fII', 0.0, 0, 0))]))

# (the Reliquary's grey likenesses of the Bane and the Slayer: their models are Light Greatswords' own, so not greyed in the file -
# a pale ghostly membrane laid over them instead, src/Echo.cpp)
d = bytearray(tsub('efsh_enchpurple', 'DATA'))
d[0] |= 0x08                                     # no particles
struct.pack_into('<f', d, 0x20, 1.6)             # a full, steady fill
struct.pack_into('<f', d, 0x24, 0.0)
d[0x10:0x13] = bytes((150, 155, 165))
d[0x38:0x3B] = bytes((210, 215, 225))
subs = [sub('EDID', z('UN_HallGhostShader'))]
for name, hx in T['efsh_enchpurple']['subs']:
    if name == 'EDID': continue
    subs.append(sub(name, bytes(d) if name == 'DATA' else bytes.fromhex(hx)))
recs['EFSH'].append(record('EFSH', fid('EFSH', 'hallghost'), subs))

# (Noah: "shadowflame ... should set enemies on fire with purple flames") Shadowflame on its victim: Skyrim's enchantment fire
# wrapped round the whole body in violet - a burning membrane and a thick stream of violet flames rising off it - for as
# long as it burns (src/Classes.cpp Shadowburn lays it on)
d = bytearray(tsub('efsh_enchfire', 'DATA'))
d[0] &= ~0x08                                   # its flames on
struct.pack_into('<f', d, 0x20, 1.4)            # the membrane bright
struct.pack_into('<f', d, 0x24, 0.25)
d[0x10:0x13] = bytes((150, 40, 255))
d[0x38:0x3B] = bytes((200, 90, 255))
struct.pack_into('<f', d, 0x7C, 260.0)          # many flames
struct.pack_into('<fff', d, 0xC8, 0.2, 0.9, 0.2)
struct.pack_into('<f', d, 0xB0, 14.0)
struct.pack_into('<f', d, 0x84, 0.9)
for k, rgb in enumerate(((245, 215, 255), (170, 70, 255), (80, 0, 170))): d[0xBC + 4 * k:0xBC + 4 * k + 3] = bytes(rgb)
subs = [sub('EDID', z('UN_ShadowburnShader'))]
for name, hx in T['efsh_enchfire']['subs']:
    if name == 'EDID': continue
    subs.append(sub(name, bytes(d) if name == 'DATA' else bytes.fromhex(hx)))
recs['EFSH'].append(record('EFSH', fid('EFSH', 'shadowburn'), subs))

# (Noah: "for all the bows' animations in hand they should be high quality and flow with the model of the bow, not circulating
# orbs") each bow's element as a living skin over the whole bow - the enchantment shader's membrane and its particles running
# over the model as it bends (src/Bows.inl TickSkin lays it on the bow in your hands)
BOW_SKIN = {
    'fire':  dict(fill=(255, 110, 30), edge=(255, 70, 10), keys=((255, 230, 150), (255, 120, 30), (160, 20, 0))),
    'frost': dict(fill=(140, 210, 255), edge=(90, 180, 255), keys=((240, 250, 255), (150, 210, 255), (40, 110, 220))),
    'shock': dict(fill=(150, 170, 255), edge=(120, 140, 255), keys=((255, 255, 255), (170, 190, 255), (70, 80, 240))),
    'wind':  dict(fill=(170, 255, 225), edge=(120, 240, 210), keys=((240, 255, 250), (160, 245, 220), (60, 170, 150))),
    'sun':   dict(fill=(255, 215, 110), edge=(255, 190, 60), keys=((255, 255, 220), (255, 210, 100), (200, 120, 20))),
    'blood': dict(fill=(200, 10, 20), edge=(230, 20, 30), keys=((255, 120, 120), (200, 20, 30), (90, 0, 5))),
    'void':  dict(fill=(170, 60, 255), edge=(190, 80, 255), keys=((240, 210, 255), (170, 70, 255), (60, 0, 140))),
}
for el, t in ((k, v) for k, v in BOW_SKIN.items() if k == 'void'):   # (only the void skin is left: the Violet Wyrm's glow)
    d = bytearray(tsub('efsh_enchpurple', 'DATA'))
    d[0] = d[0] & ~0x08
    struct.pack_into('<f', d, 0x20, 1.1)
    struct.pack_into('<f', d, 0x24, 0.35)
    d[0x10:0x13] = bytes(c // 3 for c in t['fill'])
    struct.pack_into('<f', d, 0x7C, 80.0)
    struct.pack_into('<fff', d, 0xC8, 0.08, 0.5, 0.08)
    struct.pack_into('<f', d, 0xB0, 6.5)
    struct.pack_into('<f', d, 0x84, 0.7)
    for k, rgb in enumerate(t['keys']): d[0xBC + 4 * k:0xBC + 4 * k + 3] = bytes(rgb)
    d[0x38:0x3B] = bytes(t['edge'])
    subs = [sub('EDID', z(f'UN_BowSkin_{el}'))]
    for name, hx in T['efsh_enchpurple']['subs']:
        if name == 'EDID': continue
        subs.append(sub(name, bytes(d) if name == 'DATA' else bytes.fromhex(hx)))
    recs['EFSH'].append(record('EFSH', fid('EFSH', f'bowskin_{el}'), subs))

recs['ACTI'].append(record('ACTI', fid('ACTI', 'purplefire'), [sub('EDID', z('UN_PurpleFire')), sub('OBND', struct.pack('<6h', -120, -120, -40, 120, 120, 140)),   # (the Horn's dragon's violet breath)
    sub('MODL', z('CurseOfAlduin\\fx\\purplefire.nif')), sub('FNAM', b'\0\0')]))
for e in ('fire', 'frost', 'shock', 'wind', 'sun', 'blood', 'void', 'poison', 'gold'):  # (Noah: the arrows land as impact grenades, in the bow's own colour - tools/elemburst.py)
    recs['ACTI'].append(record('ACTI', fid('ACTI', 'burst_' + e), [sub('EDID', z('UN_Burst_' + e)), sub('OBND', struct.pack('<6h', -130, -130, -20, 130, 130, 140)),
        sub('MODL', z('CurseOfAlduin\\fx\\burst_' + e + '.nif')), sub('FNAM', b'\0\0')]))

# (the classes overhaul) the Summoner's command: hidden perks that set how many summons you may hold (2, 3, 4) -
# Twin Souls' own entry point (Mod Commanded Actor Limit, Set Value), without its conditions, above it in priority
# (Noah: 2 to start, one more for every 2 freed altars, up to 7; 8 with the Conjurer's Command Mead)
for n in (2, 3, 4, 5, 6, 7, 8):
    recs['PERK'].append(record('PERK', fid('PERK', f'summon_cap_{n}'), [
        sub('EDID', z(f'UN_SummonCap{n}')), sub('FULL', z(f'Command of {n}')), sub('DESC', z(f'You can hold {n} summons at once.')),
        sub('DATA', bytes([0, 0, 1, 0, 1])), sub('PRKE', bytes([2, 0, 10 + n])), sub('DATA', bytes([68, 1, 2])),
        sub('EPFT', bytes([1])), sub('EPFD', struct.pack('<f', float(n))), sub('PRKF', b'')]))

# (Noah: "I don't want stagger and lurch - it's annoying to get spammed with movement blocking attacks") the night's
# dead never stagger you: Mod Incoming Stagger (entry point 33), Multiply Value x0, when the attacker is in the undead
# faction (tab 1: the attacker). Given to the player at load (src/Director.cpp)
_ctda = struct.pack('<B3xfHHIIIIi', 0, 1.0, 71, 0, fid('FACT', 'undead'), 0, 0, 0, -1)  # GetInFaction(UN_UndeadFaction) == 1, run on the subject (the attacker)
recs['PERK'].append(record('PERK', fid('PERK', 'steady'), [
    sub('EDID', z('UN_SteadyAgainstTheDead')), sub('FULL', z('Steady')), sub('DESC', z("The night's dead never stagger you.")),
    sub('DATA', bytes([0, 0, 1, 0, 1])), sub('PRKE', bytes([2, 0, 0])), sub('DATA', bytes([33, 3, 2])),
    sub('PRKC', bytes([1])), sub('CTDA', _ctda),
    sub('EPFT', bytes([1])), sub('EPFD', struct.pack('<f', 0.0)), sub('PRKF', b'')]))

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

# (Noah) the Dance Of Death's song: 20 s cut from a track Noah supplied - not shipped with the mod (his own copy is
# installed beside it: Data\Sound\fx\CurseOfAlduin\dance\dance_of_death.wav); the record plays nothing when it's absent
recs['SNDR'].append(record('SNDR', fid('SNDR', 'dance_song'), [
    sub('EDID', z('UN_dance_song')), sub('CNAM', tsub('sndr_ui', 'CNAM')), sub('GNAM', struct.pack('<I', 0x000172A1)),
    sub('ANAM', z('fx\\CurseOfAlduin\\dance\\dance_of_death.wav')), sub('ONAM', tsub('sndr_ui', 'ONAM')), sub('LNAM', tsub('sndr_ui', 'LNAM')),
    sub('BNAM', struct.pack('<bBBBH', 0, 3, 128, 0, 0))]))

# music: Grimmvar's fight and a city freed play through Skyrim's own music system (music slider, ducking, no clash
# with the combat music: higher priority), from tracks cut out of the supplied library
for key, edid, track, flags, prio, fade in (('boss', 'UN_MUSBoss', 'Data\\Music\\CurseOfAlduin\\mus_boss.wav', 0x24, 1, 3.0),
                                           ('freed', 'UN_MUSFreed', 'Data\\Music\\CurseOfAlduin\\mus_freed.wav', 0x01, 1, 1.5),
                                           ('intro', 'UN_MUSIntro', 'Data\\Music\\CurseOfAlduin\\mus_intro.wav', 0x21, 1, 2.0),
                                           ('night', 'UN_MUSNight', 'Data\\Music\\CurseOfAlduin\\mus_night.wav', 0x04, 5, 6.0)):  # (Noah: the night's own song, low under it all; the file is his, deployed only)
    recs['MUST'].append(record('MUST', fid('MUST', key), [sub('EDID', z(edid + 'Track')), sub('CNAM', struct.pack('<I', 0x6ED7E048)), sub('ANAM', z(track))]))
    recs['MUSC'].append(record('MUSC', fid('MUSC', key), [sub('EDID', z(edid)), sub('FNAM', struct.pack('<I', flags)),
        sub('PNAM', struct.pack('<HH', prio, 10000)), sub('WNAM', struct.pack('<f', fade)), sub('TNAM', struct.pack('<I', fid('MUST', key)))]))
# the undead step only with the mod's own footsteps (Audio.cpp plays them on each footfall): their skins carry an empty
# footstep set, so the race's own (vanilla) steps stay quiet
recs['FSTS'].append(record('FSTS', fid('FSTS', 'silent'), [sub('EDID', z('UN_SilentFootsteps')), sub('XCNT', struct.pack('<5I', 0, 0, 0, 0, 0)), sub('DATA', b'')]))
# loading screens of our own among Skyrim's: the shrine or the lore book turning, and a hint
LOADING_TIPS = [
    "Alduin shouted three words into the earth at Helgen - SLEN TIID VO, Flesh, Time, Undo - and every barrow answered.",
    "Alduin's mist lies on the door of Sovngarde. The dead cannot pass to Shor's hall, so they rise instead.",
    "Free six cities and your path's secret wakes. When it is won, the Greybeards call - your wonder waits beneath High Hrothgar.",
    "An Echo Altar stands in every city. Upgrade your kit, take a relic or a perk - the night does not wait while you bargain.",
    "Gloomstalkers shriek to wound everyone near them. Kill them first.",
    "Necromancers hang back and raise more dead. Kill them first.",
    "Hold right mouse to gather a spell's full power in both hands. Let go too soon and the power gutters out.",
    "When the runic circle wakes round the altar, stand inside it until its count runs out. The dead come harder while you do.",
    "Solitude's bards remember a song the dead cannot resist. Free their city and the Dance of Death is yours.",
    "Wake a city's Echo Altar and its dead sleep again: no curse-dead rise inside a freed city, by night or by day.",
    "Three dragons carry Alduin's words. Slay them and only his echo is left, on the summit of the Throat of the World.",
    "The altars take no gold - only the Cursed Essence the dead leave behind.",
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
# call; stage 20+i the story of trial i when it is taken up (any three, in any order); stage 100 the city's Dragon Priest; stage
# 200 when the city is freed. Objectives: 0 choose a trial at the altar, 1-5 the trials, 6 slay the Dragon Priest (src/Curse.cpp
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
    # (Noah: "update the Echo Altar's quest description") the journal shows the latest entry only: each one opens with what
    # the altar asks as a whole, then the trial in hand
    # (Noah: "much shorter text, and the steps you still need to do in view - no check mark until it's done") a line or two;
    # the steps themselves are the objectives below (ticked only once won)
    overview = (f"Win the four trials at the Echo Altar of {c['town']}, in any order. Then {boss['name']}, its Dragon Priest, rises - "
                f"slay it to free the city.")
    stages = [(10, overview)]
    stages += [(20 + i, overview + f"\n\nNow: {tr['title']} - {fill(tr['goal'], c)}.") for i, tr in enumerate(CU['trials'])]
    stages += [(100, f"The trials are won. {boss['name']} walks the night near the Echo Altar of {c['town']} - slay it and the altar wakes.")]
    for st, text in stages:
        assert '{' not in text, text
        subs += [sub('INDX', struct.pack('<HBB', st, 0, 0)), sub('QSDT', b'\x00'), sub('CNAM', z(text))]
    subs += [sub('INDX', struct.pack('<HBB', 200, 0, 0)), sub('QSDT', b'\x01'), sub('CNAM', z(c['town'] + ' is free. ' + CU['liftedText'].replace('{city}', c['town'])))]
    objectives = [f"Choose a trial at the Echo Altar of {c['town']}"] + [f"{tr['title']}: {fill(tr['goal'], c)}" for tr in CU['trials']] + [f"Slay {boss['name']}, the Dragon Priest"]
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
    # (Noah: "clean up and simplify ... the Curse of Alduin lore book so it's easier to understand") short chapters, plain
    # words: what happened, what rises, what to do about it, and how it ties to the Dragonborn's own story
    img = lambda n, w, h: f"<p align='center'><img src='img://Textures/CurseOfAlduin/Book/{n}.png' width='{w}' height='{h}'></p>"
    out = [f"<font face='$HandwrittenFont'>", img('sigil', 200, 200),
           "<p align='center'><font size='30'>THE CURSE OF ALDUIN</font></p>",
           "<p align='center'>Written at High Hrothgar by a Greybeard who broke his silence, for whoever lived through Helgen</p>",
           "[pagebreak]", "<p align='center'><font size='24'>I. What Happened</font></p>", img('helgen', 300, 176),
           "<p>When Alduin rose over Helgen, he shouted three Words into the earth: <b>SLEN TIID VO</b> - Flesh, Time, Undo. "
           "It is the shout that raises the dead.</p>",
           "<p>His mist also covers the door of <b>Sovngarde</b>, so the dead of Skyrim cannot pass on. They lie restless in the ground. "
           "By day they sleep. When the sun goes down, they rise.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>II. The Dead</font></p>", img('graves', 300, 176),
           "<p>Each night the dead come in <b>waves</b>, more of them and harder to kill as the nights go on. They claw up out of open ground - "
           "roads and fields, never inside a house - and come at you from every side. Dawn comes only when the last wave falls.</p>",
           "<p>The curse's whole host walks with them, each under its own name. Mihail's zombies and the Thriceborn come first with the Rag Draugr, rotten skeletons and plague wolves; "
           "then Bonemen, skeleton mages and the necromancers who serve the curse - kill those first, for they raise more; then Hulking Draugr, Draugr Liches, Draman Brutes, Ash Spawn and werewolves; "
           "and at the last the Daedra and the Overlords.</p>",
           "<p>Some nights the noise you make draws a hunter that comes through a portal and pounces. "
           "The Curse's champions and great ones step out of portals too - and their names burn gold over their heads.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>III. The Echo Altars</font></p>", img('wards', 300, 176),
           "<p>Long ago the <b>Tongues</b>, the first masters of the Voice, raised an altar in every hold to hold Alduin's shout back. "
           "Every mage who fell in the Dragon War gave a last spell to them, which is why an altar still has spells, bows and gifts to give.</p>",
           "<p>Alduin's return woke the altars only halfway. Each one is held silent by a <b>Dragon Priest</b>, still bound by his old oath to Alduin.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>IV. Waking an Altar</font></p>",
           "<p>Stand at an altar and it sets you <b>trials</b>: hold its rune circle, cull the dead in its streets, hold the altar through a night's waves, "
           "and pass the trial of your own path. "
           "Finish them and its Dragon Priest rises at night. Kill him and the altar wakes.</p>",
           "<p>A woken altar makes its city a <b>sanctuary</b>: none of the curse's dead rise inside it, by night or by day. Step outside its walls and they find you again.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>V. The Dragonborn's Road</font></p>",
           "<p>The Curse walks beside your own story; it does not replace it.</p>",
           "<p><b>Riverwood's</b> altar, nearest Helgen, wakes first. <b>Whiterun's</b> answers your first dragon soul and gives you a dragon to ride - and its Thu'um, "
           "<i>Dragon's Updraft</i>. <b>Solitude's</b> bards give you the <i>Dance of Death</i>.</p>",
           "<p>Free six cities and each path's <b>secret</b> wakes. Win it and the Greybeards call you to <b>High Hrothgar</b>: your path's <b>wonder</b> waits on its pedestal "
           "in the Reliquary beneath the great hall.</p>",
           "<p>With all four wonders won - alone or with friends - Alduin's three Words wake as <b>dragons</b>: <b>SLEN</b> in the Reach, <b>TIID</b> at Labyrinthian and "
           "<b>VO</b> on Mount Anthor. Slay all three, wake every altar, and only his <b>Echo</b> is left, on the summit of the Throat of the World. End it, and the Curse is broken.</p>",
           "[pagebreak]", "<p align='center'><font size='24'>VI. The Voices of the Holds</font></p>",
           "<p><b>Whiterun</b>: <i>Dragon's Updraft</i>. A whirlwind lifts you high - and a dragon to carry you.</p>",
           "<p><b>Solitude</b>: <i>Dance of Death</i>. The dead near you dance, helpless, and take half again as much harm. Once a wave.</p>"]
    out += [
            "[pagebreak]", f"<p>{S['curse']['freedAll']}</p>", "<p align='right'><i>- written in the silence of High Hrothgar</i></p>", "</font>"]
    return '\n'.join(out)

def guide_text():
    p = lambda t: f"<p>{t}</p>"
    h = lambda t: f"<p align='center'><font size='24'>{t}</font></p>"
    out = ["<font face='$HandwrittenFont'>", "<p align='center'><font size='28'>A SURVIVOR'S GUIDE TO THE LONG NIGHT</font></p>",
           "<p align='center'>Notes left at the Sleeping Giant Inn by one who made it this far. Read them before dusk.</p>",
           "[pagebreak]", h("Day and Night"),
           p("A day lasts about twenty minutes. By day the dead sleep: rest, explore, spend what you earned. "
             "At dusk they rise in <b>waves</b>, clawing up out of the ground at the same places every night. Each wave brings a few more than the last, and each night a few more again: "
             "the first night is three small waves, and a wave is added every second night, up to eight. The night does not end until the last wave is down - then dawn breaks."),
           p("You cannot rest while the dead walk - but once they have risen, the wait menu can skip the whole night (it does not count)."),
           "[pagebreak]", h("Cursed Essence"),
           p("Every risen thing you kill leaves Cursed Essence. A headshot leaves more, and a kill with a blade or a spell more still. "
             "Cursed Essence is the only coin the Echo Altars take."),
           "[pagebreak]", h("The Echo Altar"),
           p("Every city has one. Look into its star and press Activate. Its menu does not stop the night. Move through it with the D-pad (or W and S) and take a row with A (or Enter); B leaves."),
           p("<b>Your class</b> - Summoner, Battle-mage, Ranger or Spellsword, chosen at the top of the altar. Each keeps its own gear and spells when you change."),
           p("<b>Your kit</b> - every item your class carries, with its tier and the price of its next upgrade: Tier I, Tier II, Tier III (Legendary), then Ascends."),
           p("<b>The Armory</b> - more of your class's spells, summons or bows open as you free cities."),
                      p("<b>Relics</b> - every Dragon Priest you slay leaves a relic for your class: take it at any altar."),
           p("<b>Perks</b> - one of each a life: death takes them all."),
           p("<b>Trials</b> - the quest that wakes the altar. It sets four; win all four, in any order (step over to them with D-pad left or right, or A and D). "
             "Then the city's Dragon Priest rises at night near the altar. Slay it and the night ends at once - dawn breaks and the city is safe for good. Skyrim's difficulty sets how hard the trials are."),
           "[pagebreak]", h("The Four Trials"),
           p("<b>Ward of the Tongues</b> - stand in the runic circle round the altar while the dead come faster. Step out and the count waits for you."),
           p("<b>The Culling</b> - kill a number of the risen in the city."),
           p("<b>Your class's trial</b> - the Summoner's summons make the kills, the Battle-mage kills with two elements, the Ranger from thirty metres, the Spellsword with a blade and a spell in the other hand."),
           p("<b>The Long Night's Breath</b> - stay by the altar while the night's waves are cleared. On Expert and above, never below half health."),
           "[pagebreak]", h("Spells"),
           p("Tap the attack to throw a bolt. Hold it, and both hands gather the spell's full power: the longer you hold, the harder it strikes. "
             "Let go too soon and the power gutters out. Magicka comes back fast."),
           "[pagebreak]", h("Shouts"),
           p("Two cities teach a Thu'um of their own. Free <b>Whiterun</b> and <i>Dragon's Updraft</i> is yours; free <b>Solitude</b> and its bards give you the <i>Dance of Death</i>. Equip them like any shout. A shout's cooldown shows on the rim of your stat plate."),
           "[pagebreak]", h("The Dragon"),
           p("Wake Whiterun's altar and a dragon will answer you. Jump and press Activate in mid-air: it swoops down and you take hold of its harness."),
           p("Steer with the stick or WASD. Forward dives, back climbs. Attack to make it breathe fire on the dead below - only the dead, never your friends. "
             "At the altar you can make deeper bonds with it."),
           "[pagebreak]", h("The Reliquary"),
           p("Behind a sealed door in the great hall of <b>High Hrothgar</b> lies the Reliquary of the Tongues: four pedestals, one for each path's wonder. "
             "It opens the first time the Greybeards call you."),
           p("Win your path's secret and the Greybeards' shout rolls across Skyrim - your <b>wonder</b> waits on its pedestal. Go and take it up. "
             "A wonder answers only to its own path: change class and come back to claim another."),
           "[pagebreak]", h("The Wonder Weapons"),
           p("<b>Horn of the Spectral Wyrm</b> (Summoner) - blow it and a spectral dragon fights at your side."),
           p("<b>Tome of the Six Fires</b> (Battle-mage) - one cast looses the top spell of all six of your elements on the dead round you, every 8 seconds."),
           p("<b>Subsuming Vortex</b> (Ranger) - every arrow tears open a black hole that drags the dead in, then bursts."),
           p("<b>Dragon Slayer</b> (Spellsword) - two tiers above your blade, and every hit carries the last Spellsword spell you held."),
           p("When the three dragons fall, every wonder wakes to its <b>Legendary</b> form."),
           "[pagebreak]", h("The Three Dragons"),
           p("With all four wonders won (a party pools theirs), Alduin's three words wake as dragons, each at its own place: <b>SLEN</b> - Slenvahrin, at the cult's old shrine in the Reach; "
             "<b>TIID</b> - Tiidrah, in the courtyard of Labyrinthian; <b>VO</b> - Vonahlok, on Mount Anthor. Your compass marks them."),
           p("Go there at night. The dead rise round you in waves; when the last wave falls, the dragon comes. Slay it and its word is silenced for good, its power yours: "
             "<b>SLEN</b> - everything you wield hits 15% harder; <b>TIID</b> - the night's blows hurt you 20% less; <b>VO</b> - your magicka returns half again as fast."),
           p("Leave the place, or let dawn come, and the siege falls quiet until you return."),
           "[pagebreak]", h("The Echo of Alduin"),
           p("With the three dragons dead and every Echo Altar awake, the <b>Echo of Alduin</b> waits on the summit of the Throat of the World. Climb it at night: the dead of all Skyrim answer him one last time. "
             "Kill the Echo and the Curse of Alduin is broken - the nights are only nights again."),
           "[pagebreak]", h("The Hall of the Awakened"),
           p("With the curse broken, the Reliquary becomes the <b>Hall of the Awakened</b>. Its stations:"),
           p("<b>Alduin's Bane</b> - a blade of dragon bone: every blow that lands calls a great meteor down on the one struck, and a heavy swing raises a bright white whirlwind round you for 5 seconds that throws every one of the dead that comes near far away. Its kills fill it with souls; full, Alduin's shade rains fireballs round you. "
             "<b>The First Dragonborn's Regalia</b> - while you wear its robes, everything you wield hits 25% harder, every blow on you is 25% softer, and your magic costs a quarter less and returns faster. "
             "<b>The Bone-Wyrm's Skull</b> - your dragon wears Alduin's bones (touch it again to change back)."),
           p("<b>Brazier of the Endless Night</b> - while it burns the curse comes back and never sleeps: each night's last wave brings the next night at once. A freed city is still safe. "
             "<b>Seat of the Awakened</b> - begin again: every city falls back under the curse and the nights start over, the dead half again as tough each cycle. "
             "Your wonders, powers and gear stay yours; free the altars again and the Echo returns."),
           "[pagebreak]", h("Whispers of the Altars"),
           p("Each path keeps a secret, and it is the only road to the path's <b>wonder</b>. It wakes once six cities are free, and nobody will tell you what to do. Stand at a freed altar and its <b>whispers</b> show in its menu - another line for each step you find. When it is won, the Greybeards call you to High Hrothgar. "
             "Each step is found at a freed altar, never in the same city as the step before. These are the first whispers:"),
           ] + [p(f"<b>{ {'summoner': 'The Summoner', 'destruction': 'The Battle-mage', 'ranger': 'The Alchemist', 'spellsword': 'The Spellsword'}[e['cls']] }</b> - {e['steps'][0]['hint']}") for e in S['eggs']['rows']] + [
           "[pagebreak]", h("Event Nights and Themed Waves"),
           p("From night 8, about one night in eight is an <b>event night</b>: a Goblin Raid, a Harpy Swarm, Armored Champions or a Blood Fog. The whole night belongs to it - and it pays <b>double Cursed Essence</b>, with the dead coming twice as fast."),
           p("On ordinary nights, from the third wave, about one wave in six is <b>themed</b> the same way. Its name shows on the wave dial under the compass."),
           p("About one night in five, halfway through, <b>something stirs in the dark</b>: a mini-boss, with spoils for everyone in the party."),
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

# ---- (Noah: "a guide book for each class that tracks progression") the four class guides (sheet: guides), given when the
# class is chosen; read in the Codex (src/Hud.cpp), which adds the live 'Your Progress' page. The book's own text is the
# same chapters, should the Codex ever not show.
G = S['guides']
GUIDE_KEEP = ['The Dragon', 'The Reliquary', 'The Wonder Weapons', 'The Three Dragons', 'The Echo of Alduin', 'The Hall of the Awakened',
              'Whispers of the Altars', 'Event Nights and Themed Waves', 'Falling']   # (the old Survivor's Guide's chapters still true)
def guide_chapters(cls):
    out = [(c['title'], '', c['paras']) for c in cls['chapters']]
    out.insert(1, (G['progressTitle'], '@progress', []))   # (second: right after who you are)
    out += [(c['title'], '', c['paras']) for c in G['shared'][:-1]]
    return out
def guide_tail():
    return [(c['title'], '', c['paras']) for c in G['shared'][-1:]]
def class_book_text(cls, extra):
    out = ["<font face='$HandwrittenFont'>", f"<p align='center'><font size='28'>{cls['book'].upper()}</font></p>"]
    for title, image, paras in guide_chapters(cls) + extra + guide_tail():
        if image == '@progress':
            continue
        out += ['[pagebreak]', f"<p align='center'><font size='24'>{title}</font></p>"] + [f'<p>{q}</p>' for q in paras]
    return '\n'.join(out + ['</font>'])

# ---- Stage 2 (sheet: echo, src/Echo.cpp): the Echo Stones' pages, the hunt's globals, the Reliquary under High Hrothgar
E = S['echo']
WON = {w['id']: w for w in E['wonders']}
for key in ['echo_pages', 'echo_parts', 'echo_forged', 'echo_held', 'echo_words'] + ['echo_kill_' + q['id'] for q in E['parts'] if q['how'] == 'kill']:
    recs['GLOB'].append(record('GLOB', fid('GLOB', key), [sub('EDID', z('UN_' + ''.join(w.capitalize() for w in key.split('_')))), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))

# ---- (Noah) the hidden quests (sheet: eggs, src/Eggs.cpp): a global per quest (its step and progress), one for the gifts
# won, and a lesser power for each summon (and Winterhold's beam) - the power does nothing itself: Eggs::OnSpellCast does
for e in S['eggs']['rows']:
    for k, ed in (('egg_' + e['id'], 'UN_Egg_' + e['id']), ('egg_' + e['id'] + '_town', 'UN_Egg_' + e['id'] + '_Town')):
        recs['GLOB'].append(record('GLOB', fid('GLOB', k), [sub('EDID', z(ed)), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))
recs['GLOB'].append(record('GLOB', fid('GLOB', 'egg_gifts'), [sub('EDID', z('UN_EggGifts')), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))
for k, e in (('ritual_souls', 'UN_RitualSouls'), ('ritual_stand', 'UN_RitualStand')):   # (the staffs' last step: souls fed to each, the one on its stand)
    recs['GLOB'].append(record('GLOB', fid('GLOB', k), [sub('EDID', z(e)), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))
recs['GLOB'].append(record('GLOB', fid('GLOB', 'test_kit'), [sub('EDID', z('UN_TestKit')), sub('FNAM', b'f'), sub('FLTV', struct.pack('<f', 0.0))]))  # (Noah's test characters)

def how_text(q):
    w = WON[q['wonder']]['label']
    if q['how'] == 'boss':
        return f"<b>{q['label']}</b> (for {w}): it falls from {q['town']}'s Dragon Priest when the altar there is freed."
    if q['how'] == 'relic':
        return (f"<b>{q['label']}</b> (for {w}): a reliquary burning with blue Tongue-fire waits at <b>{q['site']}</b>. It wakes only at night: "
                f"come near after dusk and its guardians rise. Kill them all, then walk up to the reliquary and the part is yours.")
    return (f"<b>{q['label']}</b> (for {w}): at <b>{q['site']}</b>, at night, kill the dead that come for you near its door. "
            f"A counter shows how many more it needs; when it is full, the part is yours.")

def page_text(town):
    # (the story) each freed altar's page is a Tongue's own writing: Jurgen Windcaller's followers, on the city's Dragon Priest
    # and the words cut into its altar's heart-stone (the staffs' parts hunt it once described is retired)
    pg = E['pages'][town]
    boss = S['curse']['bosses'].get(town, {})
    sh = next((h for h in S['shouts']['rows'] if h.get('town') == town), None)
    out = ["<font face='$HandwrittenFont'>", f"<p align='center'><font size='26'>THE BROKEN WORDS</font></p><p align='center'><font size='22'>{town}</font></p>",
           f"<p><i>{pg['echo']}</i></p>", "<p align='center'>~</p>"]
    if boss:
        out.append(f"<p>The Dragon Priest of {town}: <b>{boss['name']}</b>. {boss['text']}</p>")
    if sh:
        out.append(f"<p>The words on the heart-stone: <b>{' '.join(sh['words'])}</b> ('{' '.join(sh['translations'])}') - <i>{sh['label']}</i>. "
                   "Activate the woken altar and choose <b>Learn the Thu'um</b>: the runes give the shout up to you, as a Word Wall would.</p>")
    out += [f"<p align='right'><i>- {pg['by']}</i></p>", "</font>"]
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
    rel_acti('rel_ped_' + w['id'], w['label'], 'Take', 'pedestal.nif', 60)   # (Act II: each class's wonder waits here once its secret is won)
for key, model, r in [('rel_room', 'room.nif', 640), ('rel_relic', 'relic.nif', 80), ('rel_brazier', 'tonguefire.nif', 60), ('rel_banner', 'banner.nif', 120)] + [('rel_gem_' + w['id'], f"gem_{w['id']}.nif", 8) for w in E['wonders']]:
    recs['STAT'].append(record('STAT', fid('STAT', key), [sub('EDID', z('UN_' + key)), sub('OBND', struct.pack('<6h', -r, -r, 0, r, r, r)),
        sub('MODL', z(RQ + model)), sub('DNAM', struct.pack('<fI', 90.0, 0))]))
# (Noah: "I'm still not seeing the portal") the Reliquary's portal: an activator with no name (nothing to press - you walk in), so it
# draws the way the flask bursts do
recs['ACTI'].append(record('ACTI', fid('ACTI', 'rel_portal'), [sub('EDID', z('UN_rel_portal')), sub('OBND', struct.pack('<6h', -130, -20, 0, 130, 20, 360)),
    sub('MODL', z(RQ + 'portal.nif')), sub('FNAM', b'\0\0')]))
# (Noah: "just use High Hrothgar's building") the wonders and Alduin's Bane float in the great hall to be taken up, each in a soft
# glow, and a fiery pillar holds the Hall's rewards: nameless activators (no prompt of Skyrim's - src/Echo.cpp aims and takes them)
# (Noah: "skeleton dragon horn is from sketchfab so replace that with a spell model since its conjuration anyways") the Bone-Throne
# Horn floats as a spell: its orb of green grave-fire (our own, tools/orbfx.py), not the horn
for key, model, r in (('hf_gravecaller', 'CurseOfAlduin\\wonder\\bonehorn_show.nif', 40), ('hf_frostbinder', 'Weapons\\StaffofMagnus\\StaffofMagnus.nif', 80),
                      ('hf_wolfcrown', 'apocnew\\restorationarcane_projectile_withershins.nif', 20), ('hf_barrowhorn', lgs('slayer'), 60),
                      ('hf_bane', lgs('bane'), 90), ('hf_glow', 'CurseOfAlduin\\fx\\hall_glow.nif', 60),
                      ('hf_glowtall', 'CurseOfAlduin\\fx\\hall_glow_tall.nif', 120), ('hf_pillar', 'CurseOfAlduin\\fx\\pillar_fire.nif', 120)):
    recs['ACTI'].append(record('ACTI', fid('ACTI', key), [sub('EDID', z('UN_' + key)), sub('OBND', struct.pack('<6h', -r, -r, -r, r, r, r * 2)),
        sub('MODL', z(model)), sub('FNAM', b'\0\0')]))
for k in ('gravecaller', 'frostbinder', 'wolfcrown', 'barrowhorn', 'bane'):  # (greyed out, until it's yours: tools/modelconv/ghostnif.cpp; the Staff of Magnus's made on the machine from the game's own)
    recs['ACTI'].append(record('ACTI', fid('ACTI', 'hg_' + k), [sub('EDID', z('UN_hg_' + k)), sub('OBND', struct.pack('<6h', -80, -80, -80, 80, 80, 160)),
        sub('MODL', z({'bane': lgs('bane'), 'barrowhorn': lgs('slayer'), 'gravecaller': 'CurseOfAlduin\\wonder\\ghost_bonehorn.nif'}.get(k, 'CurseOfAlduin\\wonder\\ghost_' + k + '.nif'))), sub('FNAM', b'\0\0')]))   # (the Bane and the Slayer: Light Greatswords' models)
# Stage 5 (src/Echo.cpp Hall): the Hall of the Awakened's stations, and Alduin's Fang
for key, name, verb in (('hall_fang', "Alduin's Bane", 'Take'), ('hall_robes', "The First Dragonborn's Regalia", 'Take'),
                        ('hall_skull', "The Bone-Wyrm's Skull", 'Bind'), ('hall_endless', 'Brazier of the Endless Night', 'Kindle'),
                        ('hall_throne', 'Seat of the Awakened', 'Begin again')):
    rel_acti(key, name, verb, 'pedestal.nif', 60)
for key, model, r in (('hall_fang_show', lgs('bane'), 70), ('1st_fang', lgs('bane'), 70)):
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
    elif name == 'MODL': b = z(lgs('bane'))   # (Noah: Light Greatswords' Dragonbone - the mod's own model)
    elif name == 'MODT': continue
    elif name == 'DESC': b = z("Dragon bone from the Echo of Alduin. Swings 50% faster. Each blow that lands calls a great meteor down on the one struck. Heavy attacks: a bright white whirlwind round you for 5 s that throws the dead far away. Every 20 kills, fireballs rain round you; kills heal you.")
    elif name == 'DNAM': struct.pack_into('<f', b, 4, 0.75 * 1.5)   # (Noah: "make 2hand attacks faster by 50% only while holding those 2 weapons")
    elif name == 'WNAM': b = struct.pack('<I', fid('STAT', '1st_fang'))
    elif name == 'DATA': b = bytearray(struct.pack('<IfH', 5000, 16.0, 38))
    ws.append(sub(name, bytes(b)))
recs['WEAP'].append(record('WEAP', fid('WEAP', 'fang'), ws))
for key, col, radius in (('rel_blue', '5AA8FF', 620), ('rel_warm', 'FFD9A0', 1100), ('rel_relic', '6AB8FF', 700), ('lantern', 'FFD48A', 1050)):  # (Noah: "a soft, warm light that makes everywhere around me visible, but not blindingly bright, within 15 meters")
    data = bytearray(tsub('ligh', 'DATA')); c = int(col, 16)
    struct.pack_into('<I', data, 4, radius); struct.pack_into('<BBBB', data, 8, c >> 16, (c >> 8) & 255, c & 255, 0)
    # (the Light Orb: a steady light, no flicker. Its omnidirectional shadows (0x1000, added with the orb) went with the crash
    # that came after a while in a lamp-heavy interior (Dragonsreach, three times): a plain dynamic light, as the belt lantern's was)
    if key == 'lantern': struct.pack_into('<I', data, 12, 0x1)
    recs['LIGH'].append(record('LIGH', fid('LIGH', key), [sub('EDID', z('UN_' + key)), sub('OBND', tsub('ligh', 'OBND')), sub('DATA', bytes(data)), sub('FNAM', struct.pack('<f', 1.5 if key == 'lantern' else 1.0))]))  # (soft: a warm fill, not a glare)

# (Noah: "the belt lantern should be an equipable item like the book for battle mage classes - when unequipped it shouldn't
# give illumination, when equipped it should"; "a Skyrim-looking smaller lantern attached by a chain from your body at your
# hip") the Belt Lantern: a small iron candle lantern hung on a chain at the left hip (tools/lantern.py builds its model),
# worn in slot 60. While it is worn src/Director.cpp keeps its light on you: Candlelight's own kind of light effect (the
# game carries it with you, so it never lags behind), hidden, an hour at a time
md = bytearray(bytes.fromhex('001040000000003f58fa030012000000ffffffff00000000091f040000000000000000000000000000000000000000000000003f0000000000000000000000000c000000ffffffff00000000000000000100000000000000ffffffff081f0400061f0400000000008fc2f53d000000000000803f000000000000000000000000000000000000000000000000000000000000000000000000'))  # (Skyrim's LightFFSelf, Candlelight's effect)
struct.pack_into('<I', md, 0, struct.unpack_from('<I', md, 0)[0] | 0x8000)   # hidden in the effects list
struct.pack_into('<I', md, 8, fid('LIGH', 'lantern'))                         # its light: the lantern's
struct.pack_into('<I', md, 24, 0)                                              # no casting light
for off in (92, 96): struct.pack_into('<I', md, off, 0)                        # no orb, no hand art
recs['MGEF'].append(record('MGEF', fid('MGEF', 'lanternlight'), [sub('EDID', z('UN_LanternLightFX')), sub('FULL', z('Belt Lantern')), sub('MDOB', struct.pack('<I', 0)),
    sub('KSIZ', struct.pack('<I', 1)), sub('KWDA', struct.pack('<I', 0x0A9B1E)), sub('DATA', bytes(md)), sub('DNAM', z(''))]))
recs['SPEL'].append(record('SPEL', fid('SPEL', 'lanternlight'), [sub('EDID', z('UN_LanternLight')), sub('OBND', bytes(12)), sub('FULL', z('Belt Lantern')),
    sub('ETYP', struct.pack('<I', 0x013F44)), sub('DESC', z('')),
    sub('SPIT', struct.pack('<IIIfIIffI', 0, 0x1 | 0x200000, 0, 0.0, 1, 0, 0.0, 0.0, 0)),   # (no cost, no charge time: fire and forget, on yourself)
    sub('EFID', struct.pack('<I', fid('MGEF', 'lanternlight'))), sub('EFIT', struct.pack('<fII', 5.0, 0, 3600))]))
LANTERN_NIF = 'CurseOfAlduin\\lantern\\beltlantern.nif'
recs['ARMA'].append(record('ARMA', fid('ARMA', 'lantern'), [sub('EDID', z('UN_BeltLanternAA')), sub('BOD2', struct.pack('<II', 1 << 30, 2)), sub('RNAM', struct.pack('<I', 0x19)),
    sub('DNAM', bytes.fromhex('0a0a00000000000000000000'))] +   # (Noah: the Light Orb - nothing worn on the body: the orb floats over your head, src/Gun.cpp)
    [sub('MODL', bytes.fromhex(h)) for h in '40370100 3a880800 41370100 3c880800 3d7a0900 42370100 3d880800 d87c0600 ba820a00 43370100 40880800 44370100 44880800 45370100 45880800 0a761000 46370100 94870800 47370100 b9820a00 48370100 46880800 49370100 84880800'.split()]))
recs['ARMO'].append(record('ARMO', fid('ARMO', 'lantern'), [sub('EDID', z('UN_BeltLantern')), sub('OBND', struct.pack('<6h', -6, -6, 0, 6, 6, 18)), sub('FULL', z('Light Orb')),
    sub('MOD2', z('CurseOfAlduin\\fx\\lightorb_inv.nif')), sub('BOD2', struct.pack('<II', 1 << 30, 2)), sub('RNAM', struct.pack('<I', 0x19)),
    sub('KSIZ', struct.pack('<I', 1)), sub('KWDA', struct.pack('<I', 0x08F95B)),   # (ArmorClothing)
    sub('DESC', z('A small orb of golden light. Worn, it floats just above your head and lights everything within fifteen metres of you; taken off, it goes dark.')),
    sub('MODL', struct.pack('<I', fid('ARMA', 'lantern'))), sub('DATA', struct.pack('<If', 25, 1.0)), sub('DNAM', struct.pack('<I', 0))]))

# (Noah: the creature overhaul - "combine both zombie mods") the Thriceborn: Mihail's CC Zombies' bodies (its plugin needs
# the Creation Club's Zombies, so its records are ours) on Mihail's own zombie race (mihailzombies.esp), its claws, its AI.
# No script: they do not rise again (Noah: "the coming back to life up to 3 times should be turned off")
MZ = 4 << 24   # (mihailzombies.esp's place among our masters; Dragonborn.esm is 3)
def mz_fid(v):  # one of mihailzombies.esp's form ids, as our plugin sees it
    hi = v >> 24
    return v if hi <= 2 else ((3 << 24) | (v & 0xFFFFFF)) if hi == 4 else (MZ | (v & 0xFFFFFF)) if hi == 5 else v
CCZ = 'Mihail Monsters and Animals\\CC stuff\\Zombies\\actors\\zombies\\'
THRICEBORN = [('mangled', 'Thriceborn Mangled Zombie', 'zombie01.nif'), ('foul', 'Thriceborn Foul Zombie', 'zombie02.nif'),
              ('putrid', 'Thriceborn Putrid Zombie', 'zombie03putrid.nif'), ('malignant', 'Thriceborn Malignant Zombie', 'zombie02dread.nif'),
              ('relentless', 'Thriceborn Relentless Zombie', 'zombie03.nif'), ('dread_putrid', 'Thriceborn Dread Putrid Zombie', 'zombie01putrid.nif')]
for key, label, nif in THRICEBORN:
    recs['ARMA'].append(record('ARMA', fid('ARMA', 'tb_' + key), [sub('EDID', z('UN_tb_' + key + 'AA')), sub('BOD2', struct.pack('<II', 4, 2)), sub('RNAM', struct.pack('<I', MZ | 0x800)),
        sub('DNAM', bytes.fromhex('000000000000001100000000')), sub('MOD2', z(CCZ + nif)), sub('MOD3', z(CCZ + nif)), sub('SNDD', struct.pack('<I', 0x0C0D36))]))
    recs['ARMO'].append(record('ARMO', fid('ARMO', 'tb_' + key), [sub('EDID', z('UN_tb_' + key + 'Skin')), sub('OBND', bytes(12)), sub('BOD2', struct.pack('<II', 4, 2)),
        sub('RNAM', struct.pack('<I', MZ | 0x800)), sub('MODL', struct.pack('<I', fid('ARMA', 'tb_' + key))), sub('DATA', bytes(8)), sub('DNAM', bytes(4))]))
    subs = []
    for name, hx in T['npc_mihailzombie']['subs']:
        b = bytearray(bytes.fromhex(hx))
        if name == 'EDID': b = z('UN_tb_' + key)
        elif name == 'FULL': b = z(label)
        elif name == 'WNAM': b = struct.pack('<I', fid('ARMO', 'tb_' + key))
        elif name in ('SNAM', 'CNTO') or (name in ('VTCK', 'RNAM', 'ATKR', 'CNAM', 'ZNAM', 'CSCR', 'DPLT') and len(b) == 4):
            struct.pack_into('<I', b, 0, mz_fid(struct.unpack_from('<I', b, 0)[0]))
        subs.append(sub(name, bytes(b)))
    recs['NPC_'].append(record('NPC_', fid('NPC_', 'tb_' + key), subs))

# (Noah: the class sets' powers) the Gondorian Ranger's set, Tier I: 10% faster on your feet (an ability while the
# Ranger's set has its first tier: src/Classes.cpp)
md = bytearray(tsub('mgef', 'DATA'))
struct.pack_into('<I', md, 0, 0x2)       # recover: off again when taken away
struct.pack_into('<I', md, 32, 0)        # no shader
struct.pack_into('<I', md, 64, 0)        # value modifier
struct.pack_into('<i', md, 68, 30)       # SpeedMult
struct.pack_into('<I', md, 80, 0)        # constant
struct.pack_into('<I', md, 84, 0)        # self
recs['MGEF'].append(record('MGEF', fid('MGEF', 'set_swift'), [sub('EDID', z('UN_SetSwiftFX')), sub('FULL', z('Ranger\'s Stride')), sub('MDOB', tsub('mgef', 'MDOB')),
    sub('DATA', bytes(md)), sub('DNAM', z('You move 10% faster.'))]))
recs['SPEL'].append(record('SPEL', fid('SPEL', 'set_swift'), [sub('EDID', z('UN_SetSwift')), sub('OBND', bytes(12)), sub('FULL', z('Ranger\'s Stride')),
    sub('ETYP', struct.pack('<I', 0x013F44)), sub('DESC', z('')), sub('SPIT', struct.pack('<IIIfIIffI', 0, 0x1, 4, 0.0, 0, 0, 0.0, 0.0, 0)),
    sub('EFID', struct.pack('<I', fid('MGEF', 'set_swift'))), sub('EFIT', struct.pack('<fII', 10.0, 0, 0))]))

# (spell blasts are cloned from Skyrim's explosions at runtime: EXPL records in an ESL crashed the game at load)
# (the Codex's chapter parser, shared by the class guides' book text and src/Codex.h below)
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
_GUIDE_OLD = {t: (t, im, ps) for t, im, ps in codex_chapters(guide_text(), 'Before Dusk', 'Good Luck')}
GUIDE_EXTRA = []  # (Noah: "make the guide book simpler - way too much information spread out") the old guide's chapters left out
_unused_extra = [_GUIDE_OLD[t] for t in GUIDE_KEEP if t in _GUIDE_OLD]
for cls in G['rows']:
    recs['BOOK'].append(record('BOOK', fid('BOOK', 'guide_' + cls['id']), [
        sub('EDID', z('UN_ClassGuide_' + cls['id'])), sub('OBND', bytes.fromhex('f5fff1fffdff0b000f000300')), sub('FULL', z(cls['book'])),
        sub('MODL', z('Clutter\\Books\\Book02LowPoly02.nif')), sub('DESC', z(class_book_text(cls, GUIDE_EXTRA))),
        sub('KSIZ', struct.pack('<I', 1)), sub('KWDA', struct.pack('<I', 0x000937A2)),
        sub('DATA', struct.pack('<BBHIIf', 0, 0, 0, 0, 25, 1.0)), sub('INAM', struct.pack('<I', 0x0008ADAE)), sub('CNAM', struct.pack('<I', 0))]))

count = sum(len(v) for v in recs.values())
hdr = [sub('HEDR', struct.pack('<fiI', 1.71, count, NEXT)), sub('CNAM', z('Noah Grove')), sub('SNAM', z('The Curse of Alduin: round-based undead survival for Skyrim'))]

# (Noah: "have all the classes have the same armor (dark mage set the one given in the destruction and alchemist class) set
# with different accent colors matching their color class") the Black Mage robes copied for each class (no master of theirs:
# their records' own data, tools/templates/blackmage_set.json), worn with our copies of its models whose colour maps have
# the class's accents (built on the player's PC, src/ArmorForge.cpp); the ground models stay theirs
from esp_ids import CLASS_ARMOUR, CLASS_PIECES
_BM = json.load(open(ROOT / 'tools' / 'templates' / 'blackmage_set.json'))['pieces']
# (Noah: "the names for the armor sets should be different to match the class")
CLASS_ARMOUR_NAMES = {'summoner': "Conjurer's Violet", 'destruction': "Battlemage's Ember", 'alchemist': "Alchemist's Verdant", 'spellsword': "Spellsword's Azure"}
for _c in CLASS_ARMOUR:
    for _pc in CLASS_PIECES:
        _p = _BM[_pc]
        for _a in _p['armas']:
            subs = []
            for name, hx in _a['subs']:
                b = bytes.fromhex(hx)
                if name == 'EDID': b = z(f'UN_CA_{_c}_{_a["id"]}')
                elif name in ('MOD2', 'MOD3', 'MOD4', 'MOD5'):
                    base = b.rstrip(b'\0').decode('latin1').split('\\')[-1]
                    b = z('CurseOfAlduin\\classarmor\\' + _c + '\\' + base)
                elif name[:2] == 'MO' and name[3] in 'TS': continue   # (their texture hashes and texture swaps: not ours)
                subs.append(sub(name, b))
            recs['ARMA'].append(record('ARMA', fid('ARMA', f'cm_{_c}_{_a["id"]}'), subs))
        subs = []
        for name, hx in _p['subs']:
            b = bytes.fromhex(hx)
            if name == 'EDID': b = z(f'UN_CA_{_c}_{_pc}')
            elif name == 'FULL': b = z(CLASS_ARMOUR_NAMES[_c] + ' ' + {'robes': 'Robes', 'hood': 'Hood', 'wraps': 'Handwraps', 'boots': 'Boots'}[_pc])
            elif name == 'MODL' and len(b) == 4:
                aid = '%06X' % (struct.unpack('<I', b)[0] & 0xFFFFFF)
                b = struct.pack('<I', fid('ARMA', f'cm_{_c}_{aid}'))
            elif name[:2] == 'MO' and name[3] in 'TS': continue
            subs.append(sub(name, b))
        recs['ARMO'].append(record('ARMO', fid('ARMO', f'cm_{_c}_{_pc}'), subs))

for m in MASTERS: hdr += [sub('MAST', z(m)), sub('DATA', struct.pack('<Q', 0))]
out = record('TES4', 0, hdr, flags=0x200)  # ESL flag
# (Noah: "switching classes - an inventory swap, saveable") the hidden chest each class's gear waits in while another
# class is played (src/Classes.cpp places one per class, persistent and disabled, and keeps its id in the co-save)
recs['CONT'] = [record('CONT', fid('CONT', 'classchest'), [
    sub('EDID', z('UN_ClassChest')), sub('OBND', bytes(12)), sub('FULL', z('Class Chest')), sub('MODL', z('Clutter\\Containers\\Chest01.nif')),
    sub('DATA', struct.pack('<Bf', 0, 0.0))])]
for k in ['GLOB', 'FACT', 'VTYP', 'FSTS', 'LSCR', 'MUSC', 'MUST', 'ARTO', 'MGEF', 'BOOK', 'ENCH', 'SPEL', 'WOOP', 'SHOU', 'ACTI', 'ARMO', 'LIGH', 'STAT', 'WEAP', 'AMMO', 'NPC_', 'ALCH', 'PROJ', 'PERK', 'ARMA', 'SNDR', 'QUST', 'CONT']: out += group(k, recs[k])  # (PERK: the Summoner's command - it was never written before)  # Bethesda's group order: effects load before the spells that use them
(ROOT / 'data').mkdir(exist_ok=True)
(ROOT / 'data' / PLUGIN).write_bytes(out)
print('wrote', PLUGIN, len(out), 'bytes,', count, 'records')

# ---- (Noah: the lore and the guide read in a window of their own, not a book) src/Codex.h: both books as chapters
# (title, picture, paragraphs with <b>/<i> kept) for the Codex (src/Hud.cpp DrawCodex), shown when either book is read
import re as _re
_page_towns = []
for _l in S['locations']['rows']:   # (src/Generated.h kTowns' order)
    if _l['town'] not in _page_towns: _page_towns.append(_l['town'])
def _page_paras(town):
    html = page_text(town)
    out = []
    for m in _re.finditer(r'<p[^>]*>(.*?)</p>', html, _re.S):
        t = _re.sub(r'</?font[^>]*>', '', m.group(1)).strip()
        if not t or t == '~' or 'THE BROKEN WORDS' in t: continue
        if t == town: continue
        out.append(t)
    return out
def _cs(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', ' ') + '"'
_cx = ['// generated by tools/gen_esp.py from the lore book and the guide - do not edit', '#pragma once', '#include <array>', '#include <string_view>', '#include <vector>',
       'namespace bo3z::codex', '{', '\tstruct Chapter { int book; std::string_view title, image; std::vector<std::string_view> paras; };',
       '\tinline constexpr std::array<std::string_view, %d> kBooks{ "The Curse of Alduin", ' % (5 + len(_page_towns)) + ', '.join(_cs(c['book']) for c in G['rows']) + ''.join(', ' + _cs('The Broken Words: ' + t) for t in _page_towns) + ' };  // 0 the lore, 1-4 the class guides (Summoner, Destruction, Ranger, Spellsword), 5+ a freed city\'s page (kTowns order)',
       '\tinline const std::vector<Chapter> kChapters{']
for title, image, paras in codex_chapters(lore_text(), 'The Greybeard\'s Account', 'Epilogue'):
    _cx.append('\t\t{ %d, %s, %s, { %s } },' % (0, _cs(title), _cs(image), ', '.join(_cs(p) for p in paras)))
for b, cls in enumerate(G['rows'], 1):   # (each class guide: its chapters, the live page (image '@progress'), the shared ones, the old guide's still-true ones, Settings)
    for title, image, paras in guide_chapters(cls) + GUIDE_EXTRA + guide_tail():
        _cx.append('\t\t{ %d, %s, %s, { %s } },' % (b, _cs(title), _cs(image), ', '.join(_cs(p) for p in paras)))
for _i, _t in enumerate(_page_towns):   # (Noah: the freed altars' pages in the same window as the lore and the guides)
    if _t in E['pages']:
        _cx.append('\t\t{ %d, %s, %s, { %s } },' % (5 + _i, _cs('The Broken Words: ' + _t), _cs(''), ', '.join(_cs(p) for p in _page_paras(_t))))
_cx += ['\t};', '}', '']
(ROOT / 'src' / 'Codex.h').write_text('\n'.join(_cx), encoding='utf-8')
print('wrote src/Codex.h')
