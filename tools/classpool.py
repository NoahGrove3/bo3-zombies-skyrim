#!/usr/bin/env python3
"""The Echo Altar's spells, one pool per class (the classes overhaul, Noah 2026-10-21).

Each class's spells come in ladders: the altar rolls only a ladder's first rung (its base spell); the class's own
upgrade (Reforge for Destruction, Bind Pact for the Summoner, Imbue for the Spellsword) climbs it rung by rung, the last
rung being the Legendary one. The Ranger rolls bows, not spells, so it has no spell ladders here.

    python3 tools/classpool.py

Reads tools/data/*.json (each spell's cost, cast type and delivery, read from the mods' plugins) and writes
sheets/coffer_spells.json and src/CofferSpells.h.
"""
import json, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
D = ROOT / 'tools' / 'data'
MYST = 'MysticismMagic.esp'
COTC = 'Calling of the Conjurer.esp'
SPRIG = 'ConjureSpriggans.esp'
SKY = 'Skyrim.esm'
# Skyrim's own bolts (not in a tome list): form id, cost, concentration, school
VANILLA = {'Firebolt': (0x012FD0, 45), 'Fireball': (0x01C789, 132), 'Incinerate': (0x10F7ED, 240), 'Ice Spike': (0x02B96C, 40), 'Ice Storm': (0x045F9C, 124),
           'Icy Spear': (0x10F7EC, 228), 'Lightning Bolt': (0x02DD29, 50), 'Chain Lightning': (0x045F9D, 144), 'Thunderbolt': (0x10F7EE, 252)}
CLASSES = ['summoner', 'destruction', 'ranger', 'spellsword']

# (class, ladder id, label, element, [(plugin, spell name), ... rung 1 .. last])
LADDERS = [
    # (Noah: "the battle mage class should be ranged magic damage") the Battle-mage's ladders are all bolts, beams, cones
    # and runes, cast from afar; the close-range sprays and blasts went to the Spellsword (below)
    ('destruction', 'fire', 'Fire', 'fire', [(SKY, 'Firebolt'), (SKY, 'Fireball'), (SKY, 'Incinerate'), (MYST, 'Immolate')]),
    ('destruction', 'frost', 'Frost', 'frost', [(SKY, 'Ice Spike'), (SKY, 'Ice Storm'), (SKY, 'Icy Spear'), (MYST, 'Throat of the World')]),
    # (Noah: "Lightning Bolt stopped shooting after about ten casts - take it out of the Echo Altar") Chain Lightning is the
    # rung the altar rolls now; the ladder is three rungs, all bolts
    ('destruction', 'shock', 'Shock', 'shock', [(SKY, 'Chain Lightning'), (SKY, 'Thunderbolt'), (MYST, 'Finger of the Mountain')]),
    ('destruction', 'sun', 'Sun', 'sun', [(MYST, 'Solar Flare'), (MYST, 'Sunbeam'), (MYST, 'Radiant Sunbeam'), (MYST, 'Daybreak')]),
    ('destruction', 'blood', 'Blood', 'blood', [(MYST, 'Vampiric Bolt'), (MYST, 'Vampiric Blast'), (MYST, 'Vampiric Rune'), (MYST, 'Blood Lance')]),
    # (the class review: every rune read as frost - the Elements trial counted one element, the seals missed them) each rung
    # its own element
    ('destruction', 'runes', 'Runes', 'frost', [(MYST, 'Greater Frost Rune'), (MYST, 'Greater Lightning Rune', 'shock'), (MYST, 'Greater Fire Rune', 'fire'), (MYST, 'Death Cloud', 'poison')]),
    ('summoner', 'beasts', 'Beasts', 'arcane', [(COTC, 'Conjure Wolf'), (COTC, 'Conjure Ice Wolf'), (COTC, 'Conjure Alpha Wolf'), (COTC, 'Conjure Werebear')]),
    ('summoner', 'dead', 'The Dead', 'arcane', [(MYST, 'Conjure Skeleton'), (COTC, 'Conjure Draugr Wight'), (COTC, 'Conjure Draugr Deathlord Warrior'), (COTC, 'Conjure Draugr Death Overlord')]),
    ('summoner', 'daedra', 'Daedra', 'arcane', [(COTC, 'Conjure Dremora'), (COTC, 'Conjure Dremora Archer'), (COTC, 'Conjure Dremora Mage'), (MYST, 'Conjure Dremora Warlock')]),
    ('summoner', 'dwemer', 'Dwemer', 'arcane', [(COTC, 'Conjure Dwarven Spider'), (COTC, 'Conjure Dwarven Sphere'), (COTC, 'Conjure Dwarven Centurion'), (COTC, 'Conjure Dwarven Ballista Master')]),
    ('summoner', 'wild', 'The Wild', 'arcane', [(SPRIG, 'Conjure Spriggan'), (SPRIG, 'Conjure Spriggan Matron'), (SPRIG, 'Conjure Burnt Spriggan'), (SPRIG, 'Conjure Spriggan Earth Mother')]),
    ('spellsword', 'fire', 'Fire', 'fire', [(MYST, 'Burning Touch'), (MYST, 'Flame Touch'), (MYST, 'Fiery Grasp')]),
    ('spellsword', 'frost', 'Frost', 'frost', [(MYST, 'Chilling Touch'), (MYST, 'Icy Touch'), (MYST, 'Freezing Grasp')]),
    ('spellsword', 'shock', 'Shock', 'shock', [(MYST, 'Shocking Touch'), (MYST, 'Static Touch'), (MYST, 'Thundering Grasp')]),
    ('spellsword', 'sun', 'Sun', 'sun', [(MYST, 'Sunlight Touch'), (MYST, 'Radiant Touch'), (MYST, 'Searing Grasp')]),
    ('spellsword', 'blood', 'Blood', 'blood', [(MYST, 'Vampiric Touch'), (MYST, 'Vampiric Grasp'), (MYST, 'Touch of Death')]),
    ('spellsword', 'poison', 'Poison', 'poison', [(MYST, 'Poisonous Touch'), (MYST, 'Venomous Touch'), (MYST, "Viper's Grasp")]),
    # (Noah: "keep the close range spells like the fire one for the spellsword") the sprays and point-blank blasts
    ('spellsword', 'fire_breath', 'Flame Breath', 'fire', [(MYST, 'Scorch'), (MYST, 'Greater Flames'), (MYST, 'Flame Tempest')]),
    ('spellsword', 'frost_breath', 'Frost Breath', 'frost', [(MYST, 'Hoarfrost'), (MYST, 'Greater Frostbite'), (MYST, 'Flash Freeze')]),
    ('spellsword', 'shock_breath', 'Storm Breath', 'shock', [(MYST, 'Greater Sparks'), (MYST, 'Electrocute')]),
    ('spellsword', 'poison_breath', 'Poison Breath', 'poison', [(MYST, 'Poison Spray'), (MYST, 'Miasma')]),
    ('spellsword', 'blood_breath', 'Blood Breath', 'blood', [(MYST, 'Drain Life'), (MYST, 'Leech Life')]),
    # (Noah: "remove the staff the Summoner comes with - a spell that's upgradable instead, of an element no other class
    # uses") Corrosion: Mysticism's metal-eating bolts. Each strikes a burst round what it hits and marks everyone in it for
    # your summons (Classes::CorrosionHit); Bind Pact climbs it, each rung a wider, harder burst. (last: the indices before
    # it stay as they were)
    ('summoner', 'corrosion', 'Corrosion', 'corrosion', [(MYST, 'Corrode Armor'), (MYST, 'Armoreater'), (MYST, 'Swordbreaker')]),
]
ELEMENTS = ['fire', 'frost', 'shock', 'poison', 'corrosion', 'sun', 'blood', 'arcane']  # (corrosion took disease's place: nothing used it)


def load():
    out = {}
    for x in json.load(open(D / 'mysticism_tomes.json')):
        out[(MYST, x['name'])] = (int(x['spell'], 16), x['cost'], x['cast'] == 'concentration', x['school'])
    for x in json.load(open(D / 'cotc_tomes.json')):
        out.setdefault((COTC, x['name']), (int(x['spell'], 16), x['cost'], x['cast'] == 'concentration', 'Conjuration'))
    for x in json.load(open(D / 'spriggans_spells.json')):
        out[(SPRIG, x['name'])] = (int(x['fid'], 16), x['cost'], x['cast'] == 'concentration', 'Conjuration')
    for n, (fid, cost) in VANILLA.items():
        out[(SKY, n)] = (fid, cost, False, 'Destruction')
    return out


def main():
    known = load()
    rows = []
    for cls, lad, label, elem, rungs in LADDERS:
        for r, key in enumerate(rungs):
            rel = key[2] if len(key) > 2 else elem
            key = key[:2]
            fid, cost, conc, school = known[key]
            last = r == len(rungs) - 1
            rows.append(dict(id=f'{cls}_{lad}_{r + 1}', plugin=key[0], formId=f'{fid:06X}', name=key[1], mod=key[0], element=rel,
                             school=school.lower() if school else 'destruction', cast='concentration' if conc else 'fire-and-forget',
                             cls=cls, ladder=f'{cls}_{lad}', ladderLabel=label, rung=r + 1, rungs=len(rungs),
                             tier=3 if last else min(r, 2), baseCost=cost, cost=max(8, min(60, round(cost * 0.25))), power=1.0))
    sheet = dict(sheet='coffer_spells', about='The Echo Altar\'s spells, one pool per class (tools/classpool.py): the altar rolls a ladder\'s first rung; '
                 'the class\'s own upgrade climbs it, its last rung Legendary.', columns=list(rows[0].keys()), count=len(rows), rows=rows)
    (ROOT / 'sheets' / 'coffer_spells.json').write_text(json.dumps(sheet, indent=1, ensure_ascii=False) + '\n')
    ladders = []
    for r in rows:
        if r['ladder'] not in ladders:
            ladders.append(r['ladder'])
    h = ['// Generated by tools/classpool.py (the Echo Altar\'s spells, one pool per class). Do not edit.', '#pragma once', '#include <array>', '#include <cstdint>', '',
         'namespace bo3z::coffer', '{',
         '\tenum Element : std::uint8_t { ' + ', '.join(f'k{e.capitalize()}' for e in ELEMENTS) + ', kElements };',
         '\tinline constexpr const char* kElementName[kElements] = { ' + ', '.join(f'"{e}"' for e in ELEMENTS) + ' };',
         '\tenum Class : std::uint8_t { kSummoner, kDestruction, kRanger, kSpellsword, kClasses };',
         '\tinline constexpr const char* kClassName[kClasses] = { "Summoner", "Destruction", "Ranger", "Spellsword" };',
         '\tstruct Spell { const char* plugin; std::uint32_t id; const char* name; Element element; std::uint8_t tier; std::uint16_t cost; bool concentration; float power;',
         '\t\tClass cls; std::uint8_t ladder, rung, rungs; const char* ladderLabel; };   // rung 1 is what the altar rolls; the last rung is Legendary',
         f'\tinline constexpr int kLadders = {len(ladders)};',
         f'\tinline constexpr std::array<Spell, {len(rows)}> kSpells{{ {{']
    for r in rows:
        name = r['name'].replace('"', '\\"')
        h.append(f'\t\t{{ "{r["plugin"]}", 0x{r["formId"]}, "{name}", k{r["element"].capitalize()}, {r["tier"]}, {r["cost"]}, {str(r["cast"] == "concentration").lower()}, 1.0f, '
                 f'k{r["cls"].capitalize()}, {ladders.index(r["ladder"])}, {r["rung"]}, {r["rungs"]}, "{r["ladderLabel"]}" }},')
    h += ['\t} };', '}', '']
    (ROOT / 'src' / 'CofferSpells.h').write_text('\n'.join(h))
    print(f'classpool: {len(rows)} spells in {len(ladders)} ladders')


if __name__ == '__main__':
    main()
