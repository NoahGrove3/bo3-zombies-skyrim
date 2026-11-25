import json, sys, pathlib
sys.path.insert(0, '/home/claude/bo3-zombies-skyrim/tools')
import weaponfx as W
H = pathlib.Path(__file__).parent
SHA = sys.argv[1]; INSTALL = sys.argv[2]
J = lambda o: json.dumps(o, ensure_ascii=False).replace('<', '\\u003c')


DESIGN = json.load(open('/home/claude/bo3-zombies-skyrim/docs/workshop/effects_design.json'))['weapons']


def it(id_, name, key, where='', note='', look=None, col=None, tiers=None):
    v = W.ALL[key] if key in W.ALL else W.CLASS[key]
    sig = DESIGN.get(id_, {}).get('look', '')
    d = {'id': id_, 'name': name, 'key': key, 'look': look or v[3], 'col': col or v[4], 'where': where, 'note': ''}
    if tiers: d['tiers'] = tiers
    return d


P = [('priest_vahlok', "Jailor's Keyhammer", 'Vahlok, Riverwood', ''),
     ('priest_nahkriin', 'Mistblade', 'Nahkriin, Whiterun', 'Kept, as you chose.'),
     ('priest_volsung', 'Windcutter', 'Volsung, Solitude', 'The cone of still stars near the tip is gone (they were floating motes).'), ('priest_zahkriisos', 'Glacier Greatsword', 'Zahkriisos, Windhelm', ''),
     ('priest_otar', 'Storm Blade', 'Otar, Markarth', ''),
     ('priest_rahgot', 'Sacrifice Knife', 'Rahgot, Riften', ''),
     ('priest_krosis', 'Grief Blade', 'Krosis, Falkreath', 'The still pillar of colour is gone (the model\'s own old effect).'), ('priest_hevnoraak', 'Sealbreaker', 'Hevnoraak, Morthal', 'The still pillar of colour is gone.'),
     ('priest_vokun', "Nightcaller's Scythe", 'Vokun, Dawnstar', ''), ('priest_morokei', 'Arcane Maul', 'Morokei, Winterhold', 'The still pillar of colour is gone.')]
T = [('tab_stormtalons', 'Stormtalons', 'Glass claws, both hands', ''), ('tab_talons', 'Talons of Woe', 'Dragonbone claws, both hands', ''),
     ('tab_cinderclaws', 'Cinderclaws', 'Ember claws, both hands', ''), ('tab_gravebell', 'Gravebell Mace', 'Mace', ''),
     ('tab_riverstone', 'Riverstone Halberd', 'Halberd', ''), ('tab_wyrmspine', 'Wyrmspine Pike', 'Pike', ''),
     ('tab_lanternwick', 'Rimewick Quarterstaff', 'Quarterstaff, both heads', 'Only the two heads glow.'), ('tab_eclipse', 'Eclipse Edge', 'Spear: gold on one edge, ice on the other', ''),
     ('tab_frostmourn', 'Frostmourn Rapier', 'Rapier', ''), ('tab_silverscythe', 'Silver Reaping Scythe', 'Scythe', ''),
     ('tab_bonereaper', 'Bonereaper', 'Scythe', ''), ('tab_wyrmtooth', 'Wyrmtooth Scythe', 'Scythe', ''),
     ('tab_bloodmoon', 'Bloodmoon Scythe', 'Scythe', '')]
WC = [{'id': 'priest', 'name': 'Dragon Priest relics', 'sub': 'The weapon each city\'s Dragon Priest drops.', 'items': [it(k, n, k, w, nt) for k, n, w, nt in P]},
      {'id': 'tablet', 'name': 'Tablet of Mysteries', 'sub': 'The wonders the Tablet gives. The claws carry the same look in both hands. The Wandering Star isn\'t here: its star is its own effect (see Abilities).', 'items': [it(k, n, k, w, nt) for k, n, w, nt in T]}]
cls = [
    it('alch', 'Shadowflame Scythe', 'cls_dragonpriest_alch', 'Alchemist, from Tier I', '', tiers=[['cls_forsworn_alch', 'Base · Forsworn'], ['cls_falmer_alch', 'I · Falmer'], ['cls_dwarven_alch', 'II · Dwarven'], ['cls_dragonpriest_alch', 'III & Ascended · Dragon Priest']]),
    it('carian', 'Carian Wand', 'cls_daedricwand_carian', 'Battle-mage, from Tier I', '', tiers=[['cls_steelwand_carian', 'Base · Steel'], ['cls_stahlrimwand_carian', 'I · Stalhrim'], ['cls_glasswand_carian', 'II · Glass'], ['cls_daedricwand_carian', 'III & Ascended · Daedric']]),
    it('radiant', 'Radiant Quarterstaff', 'cls_dragonbonestaff_radiant', 'Summoner, from Tier I', 'Only the two heads glow now, at every tier.', tiers=[['cls_steelstaff_radiant', 'Base · Steel'], ['cls_ebonystaff_radiant', 'I · Ebony'], ['cls_daedricstaff_radiant', 'II · Daedric'], ['cls_dragonbonestaff_radiant', 'III & Ascended · Dragonbone']]),
]
RT = [['cls_steelrapier_ss_fire', 'Base · Steel'], ['cls_ebonyrapier_ss_fire', 'I · Ebony'], ['cls_daedricrapier_ss_fire', 'II · Daedric'], ['cls_dragonbonerapier_ss_fire', 'III & Ascended · Dragonbone']]
EL = [('ss_none', 'bare', 'no spell in your other hand', ''), ('ss_fire', 'fire', 'fire in your other hand', ''), ('ss_frost', 'frost', 'frost in your other hand', ''),
      ('ss_shock', 'shock', 'shock in your other hand', ''), ('ss_sun', 'sun', 'sun magic in your other hand', ''), ('ss_blood', 'arcane', 'arcane magic in your other hand', '')]
for k, n, w, nt in EL:
    look, col = W.BV_PICK[k]
    rt = [[f'cls_{m}rapier_{k}', lab] for m, lab in (('steel', 'Base · Steel'), ('ebony', 'I · Ebony'), ('daedric', 'II · Daedric'), ('dragonbone', 'III & Ascended · Dragonbone'))]
    cls.append(it(k, 'Spellbound Rapier · ' + n, f'cls_dragonbonerapier_{k}', 'Spellsword, ' + w, nt, look=look, col=col, tiers=rt))
cls.append(it('slayer', 'Dragon Slayer', 'cls_dragonslayer_slayer', 'The Spellsword\'s wonder'))
WC.append({'id': 'class', 'name': 'Class weapons', 'sub': 'One look per weapon across all its tiers; each tier\'s model is fitted on its own. The Spellbound Rapier\'s look follows the spell in your other hand, so each element is its own card.', 'items': cls})
WC.append({'id': 'bane', 'name': "Alduin's Bane", 'sub': 'The last weapon.', 'items': [it('fang', "Alduin's Bane", 'fang', 'The Hall of the Awakened', 'Its model is still Light Greatswords\'; ours replaces it.')]})

TESTS = json.load(open('/home/claude/bo3-zombies-skyrim/docs/workshop/tests_master.json'))

RECS = [
    {'id': 'final', 't': 'Is the doc final?', 'd': 'When your looks, abilities, sounds, hit feel, deaths, trials and big-night picks are set here, I build them all into the game for your test.', 'opts': ['Yes, build my picks', 'Not yet, still picking']},
    {'id': 'first', 't': 'What I build first', 'd': 'When you call it final, which goes in first (the rest follow in the same build if they fit)?', 'opts': ['Sounds and hit feel', 'The deaths', 'The trials', 'The hitches and big nights']},
    {'id': 'cards', 't': 'The new simple cards', 'd': 'Every list is now small cards; a click opens the preview beside everything to change, on one screen. Right?', 'opts': ['Yes', 'Change something (note)']},
]

RECENT = [
    ['Nov 25, night', ['<b>Simple cards</b>: every list small, a click opens the preview beside all its options on one screen.', '<b>Sounds</b>: every weapon\'s draw, swing, heavy and hit and every ability\'s cast, impact and linger - five real options each, playable, tunable.',
                        '<b>Hit feel</b>, <b>the dead\'s deaths</b>, <b>the Echo Altar\'s trials in 3D</b>, <b>big nights</b> from your own game\'s numbers, and an <b>Issues</b> list.', 'Your picks from the last doc carried over; the Dragon Priest scythe\'s blade from the staff\'s head.']],
    ['Nov 25, late evening', ['<b>Five looks for every weapon</b>, two new ones each.', '<b>Five options of its own for every ability</b>, variations of what it does, with any colour and sliders for size, brightness, area, vibrance, speed, linger, density and light cast.', '<b>The four scythes</b> fixed at the join.']],
    ['Nov 25, evening', ['<b>Our own scythes in the game</b>, the joins flush on Wyrmtooth, Silver Reaping and Bloodmoon.', '<b>The ability pictures built from the real parts</b>: their own textures and particle settings read from your game; the javelin bounces itself, the Carian Wand cycles fire, lightning and ice with its great bolts and volleys.', "<b>Alduin's Bane heavy</b> a white wind shockwave; <b>the Radiant Quarterstaff's sunbeam</b> white; <b>summons</b> out of Skyrim's portal."]],
    ['Nov 25, morning', ['<b>Every ability rebuilt from real parts</b>: Skyrim\'s, Dawnguard\'s, Dragonborn\'s, Apocalypse\'s and Mysticism\'s own particle effects, one new effect each, in its own colour and size.', '<b>Vahlok\'s chains</b> are real links that catch the dead and haul them in.', 'The class spells play their own; <b>your look picks</b> are in; <b>our own scythes</b> in the workshop for your OK.']],
    ['Nov 25, early hours', ['<b>Each weapon three looks of its own</b>: no add-ons, no shared families; pick one per weapon.', '<b>One custom effect per ability</b> (now replaced by the real-parts remake).', 'Conjurations keep Skyrim\'s portal, the Soulcaller its Magnus beam; the Gravebell stops pulsing.']],
    ['Nov 25, night', ['<b>Every weapon its own</b>: its own look signature over your pick, and its own hit effects on Skyrim\'s; the class spells and flasks their own impacts.', '<b>Skyrim\'s enchantment glow</b> under each look; every texture 2K, the wonders\' and Alduin\'s Bane\'s 4K.', 'Your rapier and ability picks in; the Mutagen Staff has no beam (recorded).']],
    ['Nov 25', ['<b>No hard cut-offs</b>: every look comes to a point at the tip and ends, in the weapon\'s shape; slanted sheets give it body; the still motes are gone.', '<b>Staffs on their heads</b>: the Radiant Quarterstaff glows only on its two heads, every tier.', '<b>Pillars and stars gone</b>: the relics\' old built-in effects taken off their models.', '<b>Your 26 look picks and 4 ability picks</b> in.', '<b>Skyrim\'s own effects</b> read from your game as the base for the remake.']],
    ['Nov 24, late', ['<b>Looks remade</b>: Flame rises hilt to tip, Storm is real lightning with no gaps, Mist softer, Runic retired; four new looks: Frost, Shadow, Radiance, Spirit.', '<b>Your 30 look picks</b> in the game; the Carian Wand covered whole and softer.', '<b>Tier models</b>: Ascended keeps Tier III\'s model; one model per tier for staff and rapier; the wand and scythe take their last model at Tier III.', '<b>The workshop</b>: drag to turn, scroll to zoom, a colour wheel, the ability effects rebuilt on GPU particles with light, a Road to Nexus tab, and this record of the mod.', '<b>Scythes 25% larger</b>, effects from guard to tip, smoother lightning, class voices out.']],
    ['Nov 24', ['<b>Workshop doc</b>: live weapon looks and abilities, test list (this page).', '<b>Weapon effects</b>: guard to tip on every blade; smoother lightning; scythes 25% larger; Carian Wand in mist.', '<b>Class voices</b> taken out (your pick); Felldir\'s altar voice stays.',
                        '<b>The soul</b> is a white fireball and <b>the ring</b> fills smoothly; the screen-drawn ring and runes are gone.', '<b>Cleanup</b>: Visual Animated Enchants, Vibrant Weapons Lite, Calling of the Conjurer, Summonable Spriggans, Gondorian Ranger and Black Mage Armor no longer read.',
                        '<b>Trials</b>: Hold the Light replaces Repeat the Echo; soul sound is a whisper and a thunk.', '<b>Javelin</b>: held in the hand, tap jabs and hold throws, a human overhand throw, trails you can follow. Mutagen Staff in the hands; the Wandering Star\'s tip in front.',
                        '<b>Our own weapon effects</b> replace Visual Animated Enchants: 24 relics and wonders and every class weapon tier, fitted to each model.', '<b>The Curse of Alduin by name</b> everywhere in the files.',
                        '<b>Tier III class sets</b> come alive: breathing trim and a trail of wisps. Class set sleeves and calves fixed.', '<b>Felldir</b>: a clear voice with a short stone echo, from the altar\'s centre stone; talk only at freed altars, centred subtitles.',
                        '<b>The ground</b>: trees, plants and rocks floating over nothing set down all over the map.', '<b>New character</b>: one smooth start with no sound under the loading screen; menu music from launch to the menu.']],
    ['Nov 23', ['<b>Talk with the Echo Altar</b>: four topics with Felldir\'s answers, opening as the story goes; his new voice on all 244 talk lines and 67 whispers.', '<b>Lore fixes</b>: Helgen beside Riverwood, the roads of the dead, King Harald\'s siege of Forelhost, what the Echo of Alduin is.',
                '<b>The dead come down red beams of light</b>, not up out of the ground, in every text.', '<b>The trials remade to the story</b>: Lay the Restless to Rest, The Stone\'s Whisper, Repeat the Echo (now Hold the Light), Break the Priest\'s Seals.',
                '<b>Co-op</b>: 1 to 4 players. Every kill pays whoever struck it last.', '<b>Death Hounds</b> in the night pool; summons kept; Tiny Terrors replaces Dance of Death.', '<b>New game</b>: one loading screen to the creator; choose Ralof or Hadvar and Survival Mode.',
                'Class voice lines recorded, wired in, then taken out of the release (kept in your Downloads).']],
    ['Nov 21–22', ['<b>The Wyrmspine Javelin</b> replaces the Staff of Magnus as the Destruction wonder.', '<b>The Summoner\'s conjurations</b> our own: 19 spells, effects and creatures.', '<b>Class armour sets</b> refitted to 3BA and HIMBO, with four tier looks.',
                   '<b>Jailor\'s Keyhammer</b> remade on Skyrim\'s Nordic Warhammer.', '<b>Dragon Slayer</b>: a plain hilt, matte darker bone, only its runes give light.', '<b>Perks</b>: all four slots open from night 1.', 'Our own launch loading screen (Alduin) and underwear on both bodies.']],
]
import json as _json
_MN = _json.load(open('/home/claude/bo3-zombies-skyrim/docs/workshop/modnow.json'))
MODNOW = _MN['sections']
NEXUS = _json.load(open('/home/claude/bo3-zombies-skyrim/docs/workshop/nexus.json'))['groups']
CHANGES = [
    ['The doc', 'Simple cards, one screen each', 'As you asked: Weapon looks, Abilities, Scythes and the new tabs are now small cards. Click one and it opens over the page with the 3D preview on one side and every option on the other - no scrolling up and down. Esc or Close puts it back.', 'ok'],
    ['Sounds', 'A sound workshop', 'Every weapon\'s own sounds (draw, swing, heavy swing, hit) and every ability\'s (cast, impact, what lingers): five options each from your own install - Skyrim, Dawnguard, Dragonborn, Apocalypse, Mysticism - and ours, the first what plays now, one marked my pick. Press ▶ to hear any; Hear it play runs the weapon in 3D with every sound on its beat. Volume, pitch, variation and a layer under each.', 'ok'],
    ['Feel', 'Hit feel', 'Hit-stop and camera shake by weapon weight, seen in the preview exactly as set; the hit markers and kill sounds.', 'ok'],
    ['The dead', 'How they die', 'Fire, frost, lightning, sun, poison, blood, shadow and plain kills, each with options from real parts and real sounds: burn to ash, shatter, disintegrate, turned to light, melt, drained husk, soul torn out, thrown by a heavy.', 'ok'],
    ['Trials', 'The Echo Altar and its trials in 3D', 'The altar, its ring of light at real size, the dead coming and the souls rising; edit each trial\'s words, numbers for each difficulty, colours and the sound of each moment.', 'ok'],
    ['Big nights', 'Your own game\'s numbers', 'Read from your CurseOfAlduin.log (the Nov 24 night): 0.6 ms a frame on average at 84 fps - but single frames of 40-85 ms in Classes, Gun, Director and Shops. Those stutters first, then the knobs.', 'ok'],
    ['Carried over', 'Your picks from the last doc', 'Your 34 look marks, 8 scythe OKs and 10 ability picks start here as you left them. Your four look changes (Krosis red o2, Volsung white o1, Cinderclaws orange o3, Silver Reaping moon o2) are the defaults now.', 'ok'],
    ['Scythes', 'The Dragon Priest scythe', 'The blade now comes from the head of the staff, as you asked. Needs your OK.', 'ok'],
    ['Abilities', 'The bolts in flight', 'Still pale in the previews: three fixes tried, not solved. In the Issues tab.', 'warn'],
    ['Tests', 'The master test list', 'Everything not yet seen in game: the last build your game ran was 6a0f8a0 (Nov 24, 7 pm), so everything since is on the list, with what to look for.', 'ok'],
]
ABJ = json.load(open('/home/claude/bo3-zombies-skyrim/docs/workshop/abilities.json'))
AB_PARTS = {}
_cnt = {}
for ab in ABJ['abilities']:
    _cnt[ab['card']] = _cnt.get(ab['card'], 0) + 1
    if ab['card'] in AB_PARTS: continue
    labels = []
    for pt in ab['parts']:
        if pt['label'] not in labels: labels.append(pt['label'])
    AB_PARTS[ab['card']] = {'parts': labels, 'note': ab.get('note', ''), 'many': False}
for k, n in _cnt.items():
    if n > 1 and k in ('rapier_light', 'rapier_heavy', 'javelin_land'): AB_PARTS[k]['many'] = True
AB_PARTS.setdefault('mutagen_flasks', AB_PARTS.get('ffire_land'))
SRCS = {}
for ab in ABJ['abilities']:
    for pt in ab['parts']: SRCS.setdefault(pt['label'].split(':')[0], set()).add(pt['label'].split(': ', 1)[1])
SKBASE = ''.join(f'<div class="sk"><div><b>{k}</b> <span class="to">{len(v)} effects</span></div><p class="d" style="margin:4px 0 0;font-size:13px;color:var(--muted)">{", ".join(sorted(v))}</p></div>' for k, v in sorted(SRCS.items(), key=lambda x: -len(x[1])))
import weaponlooks as WL
import realfx as RFX
REALFX = {'parts': {k: {r: p for r, p in v.items()} for k, v in RFX.PARTS.items()}, 'ring': RFX.RING, 'styles': {k: [list(r) for r in v] for k, v in RFX.STYLES.items()},
          'bigAt': RFX.BIG_AT, 'rampOf': RFX.RAMP_OF, 'cards': {}}
for _ab in RFX.all_abilities(): REALFX['cards'].setdefault(_ab['card'], {'theme': _ab['theme']})
import docshow as DSH
REALFX['shows'] = DSH.shows()
import abilityopts as AOP
REALFX['opts'] = AOP.options()
_SHOWNOTE = {'javelin_bounce': 'The javelin flies itself from one of the dead to the next, slow enough to follow, its element (fire here) streaming behind it.',
             'wand_heavy': 'Fire, lightning, ice in turn; the 2nd and 4th bolts here are great ones, each with its Tier III volley either side.',
             'staff_heavy': 'All white now: the bolt out of the sky, its burst, the turning pillar, the motes and the ring.',
             'soulcaller_wolves': 'Skyrim\'s own conjuring portal where each one comes.'}
for _k in REALFX['shows']: AB_PARTS[_k] = {'parts': DSH.parts_text(_k), 'note': _SHOWNOTE.get(_k, ''), 'many': False}
SCY = json.load(open('/home/claude/bo3-zombies-skyrim/build/scythes/info.json'))
WD = '/home/claude/bo3-zombies-skyrim/docs/workshop/'
import soundopts as SOP
SOUNDS = json.load(open(WD + 'sounds.json'))
FEEL = json.load(open(WD + 'feel.json')); DEATHS = json.load(open(WD + 'deaths.json')); TRIALS = json.load(open(WD + 'trials.json'))
PERF = json.load(open(WD + 'perf.json')); PERF['measured'] = json.load(open(WD + 'perf_measured.json')); ISSUES = json.load(open(WD + 'issues.json'))
_refs = set()
for d in DEATHS:
    for o in d['opts']: _refs.update(o['snd'])
for ev in TRIALS['common'] + [e for t in TRIALS['trials'] for e in t['events']]:
    for o in ev['opts']: _refs.update(o['snd'])
for m in FEEL['marks']:
    if m.get('snd'): _refs.add(m['snd'])
for r in _refs:
    if r not in SOUNDS['sounds']: SOUNDS['sounds'][r] = SOP.sound_entry(r)
for v in SOUNDS['sounds'].values(): v.pop('files', None)
# (what Noah set in the last doc, 8dDUqYQNG3UCCFYcrYsu7a: this doc's starting point)
import glob as _g
_old = '/tmp/claude-0/-home-claude/0ec68033-d637-57d9-8f95-48d7c09e98c8/scratchpad/db_2b/'
CARRY = {'abilities': json.load(open(WD + 'ability_picks2.json')), 'weaponlooks': {}, 'scythes': {}}
for f in _g.glob(_old + 'weaponlooks/*.json'):
    r = json.load(open(f)); r['choice'] = 'keep'; r['note'] = r.get('note', ''); CARRY['weaponlooks'][r['id']] = r
for f in _g.glob(_old + 'scythes/*.json'):
    r = json.load(open(f))
    if r['choice'] == 'ok': CARRY['scythes'][r['id']] = r
for k, r in CARRY['abilities'].items(): r.setdefault('id', k)
SNDFILES = sorted({m for v in SOUNDS['sounds'].values() for m in v['mp3']})
json.dump(SNDFILES, open(str(H / 'sndfiles.json'), 'w'))
s = (H / 'index.tpl.html').read_text()
js = (H / 'data.js').read_text().replace('__SHA__', SHA).replace('__COLS__', J(W.COL)).replace('__WCATS__', J(WC)).replace('__TESTS__', J(TESTS)).replace('__RECS__', J(RECS)) \
    .replace('__RECENT__', J(RECENT)).replace('__MODNOW__', J(MODNOW)).replace('__MODNOW_ABOUT__', J(_MN['about'] + ' Last updated: ' + _MN['updated'] + '.')).replace('__NEXUS__', J(NEXUS)).replace('__CHANGES__', J(CHANGES)).replace('__LOOK3__', J({k: [[o['name'], o['desc']] for o in v] for k, v in WL.LOOKS3.items()})).replace('__AB_PARTS__', J(AB_PARTS)).replace('__REALFX__', J(REALFX)).replace('__AB_PICKS__', J(json.load(open('/home/claude/bo3-zombies-skyrim/docs/workshop/ability_picks.json')))).replace('__SCY__', J(SCY)).replace('__CARRY__', J(CARRY)).replace('__SOUNDS__', J(SOUNDS)).replace('__FEEL__', J(FEEL)).replace('__TRIALS__', J(TRIALS)).replace('__DEATHS__', J(DEATHS)).replace('__PERF__', J(PERF)).replace('__ISSUES__', J(ISSUES))
js += '\n' + (H / 'real.js').read_text() + '\n' + (H / 'zombies.js').read_text() + '\n' + (H / 'app.js').read_text() + '\n' + (H / 'fx2.js').read_text() + '\n' + (H / 'ui.js').read_text() + '\n' + (H / 'sounds.js').read_text() + '\n' + (H / 'extra.js').read_text()
s = s.replace('__CSS__', (H / 'style.css').read_text()).replace('__JS__', js).replace('__SHA__', SHA).replace('__DATE__', 'NOV 25, 2026').replace('__INSTALL__', INSTALL).replace('__SKBASE__', SKBASE)
(H / 'page.html').write_text(s)
print('ok', len(s) // 1024, 'KB', sum(len(c['items']) for c in WC), 'looks', sum(len(l) for _, l in TESTS), 'tests')
