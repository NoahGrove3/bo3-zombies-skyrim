"""The workshop's close pictures of the abilities that are more than one burst in place (Noah, 2026-11-25: "Build from real parts";
"This should look like the javelin bounces not lightning"; "Tier 3 carian wand cycles through fire frost and lightning bolts not a
tornado"): each one's real parts as the game plays them - read from fxmap.ini, the same lines the game reads - put on the beats and
the paths the source gives them (a bolt from the hand, a javelin's bounce, a sunbeam from the sky, a rain of meteors).

Doc only: nothing here reaches the game. tools/realpreview.py reads the models these need; the workshop's assemble step puts the
shows in the page, where its real.js plays them.

    python3 tools/docshow.py      (prints each show's parts, and any model it needs that isn't read yet)
"""
import pathlib, re

ROOT = pathlib.Path(__file__).resolve().parent.parent
FXMAP = ROOT / 'assets' / 'fx' / 'SKSE' / 'Plugins' / 'CurseOfAlduin' / 'fxmap.ini'


def fxmap():
    """fxmap.ini: each of our effect's names -> its real parts [{model, s, pal, delay, ground, own}]"""
    out = {}
    for line in FXMAP.read_text(encoding='utf-8').splitlines():
        line = re.split(r'\s#', line, maxsplit=1)[0].strip()
        if not line or line.startswith('#') or '=' not in line: continue
        key, rest = line.split('=', 1)
        parts = []
        for seg in rest.split(';'):
            tok = seg.split()
            if not tok or tok[0] == 'none': continue
            p = dict(model=tok[0].lower(), s=1.0, pal=None, delay=0.0, ground=False)
            for t in tok[1:]:
                if t.startswith('s='): p['s'] = float(t[2:])
                elif t.startswith('pal='): p['pal'] = t[4:]
                elif t.startswith('delay='): p['delay'] = float(t[6:])
                elif t == 'ground': p['ground'] = True
            if p['model'].startswith('curseofalduin\\'): p['own'] = p['model'].split('\\')[1]   # (our own model: the javelin, a flask)
            parts.append(p)
        out[key.strip().lower()] = parts
    return out


F = 'curseofalduin\\fx\\'
W = F + 'w\\'
# each show: its beats - {fx: an fxmap name, t: when, at: where (hand, t0..t2 the dead, area, self, line:i), k: its size as the game
# scales it, fly: [from, to, seconds, arc] for a bolt in flight}
E = lambda fx, t=0.0, at='t0', k=1.0, fly=None: dict(fx=fx.lower(), t=t, at=at, k=k, fly=fly)
SHOWS = {
    # (src/Tablet.cpp: a small gold ball that bounces from one of the dead to the next, a small burst of gold on each)
    'wanderstar_cast': [E(F + 'star_ball.nif', 0, fly=['hand', 't0', 0.35, 20]), E(F + 'philosopher_burst.nif', 0.35, 't0', 0.35),
                        E(F + 'star_ball.nif', 0.4, fly=['t0', 't1', 0.4, 60]), E(F + 'philosopher_burst.nif', 0.8, 't1', 0.35),
                        E(F + 'star_ball.nif', 0.85, fly=['t1', 't2', 0.4, 60]), E(F + 'philosopher_burst.nif', 1.25, 't2', 0.35)],
    # (src/Classes.cpp JavThrow / JavFly / JavBurst: the javelin in its element's bolt; its burst where it strikes; the bounce slow,
    # arcing, a flash at each)
    'javelin_throw': [E('curseofalduin\\wyrmspine\\fly_fire.nif', 0, fly=['hand', 't0', 0.45, 40]), E(W + 'fly_fire_burst.nif', 0.45, 't0')],
    'javelin_bounce': [E(W + 'fly_fire_burst.nif', 0.0, 't0', 0.8), E('magic\\impactlightspell.nif', 0.0, 't0', 0.6),
                       E('curseofalduin\\wyrmspine\\fly_fire.nif', 0.3, fly=['t0', 't1', 0.6, 120]), E(W + 'fly_fire_burst.nif', 0.9, 't1', 0.65),
                       E('magic\\impactlightspell.nif', 0.9, 't1', 0.55),
                       E('curseofalduin\\wyrmspine\\fly_fire.nif', 1.1, fly=['t1', 't2', 0.6, 120]), E(W + 'fly_fire_burst.nif', 1.7, 't2', 0.5),
                       E('magic\\impactlightspell.nif', 1.7, 't2', 0.5)],
    # (the Staff of Magnus's beam lent to the Soulcaller's Staff, and its impact)
    'soulcaller_beam': [E('magic\\staffofmagnusbolt01.nif', 0, fly=['hand', 't0', 0, 0]), E('magic\\impactmg07staffofmagnus.nif', 0.1, 't0')],
    # (Noah, 2026-11-25: "Use skyrims summon portal") where each comes
    'soulcaller_wolves': [E(F + 'soul_portal.nif', 0, 'area'), E(F + 'soul_portal.nif', 0.35, 'area:1'), E(F + 'soul_portal.nif', 0.7, 'area:2')],
    'summon_arrive': [E(F + 'soul_portal.nif', 0, 'area')],
    # (a flask every 0.5 s, each its own element's: the flask in its bolt, its own burst where it breaks)
    'mutagen_flasks': [E('curseofalduin\\alchemist\\flask_fire.nif', 0, fly=['hand', 't0', 0.45, 80]), E(W + 'flask_fire_burst.nif', 0.45, 't0'),
                       E('curseofalduin\\alchemist\\flask_frost.nif', 0.5, fly=['hand', 't1', 0.5, 80]), E(W + 'flask_frost_burst.nif', 1.0, 't1'),
                       E('curseofalduin\\alchemist\\flask_shock.nif', 1.0, fly=['hand', 't2', 0.55, 80]), E(W + 'flask_shock_burst.nif', 1.55, 't2')],
    # (src/Classes.cpp SlayerTick: a gold fireball from the sky every 0.6 s on one of the dead near you, a small gold burst where it lands)
    'slayer_heavy': [x for i, sp in enumerate(['t0', 't1', 't2']) for x in
                     (E(F + 'meteor_gold.nif', i * 0.6, k=0.8, fly=['sky:' + sp, sp, 0.9, 0]), E(F + 'burst_gold.nif', i * 0.6 + 0.9, sp, 0.5))],
    # (src/Classes.cpp WandCast: fire, lightning, ice in turn from the wand's tip, each bolt's own shatter at half size)
    'wand_light': [E(F + 'arc_fire.nif', 0, k=1.25, fly=['hand', 't0', 0.45, 0]), E(W + 'arc_fire_burst.nif', 0.45, 't0', 0.5),
                   E(F + 'arc_shock.nif', 0.5, k=1.25, fly=['hand', 't1', 0.5, 0]), E(W + 'arc_shock_burst.nif', 1.0, 't1', 0.5),
                   E(F + 'arc_frost.nif', 1.0, k=1.25, fly=['hand', 't2', 0.55, 0]), E(F + 'burst_wandfrost.nif', 1.55, 't2', 0.5)],
    # (Tier II-III: every 2nd bolt a great one, piercing; Tier III with a volley either side)
    'wand_heavy': [E(F + 'arc_fire.nif', 0, k=1.25, fly=['hand', 't0', 0.45, 0]), E(W + 'arc_fire_burst.nif', 0.45, 't0', 0.5),
                   E(F + 'arc_shock.nif', 0.5, k=2.25, fly=['hand', 't0', 0.45, 0]), E(W + 'arc_shock_burst.nif', 0.95, 't0', 0.9),
                   E(F + 'arc_shock.nif', 0.5, k=1.25, fly=['hand', 't1', 0.55, 0]), E(W + 'arc_shock_burst.nif', 1.05, 't1', 0.5),
                   E(F + 'arc_shock.nif', 0.5, k=1.25, fly=['hand', 't2', 0.6, 0]), E(W + 'arc_shock_burst.nif', 1.1, 't2', 0.5),
                   E(F + 'arc_frost.nif', 1.0, k=1.25, fly=['hand', 't0', 0.45, 0]), E(F + 'burst_wandfrost.nif', 1.45, 't0', 0.5),
                   E(F + 'arc_fire.nif', 1.5, k=2.25, fly=['hand', 't0', 0.45, 0]), E(W + 'arc_fire_burst.nif', 1.95, 't0', 0.9),
                   E(F + 'arc_fire.nif', 1.5, k=1.25, fly=['hand', 't1', 0.55, 0]), E(W + 'arc_fire_burst.nif', 2.05, 't1', 0.5),
                   E(F + 'arc_fire.nif', 1.5, k=1.25, fly=['hand', 't2', 0.6, 0]), E(W + 'arc_fire_burst.nif', 2.1, 't2', 0.5)],
    # (src/Gun.cpp SkyBeam, white for the Radiant Quarterstaff: a bolt of sunlight out of the sky; where it lands the sunburst, the
    # pillar of sunlit fire turning, motes rising; the white ring half a second on)
    'staff_heavy': [E(F + 'sun_bolt_white.nif', 0, fly=['sky:t0', 't0', 0.43, 0]), E(F + 'sun_burst_white.nif', 0.43, 't0g', 1.0),
                    E(F + 'sun_pillar_white.nif', 0.43, 't0g', 1.0), E(F + 'sun_motes_white.nif', 0.58, 't0g'), E(F + 'sun_motes_white.nif', 1.13, 't0g'),
                    E(F + 'ring_white.nif', 0.93, 't0g', 1.5)],
    # (the Spellbound Rapier's line: Skyrim's Ice Spike volley shards, one after another along your aim)
    'rapier_line': [E(F + 'spike_frost.nif', 0.07 * i, f'line:{i}', 1.0) for i in range(6)],
    # (the Alchemist's tier powers)
    'ffire_power': [E(F + 'flask_firestorm.nif', 0, 't0g')], 'ffrost_power': [E(F + 'flask_frostnova.nif', 0, 't0g')],
    'fshock_power': [E(F + 'flask_strike.nif', 0, 't0g')], 'fvoid_power': [E(F + 'flask_manavoid.nif', 0, 't0g')],
}


def shows():
    """each show with its parts resolved: [{t, at, fly, jav, parts: [{model, s, pal, delay, ground}]}]"""
    M = fxmap()
    out = {}
    for card, evs in SHOWS.items():
        rows = []
        for e in evs:
            parts = M.get(e['fx'], [dict(model=e['fx'], s=1.0, pal=None, delay=0.0, ground=False)] if not e['fx'].startswith('curseofalduin') else [])
            if not parts and e['fx'].startswith('curseofalduin'): raise KeyError(f'{card}: {e["fx"]} not in fxmap.ini')
            jav = any(p.get('own') == 'wyrmspine' for p in parts)
            parts = [dict(model=p['model'], s=round(p['s'] * e['k'], 3), pal=p['pal'], delay=p['delay'], ground=p['ground']) for p in parts if 'own' not in p]
            rows.append(dict(t=e['t'], at=e['at'], fly=e['fly'], jav=jav, parts=parts, k=e['k']))
        out[card] = rows
    return out


LABELS = {
    'magic\\fireboltprojectile.nif': "Skyrim: Firebolt in flight", 'magic\\iceprojectile01.nif': "Skyrim: Ice Spike in flight",
    'magic\\fireballprojectile01.nif': "Skyrim: Fireball in flight", 'dlc01\\magic\\dlc1sunlightprojectile.nif': "Dawnguard: sunlight bolt in flight",
    'magic\\staffofmagnusbolt01.nif': "Skyrim: the Staff of Magnus's beam", 'magic\\impactmg07staffofmagnus.nif': "Skyrim: the Staff of Magnus's impact",
    'magic\\summontargetfx.nif': "Skyrim: the conjuring portal", 'magus\\mysticism\\sunbeamexplosion01.nif': "Mysticism: sunbeam explosion",
    'effects\\mgmagicfirepillar01.nif': "Skyrim: the College's fire pillar", 'dlc01\\magic\\dlc1impactsunfirespell.nif': "Dawnguard: Sun Fire impact",
    'effects\\mg07labyrinthianshockwave.nif': "Skyrim: Labyrinthian shockwave", 'magic\\impactlightspell.nif': "Skyrim: Magelight flash",
    'magic\\magicespikevolleyexplosion.nif': "Skyrim: Ice Spike volley shards", 'magic\\firestormareaeffect.nif': "Skyrim: Fire Storm",
    'apocnew\\destructionfrost_explosion_frostnova.nif': "Apocalypse: Frost Nova", 'apocnew\\destructionshock_hazard_lightningstrike.nif': "Apocalypse: Lightning Strike",
    'apocnew\\illusionblue_explosion.nif': "Apocalypse: Mana Void burst", 'apocnew\\destructionwizardfire_explosion.nif': "Apocalypse: wizard-fire burst",
    'magic\\impactfrosticestorm.nif': "Skyrim: Ice Storm impact",
}
PAL = {'white': 'in white', 'gold': 'in gold', 'shock': 'as shock', 'blood': 'in red', 'violet': 'in violet', 'poison': 'in green', 'sun': 'in sun-gold',
       'fire': 'in fire', 'frost': 'in frost', 'red': 'in red', 'moon': 'in moonlight', 'fear': 'in fear-green'}


def label(model):
    if model in LABELS: return LABELS[model]
    import realfx
    for th in realfx.PARTS.values():
        for r, p in th.items():
            if r != 'native' and p['model'].lower() == model: return p['label']
    return model


def parts_text(card):
    """the card's 'uses' list: each real part once, in the order it plays"""
    out = []
    for e in sorted(shows()[card], key=lambda e: e['t']):
        if e['jav'] and 'The Wyrmspine Javelin itself' not in out: out.append('The Wyrmspine Javelin itself')
        for p in e['parts']:
            t = label(p['model']) + (' ' + PAL.get(p['pal'], 'in ' + p['pal']) if p['pal'] else '')
            if t not in out: out.append(t)
    return out


def models():
    return sorted({p['model'] for evs in shows().values() for e in evs for p in e['parts']})


if __name__ == '__main__':
    rp = ROOT / 'build' / 'realparts' / 'meshes'
    for c, evs in shows().items(): print(c, [(e['t'], e['at'], [p['model'].split('\\')[-1] for p in e['parts']]) for e in evs])
    for m in models():
        if not (rp / m.replace('\\', '/')).exists() and not (rp / 'meshes' / m.replace('\\', '/')).exists(): print('NOT READ YET:', m)
