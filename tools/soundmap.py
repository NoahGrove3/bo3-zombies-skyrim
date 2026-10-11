"""Rebuild sheets/sounds.json so every sound the mod plays comes from the supplied Undying Nights SFX library
(clips cut by tools/sfx.py into Data/Sound/fx/UndyingNights/un/). No Skyrim sound effects are referenced.

    python tools/soundmap.py

Each sound: clips (random pick per play), shift (engine pitch, percent), space 2d/3d, cat (sfx/ui), loop, atten (dB).
The mapping is the Audio Assignment Map (docs/AUDIO_MAP.md is written from it)."""
import json, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
P = ROOT / 'sheets/sounds.json'
d = json.load(open(P))
old = {r['id']: r for r in d['rows']}
W = [w for w in json.load(open(ROOT / 'sheets/weapons.json'))['rows'] if w.get('kind') == 'tome' and not w.get('retired')]

def V(base, n): return [f'{base}_{i}' for i in range(n)]
FIRE, ICE, STRIKE, BRIGHT = V('sp_fire', 8), V('sp_ice', 5), V('sp_strike', 3), V('sp_bright', 4)
GHOST, WAIL, RUNNER = V('en_ghost', 3), V('en_wail', 2), V('en_runner_idle', 4)
QUAKE = V('boss_quake', 3)
BREATH, FLESH = V('en_breath', 6), V('flesh_hit', 3)
WALK, JOG, SPRINT = V('step_walk', 6), V('step_jog', 6), V('step_sprint', 5)

M = {}   # id -> (clips, shift, space, cat, loop, atten, use)
def S(id, clips, shift=0, space='2d', cat='sfx', loop=False, atten=0, use=''):
    M[id] = (clips if isinstance(clips, list) else [clips], shift, space, cat, loop, atten, use)

# ---- spells: per spell a quick cast, a charge hum (loop), the release, the bolt's impact and the gathered burst
SP = {  # id: (cast, castShift, loop, loopShift, unleash, unleashShift, hit, hitShift, boom, boomShift)
 'sidearm':      (BRIGHT, 15, 'sp_drone_loop', 30, 'champ_telegraph', 20, BRIGHT, 5, 'sp_void_hit', 25),
 'centurion':    ('sp_crunch', 10, 'sp_drone_loop', 10, GHOST, 10, 'sp_crunch_low', 15, 'en_burst', 10),
 'triad':        (ICE, 25, 'sp_holy_loop', 10, GHOST, 0, BRIGHT, -10, 'champ_telegraph', -10),
 'spider':       ('sp_crunch', 0, 'sp_drone_loop', -10, 'sp_bones', 15, 'sp_crunch_low', 0, 'sp_bones', 10),
 'drill':        (FIRE, -25, 'sp_drone_loop', -20, 'en_demon', 10, 'sp_crunch_low', -15, 'sp_void_hit', -10),
 'steampump':    ('sp_crunch', -15, 'sp_drone_loop', -25, 'sp_bones', 0, 'sp_crunch_low', -25, QUAKE, 15),
 'marksman':     ('sp_cold_whoosh', 10, 'sp_holy_loop', -15, ICE, -10, ICE, 10, ICE, -20),
 'animunculi':   (FIRE, 10, 'sp_drone_loop', 0, 'sp_hex', 0, 'sp_ignite_small', 15, 'sp_hex', 10),
 'kagrenac':     (BRIGHT, 25, 'sp_holy_loop', 25, 'champ_telegraph', 30, BRIGHT, 20, 'sp_starfall', 20),
 'mammothlmg':   ('champ_telegraph', -20, 'sp_drone_loop', -35, 'sp_void_hit', 0, 'sp_crunch_low', -30, 'sp_void_hit', -20),
 'longshot':     (BRIGHT, -5, 'sp_holy_loop', 0, 'sp_starfall', 0, STRIKE, 15, 'sp_thunder', 10),
 'aetherium':    (FIRE, 20, 'sp_holy_loop', 5, 'sp_holy_burst', 0, 'sp_ignite_small', 25, 'sp_holy_burst', 10),
 'flamerepeater':(FIRE, 0, 'sp_drone_loop', 15, 'sp_ignite', 0, 'sp_ignite_small', 0, QUAKE, 10),
 'frostlancer':  (ICE, 0, ['sp_beam_loop', 'sp_beam_loop2'], -30, 'sp_cold_whoosh', -10, ICE, -5, ICE, -25),
 'stormcaller':  ('sp_shock_cast', 0, ['sp_beam_loop', 'sp_beam_loop2'], 0, 'sp_thunder', 0, STRIKE, 0, 'sp_thunder', -10),
 'embermortar':  ('sp_wind_cast', 0, 'sp_holy_loop', -25, 'sp_wind_unleash', 0, 'sp_cold_whoosh', -20, 'sp_wind_unleash', -15),
 'wabbajack':    (BRIGHT, -15, 'sp_drone_loop', 35, 'champ_telegraph', 40, BRIGHT, -20, 'en_demon', 30),
 'skyforgearc':  ('sp_shock_cast', -10, ['sp_beam_loop', 'sp_beam_loop2'], -10, 'sp_thunder', -15, STRIKE, -10, 'sp_thunder', -25),
}
for w, (c, cs, l, ls, u, us, h, hs, b, bs) in SP.items():
    S('cast_' + w, c, cs, use=f'{w}: quick cast')
    S('charge_' + w, l, ls, loop=True, atten=4, use=f'{w}: charge hum (loops while both hands gather)')
    S('unleash_' + w, u, us, use=f'{w}: signature release')
    S('hit_' + w, 'hit_' + w, 0, '3d', use=f'{w}: bolt impact where it lands (its own sound, sheet sfx hit_{w})')
    S('boom_' + w, b, bs, '3d', use=f'{w}: gathered burst at the target')
S('charge_ready', 'sp_bright_1', -15, atten=2, use='full charge reached (both hands glow)')
S('charge_up', 'sp_cold_whoosh', 25, atten=8, use='charge begins (hands come up)')
S('charge_fizzle', 'sp_crunch', 20, atten=6, use='early release: the gathered power gutters out')
S('mana_empty', 'ui_close', -30, cat='ui', use='not enough magicka')
S('cast_heavy', 'sp_void_hit', -10, use='heavy cast (Black Hole collapse)')
S('hitmarker', 'sp_bright_0', 40, atten=10, use='a spell hit an undead')
S('headshot', 'sp_bright_2', 30, atten=6, use='a head hit')
S('knife', ['sp_cold_whoosh'], -25, use='quick knife swing')
for k in ('cast_light', 'cast_rapid', 'cast_scatter', 'cast_thuumcannon', 'cast_apprentice', 'reload_mag', 'reload_shell', 'dry_click',
          'shot_pistol', 'shot_smg', 'shot_ar', 'shot_lmg', 'shot_shotgun', 'shot_sniper', 'shot_ray', 'crossbow_fire', 'crossbow_reload', 'bow_nock'):
    S(k, BRIGHT, -10, use='(retired weapon sound, kept so old references stay quiet and in theme)')
for k in ('hearth', 'gildergreen', 'lorkhan', 'arkay', 'mirestaff', 'magnus', 'wolfqueen', 'ysgramor', 'nightingale', 'nightmare'):
    S('cast_' + k, BRIGHT, 0, use='(retired relic)'); S('unleash_' + k, 'sp_void_hit', 0, use='(retired relic)')
S('unleash_dread', GHOST, -20, use='(layer, retired)')
# ---- shouts (the Thu'um of each freed city)
for k, (c, s) in {'ember': ('sp_ignite', -10), 'gale': ('sp_wind_unleash', -10), 'wail': (WAIL, -15), 'rime': (ICE, -30),
                  'quake': (QUAKE, 0), 'shroud': (GHOST, -10), 'toll': ('sp_thunder', -20), 'mire': ('sp_crunch_low', -20),
                  'dread': ('sp_void_hit', -15), 'shatter': ('sp_bones', -10), 'roar': ('boss_growl', 10), 'dragon': ('boss_growl', 0)}.items():
    S('shout_' + k, c, s, use=f"Thu'um: {k}")
# ---- the undead (3D, from the body)
VO = {  # type: idle, attack, hurt, death
 'shambler': ((BREATH, -10), ('en_roar', -25), ('en_demon', -30), ('en_demon_low', -20)),
 'runner':   ((RUNNER, 0), ('en_runner_attack', 0), (RUNNER, 15), ('en_runner_death', 0)),
 'wailer':   ((GHOST, 0), (WAIL, 20), (WAIL, 35), ('en_hag_death', 0)),
 'bloater':  ((BREATH, -30), ('en_roar_hi', 0), ('en_roar', -20), ('en_demon_low', -30)),
 'stalker':  ((GHOST, 15), ('champ_telegraph', 20), (GHOST, 30), ('en_hag_death', -20)),
 'reaper':   (('en_roar', -55), ('champ_telegraph', -10), ('en_demon', -45), ('en_demon_low', -40)),
 'necro':    ((GHOST, -20), ('sp_hex', 10), ('en_demon', 0), ('en_demon', -10)),
 'knight':   (('en_roar', -50), ('boss_land', 20), ('en_demon', -35), ('en_demon_low', -35)),
 'king':     (('en_demon', -40), ('boss_land', 10), ('en_demon', -25), ('en_demon_low', -15)),
 'lich':     ((GHOST, -30), ('sp_hex', -10), ('en_demon', 15), ('en_demon_low', 0)),
}
for t, (i, a, h, de) in VO.items():
    for ev, (c, s) in (('idle', i), ('attack', a), ('hurt', h), ('death', de)):
        S(f'vo_{t}_{ev}', c, s, '3d', atten=4 if ev == 'idle' else 0, use=f'{t}: {ev}')
S('rise', GHOST, -50, '3d', atten=6, use='the dead climb out of the earth')
# footsteps: played on the body's own animation footfalls (BGSFootstepEvent), walk or run set by its gait
STEPS = {'shambler': ((WALK, -15), (JOG, -15)), 'runner': ((JOG, 5), (SPRINT, 5)), 'wailer': ((WALK, 10), (JOG, 10)),
         'bloater': ((WALK, -35), (JOG, -35)), 'stalker': ((SPRINT, 10), (SPRINT, 15)), 'reaper': ((JOG, -40), (JOG, -40)),
         'necro': ((WALK, -5), (WALK, -5)), 'knight': ((JOG, -25), (JOG, -25)), 'king': ((WALK, -30), (JOG, -30)),
         'lich': ((WALK, 0), (WALK, 0)), 'boss': ((JOG, -60), (JOG, -60))}
for t, ((wc, ws), (rc, rs)) in STEPS.items():
    S(f'step_{t}', wc, ws, '3d', atten=6, use=f'{t}: footstep (walking)')
    S(f'step_{t}_run', rc, rs, '3d', atten=4, use=f'{t}: footstep (running)')
S('flesh_hit', FLESH, 0, '3d', atten=2, use='an undead takes spell damage (wet flesh, under the spell\'s own impact)')
S('flesh_death', 'flesh_death', 0, '3d', use='an undead is killed (the body gives way)')
S('claw_swing', 'sp_cold_whoosh', -40, '3d', atten=4, use='an undead swings (claws, scythe)')
S('wailer_scream', WAIL, 0, '3d', use='Wailer screams: the horde comes faster')
S('bloater_burst', 'en_burst', 0, '3d', use='Bloater bursts')
S('grab', 'en_runner_attack', -10, '3d', use='(retired grab)')
# ---- Grimmvar, champions, elites
S('boss_growl', 'boss_growl', 0, '3d', use='Grimmvar growls (arrival, enraged)')
S('boss_breath_wind', 'boss_breath_wind', 0, '3d', use='Grimmvar breath telegraph (the in-breath)')
S('boss_breath', 'boss_breath', 0, '3d', use='Grimmvar fire breath')
S('boss_claw_wind', 'boss_growl', 15, '3d', use='Grimmvar raises his claws (telegraph)')
S('boss_claw', 'boss_land', 25, '3d', use='Grimmvar claw strike lands')
S('boss_leap_wind', 'boss_growl', -10, '3d', use='Grimmvar crouches to leap (telegraph)')
S('boss_land', 'boss_land', 0, '3d', use='Grimmvar lands from his leap')
S('boss_quake', QUAKE, 0, '3d', use='eruption: the ground bursts')
S('boss_quake_wind', 'champ_telegraph', -30, '3d', use='eruption telegraph (the ground rumbles)')
S('boss_death', 'boss_growl', -20, '3d', use='Grimmvar falls')
S('boss_arrive', 'boss_arrive', 0, use='Grimmvar rises (stinger)')
S('champ_arrive', 'champ_arrive', 0, use='a champion takes the field')
S('champ_telegraph', 'champ_telegraph', 0, '3d', use='champion ability wind-up (chain, summon, scream)')
S('champ_chain', ['sp_crunch_low'], -10, '3d', use='the Reaper chain bites')
S('champ_summon', GHOST, -25, '3d', use='a necromancer / lich raises the dead')
S('champ_enrage', 'en_roar', -30, '3d', use='a champion is enraged')
S('hound_night', 'boss_arrive', -5, use='special night begins (generic)')
# ---- nights and events
S('night_falls', 'sp_drone_loop', -40, use='dusk: the night is coming (warning)')
S('night_start', 'champ_arrive', -15, use='the night begins: the dead rise')
S('night_end', 'coffer_land', -10, use='dawn: the night survived')
S('round_start', 'champ_arrive', -15, use='(round start)')
S('round_end', 'coffer_land', -10, use='(round end)')
S('night_thunder', 'sp_thunder', -30, use='distant thunder (night ambience)')
S('event_bloodmoon', 'boss_arrive', -10, use='Blood Moon begins')
S('event_fog', 'night_fog', 0, use='Fog Night begins')
S('event_ash', 'night_ash', 0, use='Ash Storm begins')
S('event_champions', 'champ_arrive', 0, use='Night of Iron begins')
S('nuke', QUAKE, -15, use='Nuke power-up')
S('powerup_spawn', 'sp_bright_3', -20, '3d', use='a power-up drops')
S('powerup_grab', 'coffer_take', 0, use='a power-up taken')
# ---- the shrine and its roll, brews, the rites
S('coffer_open', 'ui_open', 0, '3d', use='shrine menu opens')
S('coffer_charge', 'coffer_charge', 0, '3d', use='the shrine gathers power (roll begins)')
S('coffer_spin', 'coffer_tick', 0, '3d', atten=2, use='a spell passes over the crown (cycling tick, rate-limited)')
S('coffer_land', 'coffer_land', 0, '3d', use='the shrine lands on a spell')
S('coffer_take', 'coffer_take', 0, use='you take the spell')
S('coffer_cheese', 'ui_close', -20, '3d', use='(retired)')
S('coffer_skull', 'ui_close', -20, '3d', use='(retired)')
S('buy', 'coffer_take', -10, use='points spent')
S('drink', 'sp_bright_1', -25, use='a brew drunk')
S('forge_upgrade', 'sp_ignite', 10, '3d', use='reforge')
S('forge_ritual', 'sp_ignite', -10, use='(reforge ritual)')
perkshift = [-30, -20, -10, 0, 10, 20, -25, -5, 15]
for i, k in enumerate(['trollshide', 'quicksilver', 'twinstrike', 'hearthfire', 'windrunner', 'hawkeye', 'mammoth', 'frostbite', 'stormcrown']):
    S('brew_' + k, BRIGHT, perkshift[i], '3d', use=f'brew bought: {k}')
S('ui_move', 'ui_move', 0, cat='ui', use='menu: choose')
S('ui_select', 'ui_select', 0, cat='ui', use='menu: select')
S('ui_close', 'ui_close', 0, cat='ui', use='menu: close / back')
S('quest_start', 'rite_start', 0, use='a rite begins')
S('quest_step', 'rite_step', 0, use='a rite step done')
S('rite_hum', 'rite_hum', 0, '3d', loop=True, atten=2, use='the ward / hold circle hums (loop while active)')
S('rite_reset', 'rite_reset', 0, use='the dead broke the circle: the timer resets')
S('rite_fail', 'rite_reset', -20, use='a rite failed')
S('ward_lit', 'sp_ignite_small', -10, '3d', use='a ward-fire kindled')
S('curse_lifted', 'shout_learned', -10, use='a city freed (with the music stinger)')
S('shout_learned', 'shout_learned', 0, use="a Thu'um learned")
# ---- the intro (plays once, after character creation; UI category: it plays while the game waits)
S('intro_helgen', 'boss_growl', -15, cat='ui', use='intro: the World-Eater over Helgen')
S('intro_dirge', 'boss_arrive', -10, cat='ui', use='intro: THE UNDYING DIRGE title hit')
S('intro_graves', GHOST, -30, cat='ui', use='intro: the graves wake')
S('intro_horde', 'en_roar', -20, cat='ui', use='intro: the horde')
S('intro_champions', 'champ_arrive', -10, cat='ui', use='intro: the champions')
S('intro_wyrm', 'boss_breath', -10, cat='ui', use='intro: Grimmvar in the fog')
S('intro_shrine', 'coffer_charge', -10, cat='ui', use='intro: the Shrine of the Dirge')
S('intro_title', 'champ_arrive', -20, cat='ui', use='intro: UNDYING NIGHTS')

rows = []
for id, (clips, shift, space, cat, loop, atten, use) in M.items():
    r = dict(old.get(id, {}))
    r.update(id=id, plugin='BO3Zombies.esp', localId='auto', editorId='UN_' + id, verify=[], space=space, cat=cat,
             file='fx\\UndyingNights\\un\\' + clips[0] + '.wav', files=['fx\\UndyingNights\\un\\' + c + '.wav' for c in clips],
             shift=shift, layer='none', atten=atten, loop=loop, use=use)
    rows.append(r)
missing = [k for k in old if k not in M]
if missing:
    raise SystemExit('unmapped: ' + ', '.join(missing))
d['rows'] = rows
d['columns'] = ['id', 'plugin', 'localId', 'editorId', 'file', 'space', 'cat', 'verify', 'files', 'shift', 'layer', 'atten', 'loop', 'use']
d['about'] = 'Every sound of the mod (SNDR). Clips come only from the supplied Undying Nights SFX library (tools/sfx.py, sheets/sfx.json). Rebuilt by tools/soundmap.py.'
json.dump(d, open(P, 'w'), indent=2)
print(len(rows), 'sounds')
