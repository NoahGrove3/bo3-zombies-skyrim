"""Undying Nights' dark fantasy sound pass: every sound of ours built from Skyrim's own effects (dragon, draugr, Daedric,
Dwemer, the destruction and illusion schools...), pitched down (SNDR frequency shift) and layered (a second sound played
with it). Nothing is copied: our sound descriptors point at files in the player's own Skyrim - Sounds.bsa
(tools/templates/vanilla_sounds.txt lists them, checked here).
    python tools/darksounds.py         rewrites sheets/sounds.json (rows with 'files' use vanilla files)"""
import json, pathlib
ROOT = pathlib.Path(__file__).parent.parent
have = {l.strip().lower() for l in open(ROOT / 'tools/templates/vanilla_sounds.txt', encoding='latin1')}
def f(*paths):
    out = []
    for p in paths:
        q = ('sound\\fx\\' + p).lower()
        assert q in have, 'missing in Skyrim - Sounds.bsa: ' + p
        out.append('fx\\' + p)
    return out
def n(base, k, ext='.wav'): return [f'{base}_{i:02d}{ext}' for i in range(1, k + 1)]
DR = 'npc\\dragon\\flightroar\\npc_dragon_flightroar'
M = {  # id: (files, pitch shift %, layer id, 2d/3d)
 # ---- the hand spells: a quick cast (cast_*) and the signature release (unleash_*)
 'cast_sidearm': (f('mag\\illusion\\release\\mag_illusion_release_01.wav'), -18, None),
 'cast_centurion': (f(*n('dlc02\\mag\\morasagony\\mag_morasagony_projectile_fire', 3)), -12, None),
 'cast_triad': (f('mag\\mysticism\\soultrap\\mag_mysticism_soultrap_capture.wav'), -15, None),
 'cast_spider': (f(*n('dlc02\\mag\\power\\morasgrasp\\mag_power_moras_grasp_projectile', 2)), -10, None),
 'cast_drill': (f('dlc01\\mag\\alteration\\mag_alteration_vampiredrainrelease_01.wav'), -12, None),
 'cast_steampump': (f(*n('npc\\skeleton\\attack\\npc_skeleton_attack', 3)), -25, 'cast_heavy'),
 'cast_marksman': (f('mag\\light\\mag_light_fire.wav'), -28, None),
 'cast_animunculi': (f('dlc02\\mag\\blackbookpowers\\mag_blackbookpowers_a_01.wav', 'dlc02\\mag\\blackbookpowers\\mag_blackbookpowers_c_01.wav'), -12, None),
 'cast_kagrenac': (f('mag\\illusion\\releaseaimed\\mag_illusion_release_aimed_01.wav'), -22, None),
 'cast_mammothlmg': (f('mag\\conjure\\mag_conjure_fire.wav'), -32, None),
 'cast_longshot': (f('mag\\alteration\\mag_alteration_release.wav'), -18, None),
 'cast_aetherium': (f('dlc01\\mag\\destruction\\stendarrsaura\\mag_destruction_stendarrsaura_fire.wav'), -8, None),
 'cast_flamerepeater': (f('mgk\\firebolt\\mgk_firebolt_03_fire.wav'), -12, None),
 'cast_frostlancer': (f('mag\\icicle\\fire\\mag_icicle_fire.wav'), -15, None),
 'cast_stormcaller': (f(*n('mag\\shock\\concentrationbolt\\mag_shock_concentration_bolt', 3)), -12, None),
 'cast_embermortar': (f('voc\\shout\\projectile\\push\\voc_shout_projectile_push.wav'), -18, None),
 'cast_wabbajack': (f('mag\\illusion\\release\\mag_illusion_release_01.wav'), -40, None),
 'cast_skyforgearc': (f(*n('amb\\weather\\thunder\\amb_weather_extrathunder', 4)), -5, None),
 'cast_heavy': (f('mag\\conjure\\impact\\mag_conjure_impact_01.wav'), -30, None),
 'cast_light': (f('mag\\illusion\\release\\mag_illusion_release_01.wav'), -18, None),
 'cast_rapid': (f('mgk\\firebolt\\mgk_firebolt_03_fire.wav'), -15, None),
 'cast_scatter': (f(*n('mag\\shock\\concentrationbolt\\mag_shock_concentration_bolt', 3)), -15, None),
 'unleash_sidearm': (f('mag\\conjure\\impact\\mag_conjure_impact_01.wav'), -25, 'cast_sidearm'),
 'unleash_centurion': (f(*n('dlc02\\mag\\morasagony\\mag_morasagony_projectile_impact', 3)), -15, 'unleash_dread'),
 'unleash_triad': (f('mag\\mysticism\\soultrap\\mag_mysticism_captureshader.wav'), -20, 'unleash_dread'),
 'unleash_animunculi': (f(*n('dlc02\\fx\\explosion\\fx_explosion_fishman_tentacle', 2)), -15, 'unleash_dread'),
 'unleash_mammothlmg': (f('dlc01\\qst\\soulcairnportal\\qst_soul_cairn_portal_open_01.wav'), -20, None),
 'unleash_kagrenac': (f('mag\\illusion\\charm\\mag_illusion_charm_on_01.wav'), -25, 'unleash_dread'),
 'unleash_longshot': (f('dlc02\\obj\\standingstone\\obj_standing_stone_explode_01_mono.wav'), -10, None),
 'unleash_aetherium': (f(*n('dlc01\\fx\\explosions\\aurielssun\\fx_explosion_aurielssun_near', 2)), -10, None),
 'unleash_flamerepeater': (f(*n('fx\\explosion\\firestorm\\fx_explosion_firestorm_near', 2)), -12, None),
 'unleash_frostlancer': (f('mag\\destruction\\frostbitedragon\\mag_destruction_forstbitedragon_explosion_01.wav'), -12, None),
 'unleash_stormcaller': (f(*n('amb\\weather\\thunder\\amb_weather_extrathunder', 4)), -12, None),
 'unleash_embermortar': (f(*n('qst\\hrothgarpushback\\qst_windhrothgarpushback', 2)), -12, None),
 'unleash_dread': (f(*n(DR, 5)), -22, None),
 'charge_up': (f('mag\\conjure\\mag_conjuration_charge_050.wav'), -30, None),
 'charge_ready': (f('mag\\mysticism\\soultrap\\mag_mysticism_captureshader.wav'), -25, None),
 'mana_empty': (f('mgk\\firebolt\\mag_firebolt_fail.wav'), -20, None),
 # ---- the Thu'ums: a dragon's roar under every word, the city's element over it
 'shout_ember': (f(*n('fx\\explosion\\firestorm\\fx_explosion_firestorm_near', 2)), -10, 'shout_roar'),
 'shout_gale': (f(*n('qst\\hrothgarpushback\\qst_windhrothgarpushback', 2)), -8, 'shout_roar'),
 'shout_wail': (f('npc\\wisp\\npc_wisp_ambush.wav'), -20, 'shout_roar'),
 'shout_rime': (f('mag\\destruction\\frostbitedragon\\mag_destruction_forstbitedragon_explosion_01.wav'), -10, 'shout_roar'),
 'shout_quake': (f('npc\\dragon\\landcrash\\long\\npc_dragon_landcrash_long.wav'), -15, 'shout_roar'),
 'shout_shroud': (f('mag\\illusion\\invisibility\\mag_illusion_invisibility_in.wav'), -25, 'shout_roar'),
 'shout_toll': (f('dlc01\\qst\\stonetombserana\\qst_serana_tomb_puzzle_solve_01.wav'), -20, 'shout_roar'),
 'shout_mire': (f(*n('dlc02\\mag\\morasagony\\mag_morasagony_projectile_impact', 3)), -18, 'shout_roar'),
 'shout_dread': (f(*n('dlc01\\npc\\dogdeathhound\\npc_dogdeathhound_attack_a', 2)), -25, 'shout_roar'),
 'shout_shatter': (f('dlc02\\obj\\standingstone\\obj_standing_stone_explode_01_mono.wav'), -12, 'shout_roar'),
 'shout_roar': (f('voc\\shout\\projectile\\push\\voc_shout_projectile_push.wav'), -20, 'shout_dragon'),
 'shout_dragon': (f(*n(DR, 5)), -15, None),
 # ---- the dead
 'rise': (f(*[f'npc\\draugr\\alcoveexit\\a\\npc_draugr_alcoveexit_a_{i:02d}.wav' for i in (1, 2)], *[f'npc\\draugr\\alcoveexit\\b\\npc_draugr_alcoveexit_b_{i:02d}.wav' for i in (1, 2)]), -22, None),
 'hound_night': (f(*n('npc\\dragon\\attack\\npc_dragon_attack', 4)), -18, None),
 'wailer_scream': (f('npc\\wisp\\npc_wisp_ambush.wav'), -28, None),
 'bloater_burst': (f(*n('dlc02\\fx\\explosion\\fx_explosion_fishman_tentacle', 2)), -25, None),
 'grab': (f(*n('fx\\melee\\clawmedium\\flesh\\fx_melee_clawmedium_flesh', 2)), -20, None),
 'knife': (f(*n('fx\\melee\\clawsmall\\flesh\\fx_melee_clawsmall', 4)), -15, None),
 'hitmarker': (f(*n('fx\\melee\\punchmedium\\fx_melee_punchmedium', 3)), -35, None),
 'headshot': (f(*n('fx\\melee\\metalimpact\\large\\flesh\\fx_melee_metalimpactlarge_flesh', 2)), -25, None),
 # ---- the station: the Coffer, the Brew Machine, the Reforge
 'coffer_spin': (f('dlc02\\mag\\blackbookpowers\\mag_blackbookpowers_b_01.wav'), -22, 'coffer_open'),
 'coffer_open': (f('drs\\drs_coffinnightmother_open.wav'), -15, None),
 'coffer_cheese': (f(*n('npc\\wisp\\npc_wisp_death', 2)), -25, None),
 'coffer_skull': (f('dlc01\\qst\\arkngthamzpuzzle\\qst_arkngthamz_puzzle_fail.wav'), -15, 'hound_night'),
 'buy': (f('ui\\enchanting\\ui_enchanting_learneffect_01.wav'), -18, None),
 'drink': (f(*n('npc\\human\\drink\\npc_human_drink', 5)), -10, None),
 'forge_upgrade': (f(*n('ui\\smithing\\ui_smithing_improveweapon', 2)), -15, 'forge_ritual'),
 'forge_ritual': (f('mag\\altars\\mag_altars_blessing_fire_01_2d.wav'), -20, None),
 **{f'brew_{k}': (f('dlc02\\mag\\mag_standingstoneactivate_a_01.wav' if i % 2 else 'dlc02\\mag\\mag_standingstoneactivate_b_01.wav'), -6 - 3 * i, None)
    for i, k in enumerate(['trollshide', 'quicksilver', 'twinstrike', 'hearthfire', 'windrunner', 'hawkeye', 'mammoth', 'frostbite', 'stormcrown'])},
 # ---- the nights and the Dirge
 'night_start': (f(*n('npc\\wolf\\howl\\npc_wolf_howl', 3)), -15, 'night_thunder'),
 'night_falls': (f(*n('npc\\wolf\\howl\\npc_wolf_howl', 3)), -25, 'night_thunder'),
 'night_thunder': (f(*n('amb\\weather\\thunder\\amb_weather_thunder_distant', 4)), -10, None),
 'night_end': (f('dlc01\\qst\\stonetombserana\\qst_serana_tomb_puzzle_solve_01.wav'), -5, None),
 'quest_start': (f('dlc02\\qst\\blackbookrewards\\qst_black_book_open_01.wav'), -15, None),
 'quest_step': (f('dlc02\\mag\\mag_standingstoneblessing_01.wav'), -12, None),
 'curse_lifted': (f('qst\\sovengardeportal\\qst_sovengarde_portal_open_01.wav'), -8, 'shout_dragon'),
 'ward_lit': (f('mag\\altars\\mag_altars_blessing_fire_02_2d.wav'), -15, None),
 'nuke': (f(*n('fx\\explosion\\firestorm\\fx_explosion_firestorm_near', 2)), -25, 'shout_dragon'),
 'powerup_spawn': (f('dlc02\\mag\\mag_standingstoneactivate_b_01.wav'), -20, None),
 'powerup_grab': (f('ui\\enchanting\\ui_enchanting_learneffect_01.wav'), -10, None),
}
S = json.load(open(ROOT / 'sheets/sounds.json'))
rows = {r['id']: r for r in S['rows']}
for k, v in M.items():
    files, shift, layer = v
    r = rows.get(k)
    if not r:
        r = {'id': k, 'plugin': 'BO3Zombies.esp', 'localId': 'auto', 'editorId': 'UN_' + k, 'file': '', 'space': '3d' if k in ('wailer_scream', 'bloater_burst', 'grab') else '2d', 'verify': []}
        S['rows'].append(r); rows[k] = r
    r.update(plugin='BO3Zombies.esp', localId='auto', editorId='UN_' + k, file=files[0], files=files, shift=shift, layer=layer or 'none')
for c in ('files', 'shift', 'layer'):
    if c not in S['columns']: S['columns'].append(c)
for r in S['rows']:
    r.setdefault('files', []); r.setdefault('shift', 0); r.setdefault('layer', 'none')
for r in S['rows']:
    assert r['layer'] == 'none' or r['layer'] in rows, r['layer']
S['about'] = "Sound descriptors. Rows with plugin BO3Zombies.esp are ours: with 'files', Skyrim's own effects from the player's Skyrim - Sounds.bsa (tools/darksounds.py: pitched by 'shift' percent, 'layer' another of ours played with it); without, synthesized by tools/synth.py into Data/Sound/fx/UndyingNights. The rest are Skyrim's own descriptors."
open(ROOT / 'sheets/sounds.json', 'w').write(json.dumps(S, indent=2, ensure_ascii=False) + '\n')
print(len(M), 'sounds remade from Skyrim files')
