"""Each weapon's, bolt's, flask's and class spell's own effects in the effects map (Noah, 2026-11-25: "there needs to be more
veriety of weapon abilities ... so we arent just recycling the same effect with a different color"; "I want the class spells and
flasks done too. using skyrim spells and effects as the base of it like the other weapons"). Writes the block at the end of
assets/fx/SKSE/Plugins/CurseOfAlduin/fxmap.ini (between its markers), read by src/Gun.cpp (FxScope / OwnFx).

    python3 tools/fxown.py
"""
import pathlib
# owner_kind -> [(model, opts)] ; the effects map's own entries (Noah, 2026-11-25)
R = 'effects\\mg07labyrinthianshockwave.nif'
OWN = {
 # ---- the Dragon Priest relics
 'priest_vahlok_hit_burst': [('magic\\paralyzeimpact01.nif', 'pal=green s=1'), ('effects\\impacteffects\\fxmetalsparkimpactblunt.nif', 's=0.8')],
 'priest_vahlok_heavy_burst': [('magic\\absorbgreenimpact01.nif', 's=1.2'), ('magic\\paralyzeimpact01.nif', 'pal=green s=1.4')],
 'priest_vahlok_heavy_ring': [(R, 'pal=green s=0.4')],
 'priest_nahkriin_hit_burst': [('magic\\explosionillusionlight01.nif', 'pal=white s=0.6')],
 'priest_nahkriin_heavy_burst': [('effects\\fxwaterfallmistblastlite.nif', 'pal=white s=0.8'), ('magic\\explosionillusionlight01.nif', 'pal=white s=1')],
 'priest_volsung_hit_burst': [('magic\\shoutareaexplosionsmall01.nif', 'pal=white s=0.5')],
 'priest_volsung_heavy_burst': [('magic\\shoutareaexplosion01.nif', 'pal=shock s=0.7')],
 'priest_volsung_heavy_ring': [(R, 'pal=blue s=0.45')],
 'priest_zahkriisos_hit_burst': [('effects\\impacteffects\\impactfrost01.nif', 's=1'), ('magic\\icicleimpactactor01.nif', 's=0.8')],
 'priest_zahkriisos_heavy_burst': [('magic\\explosionfrost01.nif', 's=1.2'), ('effects\\fxicewraithdeathexplosion.nif', 's=0.9')],
 'priest_zahkriisos_heavy_ring': [(R, 'pal=iceblue s=0.4')],
 'priest_otar_hit_burst': [('magic\\impactshock02.nif', 's=1')],
 'priest_otar_heavy_burst': [('magic\\explosionshockmass01.nif', 's=0.9'), ('magic\\impactshock02explo.nif', 's=1')],
 'priest_otar_heavy_ring': [(R, 'pal=shock s=0.4')],
 'priest_rahgot_hit_burst': [('effects\\impacteffects\\magpoisonsprayimpact.nif', 's=1')],
 'priest_rahgot_heavy_burst': [('dlc02\\effects\\poisongasexplosion.nif', 's=0.8')],
 'priest_krosis_hit_burst': [('magic\\absorbspellhiteffect01.nif', 'pal=red s=1'), ('dlc01\\magic\\dlc1vampiredrainimpact.nif', 's=0.8')],
 'priest_krosis_heavy_burst': [('magic\\explosionilusiondark01.nif', 'pal=red s=1')],
 'priest_krosis_heavy_ring': [(R, 'pal=red s=0.4')],
 'priest_hevnoraak_hit_burst': [('effects\\impacteffects\\fxmetalsparkimpactblunt.nif', 's=1.2'), ('magic\\absorbgreenimpact01.nif', 's=0.7')],
 'priest_hevnoraak_heavy_burst': [('effects\\fxdustburstmed.nif', 's=1'), ('magic\\absorbgreenimpact01.nif', 's=1.2')],
 'priest_hevnoraak_heavy_ring': [(R, 'pal=green s=0.4')],
 'priest_vokun_hit_burst': [('magic\\illusionimpact01.nif', 'pal=purple s=1')],
 'priest_vokun_heavy_burst': [('magic\\explosionilusiondark01.nif', 'pal=shadow s=1.2'), ('effects\\fxwaterfallmistblastlite.nif', 'pal=purple s=0.7')],
 'priest_morokei_hit_burst': [('magic\\absorbblueimpact01.nif', 'pal=drain s=1')],
 'priest_morokei_heavy_burst': [('magic\\impactmg07staffofmagnus.nif', 's=1.2')],
 'priest_morokei_heavy_ring': [(R, 'pal=drain s=0.4')],
 # ---- the Tablet's weapons
 'tab_stormtalons_hit_burst': [('magic\\impactshock01.nif', 's=0.9')],
 'tab_stormtalons_heavy_burst': [('magic\\explosionatronachstormattack.nif', 's=0.8')],
 'tab_gravebell_hit_burst': [('magic\\turnunimpact01.nif', 'pal=gold s=1')],
 'tab_gravebell_hit_ring': [(R, 'pal=gold s=0.4')],
 'tab_gravebell_heavy_burst': [('magic\\turnunimpact01.nif', 'pal=gold s=1.3')],
 'tab_gravebell_heavy_ring': [(R, 'pal=gold s=0.45')],
 'tab_talons_hit_burst': [('effects\\impacteffects\\bloodspraylargemist01.nif', 's=0.8'), ('dlc01\\magic\\dlc1vampiredrainspray01.nif', 'pal=blood s=0.7')],
 'tab_talons_heavy_burst': [('effects\\impacteffects\\bloodspraylargemist01.nif', 's=1'), ('dlc01\\magic\\dlc1vampiredrainspray01.nif', 'pal=blood s=0.9')],
 'tab_riverstone_hit_burst': [('effects\\fxdustburstsm.nif', 's=1.2')],
 'tab_riverstone_heavy_burst': [('effects\\fxdustburstmed.nif', 's=1')],
 'tab_riverstone_heavy_blast': [('blast:giantslam', ''), ('effects\\fxdirtgiantexplosion.nif', 's=0.6')],
 'tab_wyrmspine_hit_burst': [('effects\\impacteffects\\fxdragonfireimpactactor.nif', 'pal=fear s=0.7')],
 'tab_wyrmspine_heavy_burst': [('effects\\impacteffects\\fxdragonlandingimpact.nif', 's=0.5')],
 'tab_wyrmspine_heavy_ring': [(R, 'pal=fear s=0.4')],
 'tab_lanternwick_hit_burst': [('magic\\icicleimpactactor01.nif', 's=1')],
 'tab_lanternwick_heavy_burst': [('magic\\explosionfrost01.nif', 's=0.8')],
 'tab_cinderclaws_hit_burst': [('effects\\impacteffects\\fireballimpact01.nif', 's=0.6')],
 'tab_cinderclaws_heavy_burst': [('effects\\impacteffects\\fxdragonfireimpact.nif', 's=0.5')],
 'tab_eclipse_hit_burst': [('dlc01\\effects\\dlc1eclipseburstfx01.nif', 's=0.6')],
 'tab_eclipse_heavy_burst': [('dlc01\\magic\\dlc1sunburstfx01.nif', 's=0.8'), ('magic\\explosionfrost01.nif', 's=0.9')],
 'tab_eclipse_heavy_ring': [(R, 'pal=iceblue s=0.45')],
 'tab_wanderstar_hit_burst': [('dlc01\\effects\\dlc1aurielsskyboltexplosion01.nif', 's=0.4')],
 'tab_wanderstar_heavy_burst': [('dlc01\\effects\\dlc1aurielsskyboltexplosion01.nif', 's=0.6')],
 'tab_frostmourn_hit_burst': [('effects\\impacteffects\\fxwpniceimpactsm.nif', 's=1.2')],
 'tab_frostmourn_heavy_burst': [('magic\\explosionfrost01.nif', 'pal=iceblue s=1')],
 'tab_frostmourn_heavy_ring': [(R, 'pal=frost s=0.4')],
 'tab_silverscythe_hit_burst': [('magic\\explosionillusionlight01.nif', 'pal=moon s=0.7')],
 'tab_silverscythe_heavy_burst': [('magic\\explosionillusionlight01.nif', 'pal=moon s=0.9')],
 'tab_bonereaper_hit_burst': [('magic\\reanimateimpact01.nif', 's=1')],
 'tab_bonereaper_heavy_burst': [('magic\\reanimateimpact01.nif', 's=1.3')],
 'tab_wyrmtooth_hit_burst': [('effects\\impacteffects\\bloodsprayimpact01.nif', 's=1.2')],
 'tab_wyrmtooth_heavy_burst': [('effects\\impacteffects\\fxdragontailstompdust.nif', 's=0.6')],
 'tab_wyrmtooth_heavy_ring': [(R, 'pal=red s=0.45')],
 'tab_bloodmoon_hit_burst': [('dlc01\\magic\\dlc1vampdraintargetpointfx.nif', 's=1')],
 'tab_bloodmoon_heavy_burst': [('dlc01\\effects\\dlc1bloodexplosion500.nif', 's=0.5')],
 # ---- Alduin's Bane (its meteors land in its scope)
 'bane_light_burst': [('effects\\fxalduinexplosion.nif', 's=0.35')],
 'bane_light_blast': [('blast:fireball', ''), ('effects\\fxalduinexplosion.nif', 's=0.3')],
 'bane_light_ring': [(R, 'pal=fire s=0.45')],
 'bane_heavy_burst': [('magic\\shoutareaexplosion01.nif', 'pal=white s=0.8')],
 'bane_heavy_ring': [(R, 'pal=white s=0.5')],
 # ---- the class weapons' bolts (by their own model's name)
 'arc_purple_burst': [('effects\\impacteffects\\fxdragonfireimpactactor.nif', 'pal=violet s=0.8'), ('magic\\explosionilusiondark01.nif', 'pal=shadow s=0.5')],
 'arc_carian_burst': [('magic\\absorbblueimpact01.nif', 'pal=purple s=0.9')],
 'arc_white_burst': [('dlc01\\magic\\dlc1impactsunfirespell.nif', 'pal=white s=1'), ('dlc01\\magic\\dlc1sunburstfx01.nif', 's=0.4')],
 'arc_fire_burst': [('effects\\impacteffects\\fireballimpact01.nif', 's=0.7')],
 'arc_red_burst': [('effects\\impacteffects\\fxdragonfireimpact.nif', 'pal=red s=0.5')],
 'arc_frost_burst': [('magic\\icicleimpact01.nif', 's=1')],
 'arc_shock_burst': [('magic\\impactshock03.nif', 's=1')],
 'arc_sun_burst': [('dlc01\\magic\\dlc1impactsunfirespell.nif', 's=0.8')],
 'arc_blood_burst': [('apocnew\\illusionblue_explosion.nif', 's=0.6'), ('magic\\absorbblueimpact01.nif', 'pal=arcane s=0.6')],
 'arc_arcane_burst': [('apocnew\\illusionblue_explosion.nif', 'pal=arcane s=0.7')],
 'arc_gold_burst': [('effects\\impacteffects\\fxdragonfireimpactactor.nif', 'pal=gold s=1'), ('dlc01\\effects\\dlc1aurielsskyboltexplosion01.nif', 'pal=gold s=0.3')],
 'arc_bane_burst': [('magic\\shoutareaexplosion01.nif', 's=0.5')],
 # ---- the heavy cyclones (their touches and their end)
 'firecyclone_burst': [('apocnew\\destructionwizardfire_explosion.nif', 'pal=violet s=0.7'), ('effects\\mgmagicfirepillarsmall.nif', 'pal=violet s=0.5')],
 'cyclone_fire_burst': [('effects\\impacteffects\\fxdragonfireimpact.nif', 's=0.4')],
 'cyclone_frost_burst': [('magic\\icicleimpactactor01.nif', 's=0.9')],
 'cyclone_shock_burst': [('magic\\impactshock02.nif', 's=0.8')],
 'cyclone_storm_burst': [('magic\\explosionshock01.nif', 's=0.6')],
 'cyclone_white_burst': [('dlc01\\magic\\dlc1impactsunfirespell.nif', 's=0.8')],
 'cyclone_violet_burst': [('apocnew\\destructionwizardfire_explosion.nif', 'pal=violet s=0.6')],
 'cyclone_blood_burst': [('apocnew\\illusionblue_explosion.nif', 'pal=arcane s=0.5')],
 # ---- the Wyrmspine Javelin, by the element it flies in
 'fly_fire_burst': [('effects\\impacteffects\\fxdragonfireimpactactor.nif', 's=0.9')],
 'fly_frost_burst': [('magic\\icicleimpactactor01.nif', 's=1')],
 'fly_shock_burst': [('magic\\impactshock02explo.nif', 's=0.8')],
 'fly_wind_burst': [('magic\\shoutareaexplosionsmall01.nif', 's=0.6')],
 'fly_sun_burst': [('dlc01\\magic\\dlc1impactsunfirespell.nif', 's=1')],
 'fly_blood_burst': [('apocnew\\illusionblue_explosion.nif', 'pal=arcane s=0.6')],
 'fly_void_burst': [('magic\\absorbredimpact01.nif', 's=1')],
 'fly_poison_burst': [('effects\\impacteffects\\magpoisonsprayimpact.nif', 's=1')],
 # ---- the Alchemist's flasks (where each one breaks)
 'flask_fire_burst': [('effects\\impacteffects\\fireballimpact01.nif', 's=0.8'), ('effects\\trapoillampexplosion.nif', 's=0.5')],
 'flask_frost_burst': [('magic\\explosionfrost01.nif', 's=0.7')],
 'flask_shock_burst': [('magic\\explosionshock01.nif', 's=0.7')],
 'flask_poison_burst': [('dlc02\\effects\\poisongasexplosion.nif', 's=0.5')],
 'flask_sun_burst': [('magic\\turnunimpact01.nif', 'pal=gold s=1'), ('dlc01\\magic\\dlc1impactsunfirespell.nif', 'pal=gold s=0.8')],
 'flask_blood_burst': [('magic\\absorbredimpact01.nif', 's=1'), ('dlc01\\magic\\dlc1vampireabsorbexplosion.nif', 's=0.5')],
 'flask_philosopher_burst': [('magic\\explosionillusionlight01.nif', 'pal=gold s=1')],
}
SPELLS = {   # each class spell's own impact over the spell's own (fxmap w\spell_<name>_spell_hit)
 'Firebolt': ('effects\\impacteffects\\fireballimpact01.nif', 's=0.4'), 'Fireball': ('effects\\impacteffects\\fxdragonfireimpact.nif', 's=0.4'),
 'Incinerate': ('effects\\impacteffects\\fxdragonfireimpactactor.nif', 's=0.8'), 'Immolate': ('effects\\mgmagicfirepillarsmall.nif', 's=0.4'),
 'Ice Spike': ('magic\\icicleimpact01.nif', 's=0.8'), 'Ice Storm': ('magic\\impactfrosticestorm.nif', 's=0.5'), 'Icy Spear': ('magic\\icicleimpactfast01.nif', 's=1'),
 'Throat of the World': ('effects\\fxicewraithdeathexplosion.nif', 's=0.6'), 'Chain Lightning': ('magic\\impactshock01.nif', 's=0.8'),
 'Shock Nova': ('magic\\explosionshock01.nif', 's=0.6'), 'Finger of the Mountain': ('magic\\impactshock02explo.nif', 's=0.8'),
 'Solar Flare': ('dlc01\\magic\\dlc1impactsunfirespell.nif', 's=0.7'), 'Sunbeam': ('dlc01\\magic\\dlc1sunburstfx01.nif', 's=0.3'),
 'Forbidden Sun': ('dlc01\\effects\\dlc1aurielsskyboltexplosion01.nif', 's=0.35'), 'Daybreak': ('magic\\turnunimpact01.nif', 'pal=sun s=1'),
 'Arcane Bolt': ('magic\\absorbblueimpact01.nif', 'pal=arcane s=0.7'), 'Mana Void': ('magic\\explosionilusiondark01.nif', 'pal=arcane s=0.6'),
 'Lunar Tide': ('magic\\explosionillusionlight01.nif', 'pal=moon s=0.7'), 'Apocalypse': ('magic\\alterposimpact01.nif', 'pal=arcane s=1'),
 'Wall of Flames': ('effects\\impacteffects\\fireballimpact01.nif', 'pal=orange s=0.3'), 'Wall of Frost': ('magic\\icicleimpactactor01.nif', 's=0.6'),
 'Wall of Storms': ('magic\\impactshock03.nif', 's=0.6'), 'Inferno': ('effects\\impacteffects\\fxdragonfireimpactactor.nif', 's=0.5'),
 'Frost Nova': ('effects\\fxicewraithdeathexplosion.nif', 's=0.45'), 'Lightning Storm': ('magic\\impactshock02.nif', 's=0.6'),
 'Multivortex': ('magic\\alterposimpact01.nif', 'pal=arcane s=0.6'), 'Lightning Strike': ('magic\\explosionatronachstormattack.nif', 's=0.5'),
 'Cinder Storm': ('effects\\impacteffects\\fireballimpact01.nif', 'pal=fire s=0.5'),
 'Burning Touch': ('effects\\impacteffects\\fxdragonfireimpactactor.nif', 's=0.5'), 'Flame Touch': ('effects\\impacteffects\\fireballimpact01.nif', 's=0.5'),
 'Fiery Grasp': ('effects\\impacteffects\\fxdragonfireimpact.nif', 's=0.45'), 'Chilling Touch': ('effects\\impacteffects\\impactfrostsmall01.nif', 's=1'),
 'Icy Touch': ('effects\\impacteffects\\fxwpniceimpactsm.nif', 's=1'), 'Freezing Grasp': ('magic\\icicleimpactactor01.nif', 's=0.8'),
 'Shocking Touch': ('magic\\impactshock01.nif', 's=0.6'), 'Static Touch': ('magic\\impactshock03.nif', 's=0.6'), 'Thundering Grasp': ('magic\\impactshock02explo.nif', 's=0.6'),
 'Sunlight Touch': ('dlc01\\magic\\dlc1impactsunfirespell.nif', 's=0.5'), 'Radiant Touch': ('magic\\turnunimpact01.nif', 'pal=sun s=0.7'),
 'Searing Grasp': ('dlc01\\magic\\dlc1impactsunfirespell.nif', 'pal=gold s=0.9'), 'Arcane Edge': ('magic\\absorbblueimpact01.nif', 'pal=arcane s=0.5'),
 'Arcane Brand': ('magic\\illusionimpact01.nif', 'pal=arcane s=0.7'), 'Arcane Cascade': ('magic\\alterposimpact01.nif', 'pal=arcane s=0.7'),
}
def slug(n): return 'spell_' + ''.join(c.lower() if c.isalnum() else '_' for c in n)


MARK0 = '# ==== own effects (tools/fxown.py) ===='
MARK1 = '# ==== end own effects ===='


def write():
    f = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'fx' / 'SKSE' / 'Plugins' / 'CurseOfAlduin' / 'fxmap.ini'
    txt = f.read_text()
    if MARK0 in txt: txt = txt[:txt.index(MARK0)].rstrip('\n') + '\n'
    L = ['', MARK0, '# each weapon\'s, bolt\'s, flask\'s and class spell\'s own effects: Skyrim\'s own impact or explosion chosen for what it',
         '# does, in its own colour, often with a second of Skyrim\'s over it. While its blow, bolt, flask or spell is shown (src/Gun.cpp',
         '# FxScope), w\\<owner>_<kind>.nif stands in for the shared effect of that kind (burst, ring, blast...)',
         'CurseOfAlduin\\fx\\spell_hit.nif = none   # (a class spell with no impact of its own here: only the spell\'s own)']
    line = lambda k, parts: 'CurseOfAlduin\\fx\\w\\' + k + '.nif = ' + ' ; '.join((m + (' ' + o if o else '')) for m, o in parts)
    for k, v in OWN.items(): L.append(line(k, v))
    for n, (m, o) in SPELLS.items(): L.append(line(slug(n) + '_spell_hit', [(m, o)]))
    L.append(MARK1)
    f.write_text(txt + '\n'.join(L) + '\n')
    print(len(OWN), 'own effects,', len(SPELLS), 'class spells')


if __name__ == '__main__':
    write()
