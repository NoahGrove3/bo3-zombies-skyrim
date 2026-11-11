"""Form IDs for CurseOfAlduin.esp. Frozen in data/form_ids.json (saves refer to them); new rows get the next free id."""
ELEMENTS = ['fire', 'frost', 'shock', 'poison', 'disease', 'sun', 'blood', 'arcane']
import json, pathlib
ROOT = pathlib.Path(__file__).parent.parent
IDFILE = ROOT / 'data' / 'form_ids.json'
# (Noah: "each sword should have an elemental animation that changes and grows ... tiers 1-3") the class swords' looks: the
# Alchemist's shadowflame, Destruction's Carian blue, the Summoner's white light, the Spellsword's by the spell in its other
# hand, the Dragon Slayer's gold dragonfire
CLASS_ARMOUR = ('summoner', 'destruction', 'alchemist', 'spellsword')   # (Noah: every class in the Black Mage set, its accents its own colour)
CLASS_PIECES = ('robes', 'hood', 'wraps', 'boots')
CLASS_ARMAS = ('01413D', '014141', '014142', '014C0A', '018766', '018767', '014140')
BLADE_VIZ = ('alch', 'carian', 'radiant', 'ss_none', 'ss_fire', 'ss_frost', 'ss_shock', 'ss_sun', 'ss_blood', 'slayer')
# (Noah: "the 10 priest drop weapons ... all players receive the weapon") each city's Dragon Priest drops his weapon
# (docs/DRAGON_PRIEST_WEAPONS.md; models: tools/priestweapons.py, tools/priestbows.py): town -> key, name, model, kind,
# damage, its line in the inventory
PRIEST_WEAPONS = {
    # (Noah: "I want to remove bows ... out of the loot pool" - "New melee weapons") Vahlok's Keyhammer: the Stendarr warhammer
    # (Noah, after the Royal Guard's was unseen in the hands: "H3"; armor_replacer, read from the install); Vokun's Nightcaller's Scythe: Battle Scythe's Daedric scythe
    'Riverwood': ('vahlok', "Jailor's Keyhammer", 'armor_replacer\\1_stendarr\\weapon\\warhammer.nif', 'warhammer', 42, "Vahlok the Jailor's warhammer, its head a great iron key. Every hit shackles the dead in place. Power attack: chains yank the dead around you together."),
    'Whiterun': ('nahkriin', 'Mistblade', 'nahkriin_mistblade', 'sword', 26, "Nahkriin's blade of pale mist. Every hit blinds: the foe slows and its blows weaken. Power attack: a fog where the dead around wander lost, slowed and weakened."),
    'Solitude': ('volsung', 'Windcutter', 'volsung_windcutter', 'sword', 26, "Volsung's sabre. Every hit sends a wind slash on through the dead ahead. Power attack: you dash forward through them, cutting all in your path."),
    'Windhelm': ('zahkriisos', 'Glacier Greatsword', 'zahkriisos_greatsword', 'greatsword', 38, "Zahkriisos' greatsword of glacier ice. Every hit freezes and slows. Power attack: every frozen foe around you shatters."),
    'Markarth': ('otar', 'Storm Blade', 'otar_stormblade', 'sword', 26, "Otar the Mad's storm-steel sword. Every hit leaps as lightning to two more of the dead. Power attack: lightning arcs out to all around you."),
    'Riften': ('rahgot', 'Sacrifice Knife', 'rahgot_dagger', 'dagger', 20, "Rahgot's ritual knife of black obsidian. Every hit poisons, and the poison passes on to the next foe. Power attack: every poisoned foe around you bursts."),
    'Falkreath': ('krosis', 'Grief Blade', 'krosis_griefblade', 'sword', 26, "Krosis' sword, weeping light. Every hit heals you. Power attack: a wail that weakens all the dead around you, their blows feebler and their bodies easier to hurt."),
    'Morthal': ('hevnoraak', 'Sealbreaker', 'hevnoraak_sealbreaker', 'battleaxe', 40, "Hevnoraak's battleaxe, cut from his tomb's cracked seal. Every hit shatters armour. Power attack: a shockwave through the ground that hurts and staggers all around you."),
    'Dawnstar': ('vokun', "Nightcaller's Scythe", 'CurseOfAlduin\\scythes\\daedric.nif', 'scythe', 40, "Vokun's scythe of black daedric steel, cold with dreams. Every hit puts the dead to sleep. Power attack: a sweep that leaves a nightmare fog, where the dead are slowed and lost."),
    'Winterhold': ('morokei', 'Arcane Maul', 'morokei_maul', 'warhammer', 42, "Morokei's maul of floating crystal. Every hit bursts with magic. Power attack: a magicka shockwave around you that also restores your magicka."),
}
# (Noah: "the mod that gives weapons effect animations ... properly assign each one for every weapon in our mod") Visual Animated
# Enchants' looks: each colour our own look-only enchantment (its shader and art taken at load from the mod's effect: src/Classes.cpp
# VaeWeapons), the mod's effect it copies; and every melee weapon's colour (Noah: Mistblade white, Sealbreaker green)
VAE_COL = {'white': 0x00438D, 'blue': 0x000D71, 'iceblue': 0x000D74, 'orange': 0x000D73, 'red': 0x000D70, 'green': 0x000D72, 'poison': 0x002892,
           'drainmagicka': 0x00288C, 'purple': 0x000D75, 'moon': 0x00438D, 'fear': 0x00288F, 'molag': 0x00BA3F, 'gold': 0x00184E}
VAE_WEAP = {'priest_nahkriin': 'white', 'priest_volsung': 'blue', 'priest_zahkriisos': 'iceblue', 'priest_otar': 'blue', 'priest_rahgot': 'poison',
            'priest_krosis': 'red', 'priest_hevnoraak': 'green', 'priest_morokei': 'drainmagicka', 'priest_vahlok': 'white', 'priest_vokun': 'purple',
            'tab_stormtalons': 'blue', 'tab_gravebell': 'white', 'tab_talons': 'red', 'tab_riverstone': 'green', 'tab_wyrmspine': 'fear',
            'tab_lanternwick': 'iceblue', 'tab_cinderclaws': 'orange', 'tab_eclipse': 'gold+iceblue', 'tab_frostmourn': 'iceblue', 'tab_silverscythe': 'moon',
            'tab_bonereaper': 'white', 'tab_wyrmtooth': 'orange', 'tab_bloodmoon': 'red', 'fang': 'orange'}
PRIEST_FIRST = {}   # (a first-person model of its own)
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
             [(k, 'bullet_' + p['id']) for p in S['projectiles']['rows'] for k in ('PROJ', 'MGEF', 'SPEL')] + [('LIGH', 'muzzle'), ('PROJ', 'castflame'), ('MGEF', 'wandcast'), ('MGEF', 'conjurerbolt'), ('FACT', 'undead')] + \
             [('ENCH', w['id']) for w in S['weapons']['rows'] if w.get('kind') == 'wand'] + \
             [(k, 'hand_' + w['id']) for w in S['weapons']['rows'] if w.get('kind') in ('tome', 'wonder') for k in ('SPEL', 'MGEF')] + \
             [(k, 'arrow_' + w['id']) for w in S['weapons']['rows'] if w.get('kind') == 'bow' for k in ('AMMO', 'PROJ')] + \
             [(k, 'shout_' + h['id'] + '_' + str(n)) for h in S['shouts']['rows'] for n in range(3) for k in ('WOOP', 'SPEL')] + [('SHOU', h['id']) for h in S['shouts']['rows']] + [('MGEF', 'shoutcast')] + [('GLOB', 'curse_' + c['town']) for c in S['curse']['cities']] + [('GLOB', 'curse_intro'), ('GLOB', 'stand_layout'), ('BOOK', 'dirge'), ('BOOK', 'guide')] + \
             [('VTYP', 'silent'), ('MUST', 'boss'), ('MUSC', 'boss'), ('MUST', 'freed'), ('MUSC', 'freed'), ('MUST', 'intro'), ('MUSC', 'intro'), ('FSTS', 'silent'), ('STAT', 'ls_shrine'), ('STAT', 'ls_book')] + [('LSCR', f'ls_{i}') for i in range(16)] + \
             [(k, f"fx_{r['id']}_{n}") for r in S['spellcraft']['rows'] for k, n in (('ARTO', 'hand'), ('STAT', 'orb'), ('LIGH', 'light'))] + [('ARTO', 'fx_' + n) for n in ('iceshell', 'roots', 'burn', 'poison', 'portal', 'portal_big')] + [('QUST', 'mcm')] + [('QUST', 'rites_' + c['town']) for c in S['curse']['cities']] + \
             [(k, f'updraft_{n}') for n in range(3) for k in ('WOOP', 'SPEL')] + [('SHOU', 'updraft')] + [(k, f'dance_{n}') for n in range(3) for k in ('WOOP', 'SPEL')] + [('SHOU', 'dance'), ('SNDR', 'dance_song')] + \
             [(k, f"{n}_{b['id']}") for b in S.get('bows', {}).get('rows', []) for k, n in (('WEAP', 'bow'), ('AMMO', 'bowarrow'), ('PROJ', 'bowarrow'))] + \
             echo_ids(S) + \
             [('GLOB', 'egg_' + e['id']) for e in S.get('eggs', {}).get('rows', [])] + [('GLOB', 'egg_' + e['id'] + '_town') for e in S.get('eggs', {}).get('rows', [])] + [('GLOB', 'egg_gifts'), ('GLOB', 'test_kit'), ('GLOB', 'ritual_souls'), ('GLOB', 'ritual_stand')] + [('MGEF', 'altarbeam'), ('SPEL', 'altarbeam')] + \
             [('PERK', f'summon_cap_{n}') for n in (2, 3, 4, 5, 6, 7, 8)] + [('MGEF', 'tomecast'), ('SPEL', 'wonder_tome')] + \
             [('WEAP', 'vortex'), ('AMMO', 'vortexarrow'), ('PROJ', 'vortexarrow'), ('ACTI', 'vortexfx'), ('ACTI', 'purplefire'), ('WEAP', 'slayer'), ('PROJ', 'throwdagger'), ('MGEF', 'throwdagger'), ('SPEL', 'throwdagger'), ('MGEF', 'throwdaggers'), ('SPEL', 'throwdaggers')] + [('EFSH', 'bowskin_' + e) for e in ('fire', 'frost', 'shock', 'wind', 'sun', 'blood', 'void')] + [('ACTI', 'burst_' + e) for e in ('fire', 'frost', 'shock', 'wind', 'sun', 'blood', 'void', 'poison', 'gold')] + [(k, 'flask_' + e) for e in ('fire', 'frost', 'shock', 'poison', 'sun', 'blood', 'philosopher') for k in ('PROJ', 'MGEF', 'SPEL')] + [('SPEL', 'throw_' + e) for e in ('fire', 'frost', 'shock', 'poison', 'sun', 'blood', 'philosopher')] + [('MGEF', 'flaskcast'), ('WEAP', 'alchknife'), ('SPEL', 'bonehorn')] + [('ALCH', 'flaskitem_' + e) for e in ('fire', 'frost', 'shock', 'poison', 'sun', 'blood', 'philosopher')] + [('EFSH', f'armglow_{c}_{t}') for c in range(4) for t in (1, 2, 3, 4, 5, 6)] + [('WEAP', 'flaskweap_' + e) for e in ('fire', 'frost', 'shock', 'poison', 'sun', 'blood', 'philosopher')] + [('BOOK', 'guide_' + c) for c in ('summoner', 'destruction', 'ranger', 'spellsword')] + [('PERK', 'steady'), ('CONT', 'classchest')] + [('MUST', 'night'), ('MUSC', 'night')] + [('WEAP', 'xbow_' + e) for e in ('fire', 'frost', 'shock')] + [('LIGH', 'lantern')] + [('WEAP', 'riftblade'), ('MGEF', 'reanimatebolt')] + [(k, f'rift{w}{n}') for n in (1, 2, 3) for k, w in (('EFSH', 'shader'), ('MGEF', 'fire'), ('ENCH', 'fire'))] + [('SPEL', f'reanimate_{n}') for n in (1, 2, 3)] + \
             [('WEAP', 'madness'), ('STAT', 'madness_1st'), ('ARMO', 'lantern'), ('ARMA', 'lantern'), ('MGEF', 'lanternlight'), ('SPEL', 'lanternlight')] + [(k, f'mad{w}{n}') for n in (1, 2, 3) for k, w in (('EFSH', 'shader'), ('MGEF', 'glow'), ('ENCH', 'glow'))] + [(k, 'tb_' + v) for v in ('mangled', 'foul', 'putrid', 'malignant', 'relentless', 'dread_putrid') for k in ('NPC_', 'ARMO', 'ARMA')] + [('MGEF', 'set_swift'), ('SPEL', 'set_swift')] + \
             [(k, f'cm_{c}_{a}') for c in CLASS_ARMOUR for a in CLASS_ARMAS for k in ('ARMA',)] + [('ARMO', f'cm_{c}_{pc}') for c in CLASS_ARMOUR for pc in CLASS_PIECES] + [('WEAP', 'carianblade')] + [(k, f'bv_{b}_{t}') for b in BLADE_VIZ for t in (1, 2, 3) for k in ('EFSH', 'MGEF', 'ENCH')] + [('EFSH', 'shadowburn')] + [(k, f'vw_{b}') for b in BLADE_VIZ for k in ('LIGH', 'ARTO', 'MGEF', 'SPEL')] + [('EFSH', 'hallghost'), ('EFSH', 'spectral'), ('KYWD', 'scythe'), ('MGEF', 'soulcall'), ('ENCH', 'soulcaller'), ('WEAP', 'soulcaller')] + [(k, 'vae_' + c) for c in VAE_COL for k in ('MGEF', 'ENCH')] + [('ENCH', 'vae_' + c.replace('+', '_')) for c in sorted(set(VAE_WEAP.values())) if '+' in c]   # (Noah: the four crystal swords and the Dragon Slayer - their tier looks)
    for kind, key in wanted:
        k = f'{kind}:{key}'
        if k not in frozen:
            frozen[k] = nxt; nxt += 1
        ids[(kind, key)] = frozen[k]
    if write:
        store['ids'] = {k: hex(v) for k, v in frozen.items()}
        IDFILE.write_text(json.dumps(store, indent=1))
    return ids, nxt
PLUGIN = 'CurseOfAlduin.esp'
def echo_ids(S):
    """Stage 2 (sheet: echo): the cities' pages, the hunt's globals, the Reliquary's doors, pedestals, room and lights"""
    E = S.get('echo')
    if not E:
        return []
    ids = [('BOOK', 'page_' + c['town']) for c in S['curse']['cities']] + [('GLOB', 'echo_' + k) for k in ('pages', 'parts', 'forged', 'held', 'words')]
    ids += [('GLOB', 'echo_kill_' + p['id']) for p in E['parts'] if p['how'] == 'kill']
    ids += [('ACTI', 'rel_door_in'), ('ACTI', 'rel_door_out')] + [('ACTI', 'rel_ped_' + w['id']) for w in E['wonders']]
    ids += [('STAT', 'rel_room'), ('STAT', 'rel_relic'), ('STAT', 'rel_brazier'), ('STAT', 'rel_banner'), ('ACTI', 'rel_portal')] + [('STAT', 'rel_gem_' + w['id']) for w in E['wonders']]
    ids += [('LIGH', 'rel_blue'), ('LIGH', 'rel_warm'), ('LIGH', 'rel_relic')]
    # Stage 5: the Hall of the Awakened (the Reliquary once the curse is broken) and Alduin's Fang
    ids += [('ACTI', 'hall_' + k) for k in ('fang', 'robes', 'skull', 'endless', 'throne')]
    ids += [('STAT', 'hall_fang_show'), ('STAT', '1st_fang'), ('WEAP', 'fang')]
    # the wonders' charged looks and Alduin's Fang's tornado (Elemental Mastery Magic's models, placed and moved by code)
    ids += [('ACTI', 'emm_' + k) for k in ('summon', 'crystal', 'orb', 'storm', 'tornado')]
    ids += [('ACTI', 'tornado_' + k) for k in ('frost', 'shock', 'wind', 'sun', 'blood')]   # the bows' third tier
    # (Noah: the wonders and Alduin's Bane floating in High Hrothgar's great hall, a fiery pillar for the Hall's rewards)
    ids += [('ACTI', 'hf_' + k) for k in ('gravecaller', 'frostbinder', 'wolfcrown', 'barrowhorn', 'bane', 'glow', 'glowtall', 'pillar')]
    ids += [('ACTI', 'hg_' + k) for k in ('gravecaller', 'frostbinder', 'wolfcrown', 'barrowhorn', 'bane')]   # (Noah: each one greyed out where it will float, until it's yours to take)
    # (Noah, 2026-11-05: Blood made Arcane) the Destruction ladder's three spells of ours and the Spellsword's arcane blade
    ids += [(k, 'arcane_' + n) for n in ('bolt', 'orb', 'tide', 'blade1', 'blade2', 'blade3') for k in ('MGEF', 'SPEL')]
    ids += [('MGEF', 'mutagencast'), ('ENCH', 'mutagenstaff')]
    ids += [('WEAP', 'priest_' + v[0]) for v in PRIEST_WEAPONS.values()]   # (Noah: the Dragon Priests' drops)
    # (Noah: the Tablet of Mysteries) its donated weapons: a weapon each (the Codex: a hand spell and its effect), the bows'
    # arrows, and the model shown rising out of the tablet
    TB = S.get('tablet', {}).get('rows', [])
    ids += [('WEAP', 'tab_' + w['id']) for w in TB if w['type'] != 'book'] + [(k, 'tab_' + w['id']) for w in TB if w['type'] == 'book' for k in ('SPEL', 'MGEF')]
    ids += [('AMMO', 'tab_arrow'), ('PROJ', 'tab_arrow')]
    ids += [('MGEF', 'tabstaffcast'), ('ENCH', 'tabstaff')]
    ids += [('ENCH', 'carianwand')]   # (Noah: Destruction's wands - their cast is ours, src/Classes.cpp WandCast)   # (the donated staffs' casts: ours, src/Tablet.cpp)
    ids += [(k, 'tabglove_' + w['id']) for w in TB if w.get('gloves') for k in ('ARMA', 'ARMO')]   # (the fists: gloves on the hands)
    ids += [('MGEF', 'wyrmcall')]   # (Noah: the Violet Wyrm made a summon spell: its cast's effect)
    ids += [('WEAP', 'wyrmhorn')]   # (Noah: the Horn of the Violet Wyrm - "Held in hand")   # (Noah: the Mutagen Gauntlet made a staff: its beam held as Skyrim's staff cast)
    ids += [('MGEF', 'wandhold')]   # (Noah: the Carian Wand pointed like a staff, its cast held - a bolt every 2 s)
    return ids
def blasts():
    """Spell blasts: no-damage copies of Skyrim's explosions (tools/templates/expl/<name>.json)."""
    return sorted(q.stem for q in (ROOT / 'tools' / 'templates' / 'expl').glob('*.json'))
MASTERS = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm', 'Dragonborn.esm', 'mihailzombies.esp']  # (the creature overhaul: the Thriceborn stand on Mihail's zombie race)

POWERUPS = [('instakill', 'FF3030'), ('double', 'FFD040'), ('kaboom', 'FF8A20'), ('firesale', 'B060FF'), ('maxammo', '46C8FF'), ('shadow', '8040E0'), ('thuum', 'D0E8FF'), ('mercy', '50E070')]

SCYTHES = ['forsworn', 'falmer', 'dwarven', 'glass', 'dragonpriest', 'silver', 'Skeleton', 'dragonTeeth', 'vampire', 'daedric']


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
    for r in S['dressing']['rows']:   # (the Ward trial's brazier: placed by the trial itself, on no altar object)
        if r['for'] == 'trial':
            records[('STAT' if r['type'] == 'stat' else 'LIGH', r['id'])] = {'model': r['model'], 'color': r['color'], 'radius': r['radius']}
    for w in S['weapons']['rows']:
        records[('STAT', '1st_' + w['id'])] = {'model': w.get('firstModel', w['standinModel']), 'color': '', 'radius': 0}
        sc_ids = {r['id'] for r in S['spellcraft']['rows']} if 'spellcraft' in S else set()
        records[('STAT', 'show_' + w['id'])] = {'model': f"CurseOfAlduin\\fx\\{w['id']}_icon.nif" if w['id'] in sc_ids else w['standinModel'], 'color': '', 'radius': 0}  # the roll shows the spell's logo
    for _e in ('fire', 'frost', 'shock', 'poison', 'sun', 'blood', 'philosopher'):   # (Noah: the flasks held in the hand)
        records[('STAT', '1st_flask_' + _e)] = {'model': 'CurseOfAlduin\\alchemist\\mutagen_staff.nif' if _e == 'philosopher' else 'CurseOfAlduin\\alchemist\\flask_' + _e + '_held.nif', 'color': '', 'radius': 0}
    for _t, (_k, _n, _m, _kind, _d, _x) in PRIEST_WEAPONS.items():   # (the Dragon Priests' weapons, held)
        records[('STAT', '1st_priest_' + _k)] = {'model': PRIEST_FIRST.get(_k, _m if '\\' in _m else 'CurseOfAlduin\\priests\\' + _m + '.nif'), 'color': '', 'radius': 0}
    records[('STAT', '1st_slayer')] = {'model': 'Light Greatswords\\akaviri\\LightGreatsword.nif', 'color': '', 'radius': 0}        # (Noah) the Dragon Slayer: the Ayleid Crystal Saber
    records[('STAT', '1st_mace')] = {'model': 'CurseOfAlduin\\hipblades\\daedric.nif', 'color': '', 'radius': 0}  # (Noah) the Alchemist's Shadowflame Sword: Kv's purple crystal sword
    records[('STAT', '1st_kvred')] = {'model': 'CurseOfAlduin\\hipblades\\nordic.nif', 'color': '', 'radius': 0}  # the Spellsword's
    records[('STAT', '1st_kvblue')] = {'model': 'CurseOfAlduin\\hipblades\\stahlrim.nif', 'color': '', 'radius': 0}  # Destruction's Carian blade
    records[('STAT', '1st_riftblade')] = {'model': 'CurseOfAlduin\\hipblades\\silver.nif', 'color': '', 'radius': 0}  # (Noah) the Riftblade: Abyssal Heart   # the Ranger's wonder: the Subsuming Vortex
    for b in S.get('bows', {}).get('rows', []):   # the altar's bows: their first-person model
        records[('STAT', '1st_bow_' + b['id'])] = {'model': f"CurseOfAlduin\\bow\\{b['id']}.nif", 'color': '', 'radius': 0}
    records[('STAT', 'show_cheese')] = {'model': S['dressing']['show']['cheeseModel'], 'color': '', 'radius': 0}
    for el in ELEMENTS:   # the Coffer spells' element emblems (the shrine's roll in the world)
        records[('STAT', 'show_el_' + el)] = {'model': f"CurseOfAlduin\\fx\\el_{el}_icon.nif", 'color': '', 'radius': 0}
    records[('STAT', 'dragon_away')] = {'model': 'CurseOfAlduin\\fx\\dragon_away.nif', 'color': '', 'radius': 0}  # the glider's dragon, flying off
    # (Noah: the Tablet of Mysteries "a beacon of light that shoots high so it can be seen in the distance", in the tablet's own
    # glow; "a Dragon Priest mask rises" when it moves on) Skyrim's own wayshrine beam and Konahrik's mask
    # (Noah: "The scythes still don't have an inventory model same goes for the gravebell mace") the item menu shows a weapon by its
    # first-person model: the donated weapons wearing their own model, and every scythe, get one (the same model)
    for w in S['tablet']['rows']:
        if w['type'] != 'book' and w.get('model') and not w.get('look'):
            records[('STAT', '1st_tab_' + w['id'])] = {'model': w['model'], 'color': '', 'radius': 0}
    for n in SCYTHES:
        records[('STAT', '1st_scythe_' + n.lower())] = {'model': 'CurseOfAlduin\\scythes\\' + n + '.nif', 'color': '', 'radius': 0}
    records[('STAT', 'tablet_beacon')] = {'model': 'DLC01\\Effects\\DLC1WayshrineLightBeam01.nif', 'color': '', 'radius': 0}
    records[('STAT', 'tablet_mask')] = {'model': 'Armor\\DragonPriestHelms\\dragonhelm_ultra_go.nif', 'color': '', 'radius': 0}
    records[('STAT', 'whirlwind')] = {'model': 'CurseOfAlduin\\fx\\whirlwind.nif', 'color': '', 'radius': 0}  # Bond II's Updraft shout: the whirlwind round you
    records[('STAT', 'rise_fx')] = {'model': S['dressing']['show']['riseFxModel'], 'color': '', 'radius': 0}
    for w in S.get('tablet', {}).get('rows', []):   # (the Tablet of Mysteries: each weapon's model, shown rising out of it)
        if w.get('show') or w['model']: records[('STAT', 'tabshow_' + w['id'])] = {'model': w.get('show') or w['model'], 'color': '', 'radius': 0}
    return records, pieces
