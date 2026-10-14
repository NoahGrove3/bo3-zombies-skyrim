"""The Coffer's spells: 200 attack spells (Destruction first, then other schools' attack spells to fill) picked from the spell mods the party plays with, written to
sheets/coffer_spells.json and src/CofferSpells.h (the game reads the header).

    python3 tools/spellpool.py <dir with the mod plugins> <dir with Skyrim.esm, Update.esm, Dawnguard.esm, Dragonborn.esm>

The pool is made of the spells each mod teaches with a spell tome (so they are real player spells, as the mod made
them), keeping the ones that hurt (hostile, Health), plus Strange Runes' attack runes (the mod hands its runes out
without tomes). Every spell keeps its own effects, art and sounds; the game only lowers its magicka cost (so a new
character can cast it) and drops its charge time (no wind-up before a regular cast).
Element: the damage's resistance (fire, frost, shock, poison, disease), else sun (holy/light spells), blood (drain /
vampiric), arcane (everything else).
"""
import json, os, pathlib, re, struct, sys
from collections import Counter, defaultdict

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import spellscan

ROOT = pathlib.Path(__file__).resolve().parent.parent
TARGET = 200
MODS = [  # (plugin file, how many it may give at most)
    ('Apocalypse - Magic of Skyrim.esp', 999),
    ('MysticismMagic.esp', 999),
    ('ForgottenMagic_Redone.esp', 999),
    ('StormCalling.esl', 999),
    ('Conduit.esp', 999),
    ('StrangeRunes.esp', 999),
    ('The Restless Dead.esp', 999),   # the new night creatures' own spells (cast by their mages; player-type, untaught)
    ('mihailesobonecolossus.esp', 999),
    ('mihailoldgod.esp', 999),
    ('mihailfogling.esp', 999),
    ('Skyrim.esm', 999),        # Skyrim's own Destruction spells (and Dawnguard's / Dragonborn's) fill what the mods don't
    ('Dawnguard.esm', 999),
    ('Dragonborn.esm', 999),
]
RUNES = {'Fire': 'fire', 'Frost': 'frost', 'Shock': 'shock', 'Sun': 'sun', 'Poison': 'poison', 'Paralyse': 'arcane', 'Frenzy': 'blood',
         'ConjureFire': 'fire', 'ConjureFrost': 'frost', 'ConjureShock': 'shock', 'WeaknessFire': 'fire', 'WeaknessFrost': 'frost',
         'WeaknessShock': 'shock', 'WeaknessPoison': 'poison', 'Weakness': 'arcane', 'Conjure': 'arcane'}   # the attack runes
SUN = ('sun', 'solar', 'holy', 'light', 'stendarr', 'divine', 'purif', 'meridia', 'dawn', 'bane of the undead', 'vampire\'s bane', 'judgement', 'radian')
BLOOD = ('blood', 'vampir', 'leech', 'drain', 'siphon', 'life\'s finale', 'bloodthirst')


UNTAUGHT_FROM = {'Apocalypse - Magic of Skyrim.esp', 'mihailesobonecolossus.esp', 'The Restless Dead.esp'}
JUNK = re.compile(r' - |test|torch|cloak|damage|teleport|riposte|bash|stagger|aggro|absorb|summon|conjure|heal|leshen|enter the dungeon|^(fire|frost|shock|acid)$', re.I)
EMPOWER = 1.6   # an Empowered spell: the same spell, this much stronger, a third dearer, one tier rarer


def element_of(name, elem):
    n = name.lower()
    if elem in ('fire', 'frost', 'shock', 'poison', 'disease'):
        return elem
    if any(s in n for s in SUN):
        return 'sun'
    if any(s in n for s in BLOOD):
        return 'blood'
    return 'arcane'


_STR = {}


def strings(stem):
    """a localized master's names: <strings dir>/<stem>_english.strings (env UN_STRINGS)"""
    import os
    if stem not in _STR:
        _STR[stem] = {}
        f = pathlib.Path(os.environ.get('UN_STRINGS', '.')) / f'{stem.lower()}_english.strings'
        if f.exists():
            d = f.read_bytes()
            n, size = struct.unpack_from('<II', d, 0)
            base = 8 + n * 8
            for i in range(n):
                sid, off = struct.unpack_from('<II', d, 8 + i * 8)
                e = d.index(b'\0', base + off)
                _STR[stem][sid] = d[base + off:e].decode('cp1252', 'replace')
    return _STR[stem]


def tier_of(cost):
    return 0 if cost < 60 else 1 if cost < 160 else 2 if cost < 400 else 3


def main():
    moddir, vdir = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    masters = [vdir / f for f in ('Skyrim.esm', 'Update.esm', 'Dawnguard.esm', 'Dragonborn.esm')]
    files = {}
    for md in str(sys.argv[1]).split(':'):
        files.update({p.name: p for p in pathlib.Path(md).rglob('*') if p.suffix.lower() in ('.esp', '.esl', '.esm')})
    # every effect and spell, masters first
    mgef = {}
    spells = {}
    taught = defaultdict(set)
    for p in masters + [files[m] for m, _ in MODS if m in files and not m.endswith('.esm')]:
        recs, mast, loc = spellscan.records(p, {b'MGEF', b'SPEL', b'BOOK'})
        def key(fid):
            idx = fid >> 24
            return (mast[idx] if idx < len(mast) else p.name, fid & 0xFFFFFF)
        for t, fid, subs in recs:
            s, effs = {}, []
            for st, sb in subs:
                if st == b'EFID':
                    effs.append([key(struct.unpack('<I', sb)[0]), None])
                elif st == b'EFIT' and effs:
                    effs[-1][1] = struct.unpack_from('<fII', sb)
                elif st not in s:
                    s[st] = sb
            edid = s.get(b'EDID', b'').rstrip(b'\0').decode('latin1')
            if b'FULL' in s and not loc:
                name = s[b'FULL'].rstrip(b'\0').decode('latin1', 'replace')
            elif b'FULL' in s and len(s[b'FULL']) == 4:
                name = strings(p.stem).get(struct.unpack('<I', s[b'FULL'])[0], '')
            else:
                name = ''
            if t == 'MGEF' and b'DATA' in s:
                dt = s[b'DATA']
                flags = struct.unpack_from('<I', dt, 0)[0]
                skill, resist = struct.unpack_from('<ii', dt, 12)
                archetype, pav = struct.unpack_from('<Ii', dt, 64)
                mgef[key(fid)] = dict(edid=edid, hostile=bool(flags & 1), skill=spellscan.AV.get(skill, ''), resist=spellscan.RESIST.get(resist, ''), archetype=archetype, av=pav)
            elif t == 'SPEL' and b'SPIT' in s:
                cost, sflags, stype, charge, ctype, deliv = struct.unpack_from('<IIIfII', s[b'SPIT'], 0)
                k = key(fid)
                prev = spells.get(k, {})
                spells[k] = dict(owner=k[0], id=k[1], edid=edid, name=name or prev.get('name', ''), type=stype, cost=cost, charge=charge,
                                 cast=spellscan.CAST.get(ctype), delivery=spellscan.DELIV.get(deliv), effects=effs, file=p.name)
            elif t == 'BOOK' and b'DATA' in s:
                fl = s[b'DATA'][0]
                if fl & 4:
                    taught[p.name].add(key(struct.unpack_from('<I', s[b'DATA'], 4)[0]))
    def hurts(sp):
        # an attack: a hostile effect on Health (or Magicka/Stamina, or a paralysis / a hostile scripted strike),
        # aimed at someone (not a spell cast on yourself, unless it bursts around you: novas, cloaks)
        school = elem = ''
        health = hostile = False
        for k_, e in sp['effects']:
            m = mgef.get(k_)
            if not m or not m['hostile']:
                continue
            hostile = True
            school = school or m['skill']
            elem = elem or m['resist']
            if m['av'] in (24, 25, 26) or m['archetype'] in (17, 21, 22, 34):
                health = True
        aoe = any(w in sp['name'].lower() for w in ('nova', 'cloak', 'storm', 'aura', 'burst', 'dome', 'rift', 'apocalypse', 'tempest', 'wrath'))
        lname = sp['name'].lower()
        if any(b in lname for b in ('burden', 'disintegrate', 'corrode', 'silence', 'drain stamina', 'drain magicka', 'leech stamina', 'leech magicka', 'curse of the silent', 'dispel', 'hush', 'mute', 'swordbreaker', 'armoreater', 'oppression', 'weakness to')):
            return False, school, elem
        ok = hostile and (health or school == 'destruction') and (sp['delivery'] != 'self' or aoe) and sp['cost'] > 0
        return ok, school, elem
    pool = []
    seen_names = set()
    for mod, cap in MODS:
        rows = []
        if mod == 'StrangeRunes.esp':
            byedid = {sp['edid']: sp for k, sp in spells.items() if sp['file'] == mod}
            for r, el in RUNES.items():
                sp = byedid.get(f'po3_RUNE_{r}Right')
                if sp:
                    nm = (sp['name'] or r).split(' - ')[0].strip()
                    sp = dict(sp, name='Rune of ' + nm, rune_el=el)
                    rows.append((sp, 'destruction', el))
        else:
            tk = taught.get(mod, set())
            # spells a mod gives out without a tome (Apocalypse's Battlemage and Elemental spells, the creature mods' own
            # attack spells): only from the mods that have real ones, and never the helper pieces (cloaks, tests, parts)
            untaught = {k for k, sp in spells.items() if sp['file'] == mod and k not in tk and sp['owner'] == mod
                        and not JUNK.search(sp['name'] or '-')} if mod in UNTAUGHT_FROM else set()
            for k in sorted(tk, key=lambda k: (spells.get(k, {}).get('cost', 0))) + sorted(untaught, key=lambda k: (spells.get(k, {}).get('cost', 0))):
                sp = spells.get(k)
                if not sp or sp['type'] != 0:
                    continue
                ok, school, elem = hurts(sp)
                if ok and sp['name']:
                    rows.append((sp, school, elem))
        if os.environ.get('UN_DEBUG'):
            print('  ', mod, 'taught', len(taught.get(mod, ())), 'rows', len(rows), 'in files', mod in files)
        n = 0
        for sp, school, elem in rows:
            nm = sp['name']
            if nm.lower() in seen_names or n >= cap:
                continue
            seen_names.add(nm.lower())
            pool.append(dict(id=''.join(ch for ch in nm.lower().replace(' ', '_') if ch.isalnum() or ch == '_'), plugin=sp['owner'], formId='%06X' % sp['id'], name=nm, mod=mod, element=sp.get('rune_el') or element_of(nm, elem), school=school, cast=sp['cast'],
                             delivery=sp['delivery'], baseCost=sp['cost'] or 60, charge=round(sp['charge'], 2), tier=tier_of(sp['cost'])))
            n += 1
    print('candidates', len(pool), Counter(r['school'] for r in pool))
    # Destruction first (in mod order), other schools' attack spells only to fill up to TARGET
    pool = [r for r in pool if r['school'] == 'destruction'] + [r for r in pool if r['school'] != 'destruction']
    pool = pool[:TARGET]
    # still short: Empowered tiers of the strongest Destruction spells fill the rest
    strong = sorted([r for r in pool if r['school'] == 'destruction'], key=lambda r: -r['baseCost'])
    for r in strong[:max(0, TARGET - len(pool))]:
        pool.append(dict(r, id='empowered_' + r['id'], name='Empowered ' + r['name'], baseCost=round(r['baseCost'] * 1.33), tier=min(3, r['tier'] + 1), power=EMPOWER))
    # the game's cost: a new character (100 magicka) gets 5-8 casts of a cheap spell, 2-3 of the strongest
    for r in pool:
        r.setdefault('power', 1.0)
        c = r['baseCost']
        per_sec = r['cast'] == 'concentration'
        r['cost'] = max(4, min(14 if per_sec else 45, round((c ** 0.6) * (0.9 if per_sec else 1.6))))
    out = dict(sheet='coffer_spells', about=__doc__.strip().split('\n')[0], columns=list(pool[0].keys()), count=len(pool), rows=pool)
    (ROOT / 'sheets' / 'coffer_spells.json').write_text(json.dumps(out, indent=1))
    # the header
    els = ['fire', 'frost', 'shock', 'poison', 'disease', 'sun', 'blood', 'arcane']
    h = ['// Generated by tools/spellpool.py from the spell mods (sheets/coffer_spells.json). Do not edit.', '#pragma once', '#include <array>',
         '#include <cstdint>', '', 'namespace bo3z::coffer', '{',
         '\tenum Element : std::uint8_t { ' + ', '.join(f'k{e.capitalize()}' for e in els) + ', kElements };',
         '\tinline constexpr const char* kElementName[kElements] = { ' + ', '.join(f'"{e}"' for e in els) + ' };',
         '\tstruct Spell { const char* plugin; std::uint32_t id; const char* name; Element element; std::uint8_t tier; std::uint16_t cost; bool concentration; float power; };   // power > 1: an Empowered copy (made in game)',
         f'\tinline constexpr std::array<Spell, {len(pool)}> kSpells{{ {{']
    for r in pool:
        nm = r['name'].replace('\\', '\\\\').replace('"', '\\"')
        h.append(f'\t\t{{ "{r["plugin"]}", 0x{r["formId"]}, "{nm}", k{r["element"].capitalize()}, {r["tier"]}, {r["cost"]}, {"true" if r["cast"] == "concentration" else "false"}, {r["power"]}f }},')
    h += ['\t} };', '}', '']
    (ROOT / 'src' / 'CofferSpells.h').write_text('\n'.join(h))
    print(len(pool), 'spells')
    print(Counter(r['mod'] for r in pool))
    print(Counter(r['element'] for r in pool))
    print(Counter(r['tier'] for r in pool))


if __name__ == '__main__':
    main()
