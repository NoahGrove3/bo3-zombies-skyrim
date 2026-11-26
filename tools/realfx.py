"""Every ability's own effect, rebuilt from real parts (Noah, 2026-11-25: "All of the abilities need a complete remake they are
very low quality and 2D instead of hyper realistic and 3D. They need to be as highly polished and visually satisfying as the mods
we used for them before" - "Rebuild from their real parts": the real particle effects of Skyrim (and Dawnguard, Dragonborn) and of
the installed magic mods - Apocalypse, Mysticism - fire, smoke, sparks, shockwaves, combined into one new effect per ability, in
its own colours, each part set off on its own beat).

Each ability: its owner (src/Gun.cpp FxScope), its theme (which element's real parts it is built from), its style (how they are
put together: burst, shockwave, nova, pillar, toll...), colour and size - Noah's picks in the workshop (docs/workshop/
ability_picks.json). Writes the block between the markers at the end of assets/fx/SKSE/Plugins/CurseOfAlduin/fxmap.ini and
docs/workshop/abilities.json (what each card is made of, for the workshop).

The class spells keep their own (Noah: "we can use the spells from the actual mods and base Skyrim for those"): no entries.

    python3 tools/realfx.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
FXMAP = ROOT / 'assets' / 'fx' / 'SKSE' / 'Plugins' / 'CurseOfAlduin' / 'fxmap.ini'
PICKS = ROOT / 'docs' / 'workshop' / 'ability_picks.json'

# ---------------------------------------------------------------- the real parts
# P(model, label, s, play, natural): its model, what it is (the workshop's card names it), its own size, whether its animation
# must be set going, and whether it keeps its own colours (dust, stone) rather than taking the ability's
P = lambda m, label, s=1.0, play=False, nat=False: dict(model=m, label=label, s=s, play=play, nat=nat)
RING = P('effects\\mg07labyrinthianshockwave.nif', "Skyrim: Labyrinthian shockwave", 0.4)

PARTS = {
 'fire': {
  'core':   P('effects\\impacteffects\\fxdragonfireimpactactor.nif', 'Skyrim: dragonfire impact', 0.9),
  'big':    P('apocnew\\destructionfire_explosion_ballofire.nif', 'Apocalypse: Ball of Fire explosion', 0.8, True),
  'spark':  P('effects\\impacteffects\\fireballimpact01.nif', 'Skyrim: fireball impact', 0.6),
  'mist':   P('apocnew\\destructionfire_explosion_carbonize.nif', 'Apocalypse: Carbonize ash burst', 0.8, True),
  'pillar': P('effects\\mgmagicfirepillarsmall.nif', 'Skyrim: College fire pillar (small)', 0.55, True),
  'spikes': P('apocnew\\destructionfire_explosion_pyroclasm.nif', 'Apocalypse: Pyroclasm eruption', 0.6, True),
  'nova':   P('apocnew\\destructionhellfire_explosion.nif', 'Apocalypse: Hellfire explosion', 0.8, True),
  'sky':    P('effects\\impacteffects\\fxdragonfireimpact.nif', 'Skyrim: dragonfire blast', 0.5),
  'drain':  P('magic\\absorbredimpact01.nif', 'Skyrim: Absorb impact', 1.0),
  'rune':   P('apocnew\\alterationchaos_explosion_rune.nif', 'Apocalypse: Chaos rune burst', 0.8, True),
  'wave':   P('magic\\shoutareaexplosionwarm.nif', 'Skyrim: warm shout wave', 0.6),
  'native': ('orange', 'fire', 'flame', 'ss_none'),
 },
 'frost': {
  'core':   P('magic\\icicleimpactactor01.nif', 'Skyrim: Ice Spike impact', 1.0),
  'big':    P('magic\\explosionfrost01.nif', 'Skyrim: frost explosion', 1.0),
  'spark':  P('effects\\impacteffects\\impactfrost01.nif', 'Skyrim: frost impact', 1.0),
  'mist':   P('effects\\fxicewraithdeathexplosion.nif', 'Skyrim: ice wraith shatter', 0.7),
  'pillar': P('apocnew\\destructionfrost_explosion_shivers.nif', 'Apocalypse: Shivers explosion', 0.8, True),
  'spikes': P('magic\\magicespikevolleyexplosion.nif', 'Skyrim: Ice Spike volley shards', 0.6),
  'nova':   P('apocnew\\destructionfrost_explosion_frostnova.nif', 'Apocalypse: Frost Nova', 0.8, True),
  'sky':    P('effects\\impacteffects\\dragonfrostshoutimpact.nif', 'Skyrim: Frost Breath impact', 0.8),
  'drain':  P('apocnew\\destructionfrost_impact_howlingblast.nif', 'Apocalypse: Howling Blast impact', 1.0, True),
  'rune':   P('magic\\impactfrosticestorm.nif', 'Skyrim: Ice Storm impact', 0.7),
  'wave':   P('magic\\shoutareaexplosion01.nif', 'Skyrim: shout wave', 0.6),
  'native': ('iceblue', 'frost'),
 },
 'shock': {
  'core':   P('magic\\impactshock02explo.nif', 'Skyrim: Thunderbolt impact', 1.0),
  'big':    P('magic\\explosionshockmass01.nif', 'Skyrim: Chain Lightning burst', 0.9),
  'spark':  P('magic\\impactshock03.nif', 'Skyrim: spark impact', 1.0),
  'mist':   P('apocnew\\destructionshock_explosion_discharge.nif', 'Apocalypse: Discharge', 1.0, True),
  'pillar': P('apocnew\\destructionshock_explosion_fingerofthemountain.nif', 'Apocalypse: Finger of the Mountain', 0.7, True),
  'spikes': P('magic\\explosionatronachstormattack.nif', 'Skyrim: storm atronach blast', 0.8),
  'nova':   P('apocnew\\destructionshock_explosion_thundernova.nif', 'Apocalypse: Thunder Nova', 0.7, True),
  'sky':    P('apocnew\\destructionshock_impact_fingerofthemountain.nif', 'Apocalypse: Finger of the Mountain strike', 1.0, True),
  'drain':  P('magic\\impactshock02.nif', 'Skyrim: Lightning Bolt impact', 1.0),
  'rune':   P('magus\\mysticism\\explosionshockmass01.nif', 'Mysticism: mass shock burst', 0.8, True),
  'wave':   P('magic\\shoutareaexplosion01.nif', 'Skyrim: shout wave', 0.6),
  'native': ('blue', 'shock', 'shockv'),
 },
 'sun': {
  'core':   P('dlc01\\magic\\dlc1impactsunfirespell.nif', 'Dawnguard: Sun Fire impact', 1.0),
  'big':    P('magus\\mysticism\\sunbeamexplosion01.nif', 'Mysticism: sunbeam explosion', 1.0, True),
  'spark':  P('magic\\impactlightspell.nif', 'Skyrim: Magelight impact', 1.0),
  'mist':   P('magus\\mysticism\\sunlightexplosion01.nif', 'Mysticism: sunlight burst', 0.9, True),
  'pillar': P('dlc01\\magic\\dlc1aurielsunbeam01.nif', "Dawnguard: Auriel's sunbeam", 0.6, True),
  'spikes': P('magic\\turnundeadmassexplosion.nif', 'Skyrim: Repel Undead blast', 0.8),
  'nova':   P('magus\\mysticism\\explosionturnundead01.nif', 'Mysticism: turn undead nova', 0.9, True),
  'sky':    P('dlc01\\effects\\dlc1aurielsskyboltexplosion01.nif', "Dawnguard: Auriel's Bow sky bolt", 0.4),
  'drain':  P('magus\\mysticism\\impactsunbeamspell.nif', 'Mysticism: sunbeam impact', 1.0, True),
  'rune':   P('magic\\turnunimpact01.nif', 'Skyrim: Turn Undead impact', 1.0),
  'wave':   P('dlc01\\magic\\dlc1sunburstfx01.nif', 'Dawnguard: sunburst', 0.5),
  'native': ('gold', 'sun'),
 },
 'poison': {
  'core':   P('magus\\mysticism\\poisonexplosion01.nif', 'Mysticism: poison explosion', 1.0, True),
  'big':    P('magus\\mysticism\\poisonmassexplosion01.nif', 'Mysticism: mass poison explosion', 0.9, True),
  'spark':  P('effects\\impacteffects\\magpoisonsprayimpact.nif', 'Skyrim: poison spray impact', 1.0),
  'mist':   P('dlc02\\effects\\poisongasexplosion.nif', 'Dragonborn: poison gas cloud', 0.6),
  'pillar': P('magus\\mysticism\\poisonexplosion03.nif', 'Mysticism: poison eruption', 1.0, True),
  'spikes': P('magus\\mysticism\\poisonexplosion02.nif', 'Mysticism: poison splash', 1.0, True),
  'nova':   P('magus\\mysticism\\rustmassexplosion01.nif', 'Mysticism: corrosion wave', 0.9, True),
  'sky':    P('effects\\fxgastrapblast.nif', 'Skyrim: gas trap blast', 0.8),
  'drain':  P('magic\\absorbgreenimpact01.nif', 'Skyrim: green Absorb impact', 1.0),
  'rune':   P('magus\\mysticism\\poisonimpactfx01.nif', 'Mysticism: poison impact', 1.0, True),
  'wave':   P('magic\\shoutareaexplosionsmall01.nif', 'Skyrim: small shout wave', 0.6),
  'native': ('poison',),
 },
 'shadow': {
  'core':   P('magic\\explosionilusiondark01.nif', 'Skyrim: dark Illusion burst', 1.0),
  'big':    P('apocnew\\illusionnightmare_explosion.nif', 'Apocalypse: Nightmare explosion', 0.9, True),
  'spark':  P('magic\\illusionimpact01.nif', 'Skyrim: Illusion impact', 1.0),
  'mist':   P('apocnew\\illusionshadow_explosion.nif', 'Apocalypse: Shadow explosion', 0.9, True),
  'pillar': P('dlc02\\effects\\miraakteleportoutexplosion.nif', "Dragonborn: Miraak's vanishing", 0.8),
  'spikes': P('apocnew\\illusionnightmare_explosion_illusorypyre.nif', 'Apocalypse: Illusory Pyre', 0.8, True),
  'nova':   P('apocnew\\illusionblue_explosion_manavoid.nif', 'Apocalypse: Mana Void', 0.8, True),
  'sky':    P('dlc01\\magic\\dlc01_soulrendimpact01.nif', 'Dawnguard: Soul Rend impact', 1.0),
  'drain':  P('magic\\absorbblueimpact01.nif', 'Skyrim: Absorb Magicka impact', 1.0),
  'rune':   P('apocnew\\alterationchaos_explosion_rune.nif', 'Apocalypse: Chaos rune burst', 0.8, True),
  'wave':   P('apocnew\\illusionempathy_explosion_screamofpain.nif', 'Apocalypse: Scream of Pain', 0.8, True),
  'native': ('purple', 'shadow', 'violet'),
 },
 'arcane': {
  'core':   P('apocnew\\illusionblue_explosion.nif', 'Apocalypse: arcane orb burst', 0.8, True),
  'big':    P('apocnew\\restorationarcane_explosion_lunartide.nif', 'Apocalypse: Lunar Tide', 0.8, True),
  'spark':  P('magic\\absorbblueimpact01.nif', 'Skyrim: Absorb Magicka impact', 1.0),
  'mist':   P('apocnew\\alterationmyst_explosion.nif', 'Apocalypse: Mysticism burst', 0.9, True),
  'pillar': P('dlc02\\effects\\dlc2constellationexplosion.nif', 'Dragonborn: constellation burst', 0.7),
  'spikes': P('magic\\alterposimpact01.nif', 'Skyrim: Alteration impact', 1.2),
  'nova':   P('apocnew\\illusionblue_explosion_manavoid.nif', 'Apocalypse: Mana Void', 0.8, True),
  'sky':    P('magic\\impactmg07staffofmagnus.nif', 'Skyrim: Staff of Magnus impact', 1.0),
  'drain':  P('apocnew\\restorationarcane_impact.nif', 'Apocalypse: arcane impact', 1.0, True),
  'rune':   P('apocnew\\alterationmyst_explosion.nif', 'Apocalypse: Mysticism burst', 0.9, True),
  'wave':   P('apocnew\\alterationalt_explosion.nif', 'Apocalypse: Alteration wave', 0.8, True),
  'native': ('arcane', 'drain', 'carian'),
 },
 'blood': {
  'core':   P('apocnew\\restorationblood_explosion.nif', 'Apocalypse: blood explosion', 0.8, True),
  'big':    P('dlc01\\effects\\dlc1bloodexplosion500.nif', 'Dawnguard: blood explosion', 0.5),
  'spark':  P('effects\\impacteffects\\bloodspraylargemist01.nif', 'Skyrim: blood mist spray', 0.9, False, True),
  'mist':   P('dlc01\\magic\\dlc1vampireabsorbexplosion.nif', 'Dawnguard: Vampiric Drain burst', 0.5),
  'pillar': P('dlc01\\magic\\dlc1vampchangeexplosion.nif', 'Dawnguard: Vampire Lord change', 0.6),
  'spikes': P('apocnew\\restorationblood_explosion_rune.nif', 'Apocalypse: blood rune burst', 0.8, True),
  'nova':   P('sacrosanct\\blood_explosion_shockwave.nif', 'Sacrosanct: blood shockwave', 0.8, True),
  'sky':    P('effects\\impacteffects\\bloodsprayalduinimpact.nif', "Skyrim: Alduin's blood spray", 0.8, False, True),
  'drain':  P('dlc01\\magic\\dlc1vampiredrainimpact.nif', 'Dawnguard: Vampiric Drain impact', 1.0),
  'rune':   P('apocnew\\restorationblood_explosion_rune.nif', 'Apocalypse: blood rune burst', 0.8, True),
  'wave':   P('sacrosanct\\blood_explosion_shockwave.nif', 'Sacrosanct: blood shockwave', 0.8, True),
  'native': ('red', 'blood'),
 },
 'spirit': {
  'core':   P('effects\\impacteffects\\fxghostimpact.nif', 'Skyrim: ghost impact', 1.2),
  'big':    P('apocnew\\restorationnecro_explosion_lifesfinale.nif', "Apocalypse: Life's Finale", 0.8, True),
  'spark':  P('magic\\paralyzeimpact01.nif', 'Skyrim: Paralyze impact', 1.0),
  'mist':   P('effects\\fxwaterfallmistblastlite.nif', 'Skyrim: waterfall mist blast', 0.7),
  'pillar': P('apocnew\\conjurationsummon_explosion.nif', 'Apocalypse: summoning burst', 0.9, True),
  'spikes': P('magic\\reanimateimpact01.nif', 'Skyrim: Reanimate impact', 1.2),
  'nova':   P('apocnew\\restorationnecro_explosion.nif', 'Apocalypse: necromantic burst', 0.9, True),
  'sky':    P('magic\\explosionillusionlight01.nif', 'Skyrim: Illusion light burst', 1.0),
  'drain':  P('magic\\absorbgreenimpact01.nif', 'Skyrim: green Absorb impact', 1.0),
  'rune':   P('apocnew\\conjurationsummon_explosion.nif', 'Apocalypse: summoning burst', 0.9, True),
  'wave':   P('magic\\shoutareaexplosionsmall01.nif', 'Skyrim: small shout wave', 0.6),
  'native': ('white',),
 },
 'earth': {
  'core':   P('effects\\fxdustburstmed.nif', 'Skyrim: dust burst', 1.0, False, True),
  'big':    P('apocnew\\alterationearth_explosion_massive.nif', 'Apocalypse: massive earth explosion', 0.7, True, True),
  'spark':  P('effects\\impacteffects\\fxmetalsparkimpactblunt.nif', 'Skyrim: blunt spark impact', 1.2, False, True),
  'mist':   P('effects\\fxdustburstsm.nif', 'Skyrim: dust puff', 1.2, False, True),
  'pillar': P('apocnew\\alterationearth_explosion_grindstone.nif', 'Apocalypse: Grindstone', 0.8, True, True),
  'spikes': P('apocnew\\alterationearth_explosion_talonsofnirn.nif', 'Apocalypse: Talons of Nirn (earth spikes)', 0.8, True, True),
  'nova':   P('effects\\impacteffects\\fxdragontailstompdust.nif', "Skyrim: dragon's tail stomp", 0.6, False, True),
  'sky':    P('effects\\impacteffects\\fxdragonlandingimpact.nif', "Skyrim: dragon's landing", 0.5, False, True),
  'drain':  P('magic\\absorbgreenimpact01.nif', 'Skyrim: green Absorb impact', 0.8),
  'rune':   P('apocnew\\alterationearth_impact.nif', 'Apocalypse: earth impact', 1.0, True, True),
  'wave':   P('effects\\impacteffects\\fxdragontailstompdust.nif', "Skyrim: dragon's tail stomp", 0.5, False, True),
  'native': (),
 },
 'wind': {
  'core':   P('magic\\shoutareaexplosionsmall01.nif', 'Skyrim: Unrelenting Force (small)', 0.7),
  'big':    P('magic\\shoutareaexplosion01.nif', 'Skyrim: Unrelenting Force wave', 0.7),
  'spark':  P('effects\\fxwaterfallmistblastlite.nif', 'Skyrim: waterfall mist blast', 0.5),
  'mist':   P('effects\\fxwaterfallmistblast.nif', 'Skyrim: waterfall mist', 0.5),
  'pillar': P('apocnew\\alterationalt_explosion_battletide.nif', 'Apocalypse: Battletide', 0.8, True),
  'spikes': P('magic\\shoutareaexplosion01.nif', 'Skyrim: Unrelenting Force wave', 0.9),
  'nova':   P('apocnew\\alterationalt_explosion_battletide.nif', 'Apocalypse: Battletide', 0.9, True),
  'sky':    P('magic\\explosionatronachstormattack.nif', 'Skyrim: storm atronach blast', 0.7),
  'drain':  P('magic\\throwvoiceimpact.nif', 'Skyrim: Throw Voice impact', 1.0),
  'rune':   P('apocnew\\alterationalt_explosion.nif', 'Apocalypse: Alteration wave', 0.8, True),
  'wave':   P('magic\\shoutareaexplosion01.nif', 'Skyrim: Unrelenting Force wave', 0.6),
  'native': ('white',),
 },
}

# ---------------------------------------------------------------- how a style puts the parts together
# (role, delay s, its size against the card's, on the ground)
S = lambda role, delay=0.0, k=1.0, ground=False: (role, delay, k, ground)
STYLES = {
 'burst':     [S('core'), S('spark', 0.05, 0.9), S('mist', 0.15, 0.8)],
 'shockwave': [S('ring', 0, 1.0, True), S('core'), S('wave', 0.08, 1.0, True), S('mist', 0.2, 0.9, True)],
 'nova':      [S('nova', 0, 1.0, True), S('spikes', 0.06, 1.0, True), S('ring', 0.02, 1.0, True), S('mist', 0.25, 0.9)],
 'pillar':    [S('pillar', 0, 1.0, True), S('ring', 0.05, 0.9, True), S('core', 0.12), S('mist', 0.3, 0.8, True)],
 'strike':    [S('sky', 0, 1.0, True), S('big', 0.06, 1.0, True), S('ring', 0.1, 1.0, True), S('spark', 0.15)],
 'chain':     [S('spark'), S('core', 0.04, 0.9), S('drain', 0.1, 0.8)],
 'bolt':      [S('core'), S('spark', 0.04, 0.8)],
 'meteor':    [S('big', 0, 1.0, True), S('core', 0.04), S('ring', 0.05, 1.1, True), S('mist', 0.3, 1.0, True)],
 'cyclone':   [S('big', 0, 1.0, True), S('wave', 0.1, 1.0, True), S('mist', 0.2, 1.0)],
 'cloud':     [S('mist'), S('core', 0.05, 0.8), S('mist', 0.5, 1.2)],
 'drain':     [S('drain'), S('core', 0.12, 0.8), S('spark', 0.3, 0.7)],
 'bind':      [S('rune', 0, 1.0, True), S('spark', 0.05), S('drain', 0.2, 0.8)],
 'freeze':    [S('core'), S('spikes', 0.05, 0.9, True), S('mist', 0.2, 0.8)],
 'toll':      [S('ring', 0, 1.0, True), S('core'), S('ring', 0.3, 1.3, True), S('ring', 0.6, 1.6, True), S('wave', 0.6, 0.9, True)],
 'wave':      [S('wave', 0, 1.0, True), S('core'), S('ring', 0.05, 1.0, True), S('mist', 0.2, 0.9, True)],
 'summon':    [S('rune', 0, 1.0, True), S('pillar', 0.1, 1.0, True), S('mist', 0.3, 0.9)],
 'mark':      [S('rune'), S('spark', 0.1, 0.8)],
 'slash':     [S('spark'), S('core', 0.03, 0.9)],
 'pool':      [S('rune', 0, 1.0, True), S('mist', 0.1, 0.9, True)],
}
BIG_AT = 125   # (a card this size or more: the theme's big explosion laid in too)

# ---------------------------------------------------------------- each ability: owner, card, theme (its colour and size the picks')
A = lambda owner, card, theme, col, style='burst', size=100: dict(owner=owner, card=card, theme=theme, col=col, style=style, size=size)
ABIL = [
    A('priest_vahlok_hit', 'vahlok_hit', 'spirit', 'green'), A('priest_vahlok_heavy', 'vahlok_heavy', 'spirit', 'green'),
    A('priest_nahkriin_hit', 'nahkriin_hit', 'spirit', 'white'), A('priest_nahkriin_heavy', 'nahkriin_heavy', 'spirit', 'white'),
    A('priest_volsung_hit', 'volsung_light', 'wind', 'white'), A('priest_volsung_heavy', 'volsung_heavy', 'wind', 'blue'),
    A('priest_zahkriisos_hit', 'zahkriisos_hit', 'frost', 'iceblue'), A('priest_zahkriisos_heavy', 'zahkriisos_heavy', 'frost', 'iceblue'),
    A('priest_otar_hit', 'otar_hit', 'shock', 'blue'), A('priest_otar_heavy', 'otar_heavy', 'shock', 'blue'),
    A('priest_rahgot_hit', 'rahgot_hit', 'poison', 'poison'), A('priest_rahgot_heavy', 'rahgot_heavy', 'poison', 'poison'),
    A('priest_krosis_hit', 'krosis_hit', 'blood', 'red'), A('priest_krosis_heavy', 'krosis_heavy', 'blood', 'red'),
    A('priest_hevnoraak_hit', 'hevnoraak_hit', 'earth', 'green'), A('priest_hevnoraak_heavy', 'hevnoraak_heavy', 'earth', 'green'),
    A('priest_vokun_hit', 'vokun_hit', 'shadow', 'purple'), A('priest_vokun_heavy', 'vokun_heavy', 'shadow', 'shadow'),
    A('priest_morokei_hit', 'morokei_hit', 'arcane', 'drain'), A('priest_morokei_heavy', 'morokei_heavy', 'arcane', 'drain'),
    A('tab_stormtalons_hit', 'stormtalons_third', 'shock', 'blue'), A('tab_stormtalons_heavy', 'stormtalons_heavy', 'shock', 'blue'),
    A('tab_gravebell_hit', 'gravebell_fifth', 'sun', 'gold'), A('tab_gravebell_heavy', 'gravebell_fifth', 'sun', 'gold'),
    A('tab_talons_hit', 'talons_hit', 'blood', 'red'), A('tab_talons_heavy', 'talons_hit', 'blood', 'red'),
    A('tab_riverstone_hit', 'riverstone_hit', 'earth', 'green'), A('tab_riverstone_heavy', 'riverstone_heavy', 'earth', 'green'),
    A('tab_wyrmspine_hit', 'wyrmspine_pike_hit', 'fire', 'fear'), A('tab_wyrmspine_heavy', 'wyrmspine_pike_heavy', 'fire', 'fear'),
    A('tab_lanternwick_hit', 'lanternwick_light', 'frost', 'iceblue'), A('tab_lanternwick_heavy', 'lanternwick_heavy', 'frost', 'iceblue'),
    A('tab_cinderclaws_hit', 'cinderclaws_hit', 'fire', 'orange'), A('tab_cinderclaws_heavy', 'cinderclaws_heavy', 'fire', 'orange'),
    A('tab_eclipse_hit', 'eclipse_hit', 'sun', 'orange'), A('tab_eclipse_heavy', 'eclipse_heavy', 'frost', 'iceblue'),
    A('tab_wanderstar_hit', 'wanderstar_fifth', 'sun', 'gold'), A('tab_wanderstar_heavy', 'wanderstar_fifth', 'sun', 'gold'),
    A('tab_frostmourn_hit', 'frostmourn_hit', 'frost', 'iceblue'), A('tab_frostmourn_heavy', 'frostmourn_heavy', 'frost', 'iceblue'),
    A('tab_silverscythe_hit', 'silverscythe_fourth', 'sun', 'moon'), A('tab_silverscythe_heavy', 'silverscythe_fourth', 'sun', 'moon'),
    A('tab_bonereaper_hit', 'bonereaper_kill', 'spirit', 'white'), A('tab_bonereaper_heavy', 'bonereaper_kill', 'spirit', 'white'),
    A('tab_wyrmtooth_hit', 'wyrmtooth_hit', 'blood', 'red'), A('tab_wyrmtooth_heavy', 'wyrmtooth_heavy', 'earth', 'red'),
    A('tab_bloodmoon_hit', 'bloodmoon_hit', 'blood', 'red'), A('tab_bloodmoon_heavy', 'bloodmoon_heavy', 'blood', 'red'),
    A('bane_light', 'bane_light', 'fire', 'orange'), A('bane_heavy', 'bane_heavy', 'wind', 'white'),
    # the class weapons' bolts and heavies (by their own model's name; each element its own colour whatever the card's)
    A('arc_purple', 'scythe_light', 'fire', 'shadow'), A('firecyclone', 'scythe_heavy', 'fire', 'shadow'),
    A('arc_white', 'staff_light', 'sun', 'white'), A('arc_carian', 'wand_hit', 'arcane', 'carian'),
    A('arc_fire', 'rapier_light', 'fire', 'orange'), A('arc_frost', 'rapier_light', 'frost', 'iceblue'),
    A('arc_shock', 'rapier_light', 'shock', 'blue'), A('arc_sun', 'rapier_light', 'sun', 'gold'),
    A('arc_blood', 'rapier_light', 'arcane', 'arcane'), A('arc_arcane', 'rapier_light', 'arcane', 'arcane'),
    A('arc_gold', 'slayer_light', 'fire', 'gold'), A('arc_bane', 'bane_light', 'wind', 'white'),
    A('cyclone_fire', 'rapier_heavy', 'fire', 'orange'), A('cyclone_frost', 'rapier_heavy', 'frost', 'iceblue'),
    A('cyclone_shock', 'rapier_heavy', 'shock', 'blue'), A('cyclone_white', 'rapier_heavy', 'sun', 'gold'),
    A('cyclone_blood', 'rapier_heavy', 'arcane', 'arcane'), A('cyclone_storm', 'rapier_heavy', 'shock', 'blue'),
    A('cyclone_violet', 'scythe_heavy', 'fire', 'shadow'),
    A('fly_fire', 'javelin_land', 'fire', 'orange'), A('fly_frost', 'javelin_land', 'frost', 'iceblue'),
    A('fly_shock', 'javelin_land', 'shock', 'blue'), A('fly_wind', 'javelin_land', 'wind', 'white'),
    A('fly_sun', 'javelin_land', 'sun', 'gold'), A('fly_blood', 'javelin_land', 'arcane', 'arcane'),
    A('fly_void', 'javelin_land', 'shadow', 'shadow'), A('fly_poison', 'javelin_land', 'poison', 'poison'),
    A('flask_fire', 'ffire_land', 'fire', 'orange'), A('flask_frost', 'ffrost_land', 'frost', 'iceblue'),
    A('flask_shock', 'fshock_land', 'shock', 'blue'), A('flask_poison', 'facid_land', 'poison', 'poison'),
    A('flask_sun', 'fholy_land', 'sun', 'gold'), A('flask_blood', 'fvoid_land', 'arcane', 'arcane'),
    A('flask_philosopher', 'mutagen_elixir', 'sun', 'gold'),
]
# (where one card speaks for several - the rapier's elements, the javelin's, the flasks - each keeps its own element's colour)
OWN_COLOUR = ('arc_', 'cyclone_', 'fly_', 'flask_')
KEEP_PICKED = ('arc_purple', 'arc_white', 'arc_gold', 'arc_bane')
# (Noah's notes on the 8cd53a1 workshop, after his picks were read out)
NOTES = {
    'vahlok_heavy': "Chains actually grabbing nearby enemies and grouping them closer together",
}
RAMP_OF = {'flame': 'fire', 'ss_none': 'fire'}   # (workshop colours that wear another ramp in the game)


def all_abilities():
    picks = json.loads(PICKS.read_text()) if PICKS.exists() else {}
    out = []
    for ab in ABIL:
        ab = dict(ab)
        pk = picks.get(ab['card'])
        if pk:
            ab['style'], ab['size'] = pk['style'], pk['size']
            if not ab['owner'].startswith(OWN_COLOUR) or ab['owner'] in KEEP_PICKED: ab['col'] = pk['col']
        if ab['style'] not in STYLES: ab['style'] = 'burst'
        out.append(ab)
    return out


def compose(ab):
    """the ability's real parts: its style's roles filled from its theme, in its colour, at its size, each on its own beat"""
    th = PARTS[ab['theme']]
    k = ab['size'] / 100.0
    native = ab['col'] in th['native']
    ramp = RAMP_OF.get(ab['col'], ab['col'])
    roles = list(STYLES[ab['style']])
    if ab['size'] >= BIG_AT and not any(r[0] in ('big', 'nova', 'sky') for r in roles): roles.append(S('big', 0.1, 1.0, roles[0][3]))
    parts = []
    for role, delay, rk, ground in roles:
        p = RING if role == 'ring' else th[role]
        if role != 'ring' and any(q['model'] == p['model'] for q in parts): continue   # (one theme's same part twice: once)
        opts = []
        if not p['nat'] and not native: opts.append(f'pal={ramp}')
        elif role == 'ring': opts.append(f'pal={ramp}')
        opts.append(f's={p["s"] * k * rk:.2f}')
        if delay: opts.append(f'delay={delay:.2f}')
        if ground: opts.append('ground')
        if p['play']: opts.append('play')
        parts.append(dict(role=role, model=p['model'], label=p['label'], opts=' '.join(opts), delay=delay, ground=ground, scale=round(p['s'] * k * rk, 2)))
    return parts


MARK0 = '# ==== own effects (tools/realfx.py) ===='
MARK1 = '# ==== end own effects ===='


def build():
    abil = all_abilities()
    lines, doc = [], {'about': __doc__.split('\n\n')[0], 'abilities': []}
    for ab in abil:
        parts = compose(ab)
        lines.append(f'CurseOfAlduin\\fx\\w\\{ab["owner"]}_burst.nif = ' + ' ; '.join(f'{p["model"]} {p["opts"]}' for p in parts))
        doc['abilities'].append(dict(ab, parts=parts, note=NOTES.get(ab['card'], '')))
    txt = FXMAP.read_text()
    for m0 in (MARK0, '# ==== own effects (tools/abilityfx.py) ====', '# ==== own effects (tools/fxown.py) ===='):
        if m0 in txt: txt = txt[:txt.index(m0)].rstrip('\n') + '\n'
    block = ['', MARK0,
             '# (Noah, 2026-11-25: "Rebuild from their real parts") each ability its own effect: the real particle effects of Skyrim,',
             '# Dawnguard, Dragonborn, Apocalypse and Mysticism put together, in its own colour and size, each part on its own beat',
             '# (delay=), the ground ones laid on the ground. While its blow, bolt, flask or cyclone is shown (src/Gun.cpp FxScope) this',
             '# stands in for the shared burst, and no shared ring, blast, ground or motes are laid beside it.',
             '# The class spells keep their own (Noah: "we can use the spells from the actual mods and base Skyrim for those")',
             'CurseOfAlduin\\fx\\spell_hit.nif = none'] + lines + [MARK1]
    FXMAP.write_text(txt + '\n'.join(block) + '\n')
    (ROOT / 'docs' / 'workshop' / 'abilities.json').write_text(json.dumps(doc, indent=1))
    models = sorted({p['model'] for ab in doc['abilities'] for p in ab['parts']})
    (ROOT / 'build').mkdir(exist_ok=True)
    (ROOT / 'build' / 'realfx_models.txt').write_text('\n'.join(models) + '\n')
    print(len(abil), 'abilities,', len(models), 'real parts')


if __name__ == '__main__' and __import__('sys').argv[1:] != ['final']:
    build()


# ---------------------------------------------------------------- (Noah, 2026-11-26) the workshop's final picks, into the game
FINAL = ROOT / 'docs' / 'workshop' / 'final_picks.json'
# his notes on the picks (the workshop doc, Nov 25 night)
FINAL_NOTES = {
    'volsung_heavy': 'remove the smoke trail effect that lingers after the explosion',   # (its mist part left out)
    'bonereaper_kill': "use the skyrim reanimation or summon portal for this",            # (Skyrim's conjuring portal)
}


def _plays():
    """which models need their own animation set going (the effects map's play flag): from the parts and the map as it is"""
    plays = {p['model'].lower(): p['play'] for th in PARTS.values() for r, p in th.items() if r != 'native'}
    for line in FXMAP.read_text().splitlines():
        if '=' not in line or line.lstrip().startswith('#'): continue
        for seg in line.split('=', 1)[1].split(';'):
            toks = seg.split()
            if toks and toks[0].lower() not in plays: plays[toks[0].lower()] = 'play' in toks
            elif toks and 'play' in toks: plays[toks[0].lower()] = True
    return plays


def final_parts(ab, pk, opts, plays):
    """one owner's parts as Noah set them: its option's parts (or the one it plays now), at his size, colour, area, speed,
    density, brightness and light - what the workshop's preview drew (src/Gun.cpp reads bright= and light=)"""
    card = ab['card']
    info = opts.get(card)
    o = next((x for x in info['opts'] if x['id'] == pk.get('opt', 'a1')), info['opts'][0]) if info else None
    parts = [dict(p) for p in (o['parts'] if o else [])]
    if card == 'volsung_heavy': parts = [p for p in parts if 'mist' not in p['model']] or parts[:1]
    # (Noah, 2026-11-26: "the Mistblade looks good, but the heavy attack should be an area of effect, like a mist, not a rectangle of
    # the mist effect") a cloud that rolls out over the ground: the gas trap's spreading bank and the ash spawn's billow, in pale
    # grey-white, the ghost's flash at its heart
    if card == 'nahkriin_heavy': parts = [dict(model='effects\\fxgastrapblast.nif', s=1.5, delay=0, ground=True, pal='white', fixed=True),
                                          dict(model='dlc02\\effects\\poisongasexplosion.nif', s=1.1, delay=0.06, ground=True, pal='white', fixed=True),
                                          dict(model='effects\\fxgastrapblast.nif', s=2.1, delay=0.35, ground=True, pal='white', fixed=True),
                                          dict(model='effects\\impacteffects\\fxghostimpact.nif', s=0.67, delay=0.08, ground=False)]
    if card == 'bonereaper_kill': parts = [dict(model='magic\\summontargetfx.nif', s=1.2, delay=0, ground=False, nat=True, label="Skyrim: the conjuring portal")]
    k, area = pk.get('size', 100) / 100.0, pk.get('area', 100) / 100.0
    speed = max(0.2, pk.get('speed', 100) / 100.0)
    dens = pk.get('density', 100) / 100.0
    if dens < 0.999 and len(parts) > 1: parts = parts[:max(1, round(len(parts) * dens))]
    col = pk.get('colour') or ab['col']
    if ab['owner'].startswith(OWN_COLOUR) and ab['owner'] not in KEEP_PICKED: col = ab['col']   # (the rapier's, javelin's, flasks' elements keep theirs)
    th = PARTS[ab['theme']]
    native = col in th['native']
    ramp = RAMP_OF.get(col, col)
    out = []
    for p in parts:
        m = p['model'].lower()
        opt = []
        if p.get('pal'): opt.append(f'pal={p["pal"]}')
        elif not ((p.get('nat') or native) and 'labyrinthianshockwave' not in m): opt.append(f'pal={ramp}')
        s = p['s'] * (1.0 if p.get('fixed') else k * (area if p.get('ground') else 1.0))
        opt.append(f's={s:.2f}')
        if p.get('delay'): opt.append(f'delay={p["delay"] / speed:.2f}')
        if p.get('ground'): opt.append('ground')
        if plays.get(m) or m.startswith(('apocnew\\', 'magus\\')) and plays.get(m) is not False: opt.append('play')
        if abs(pk.get('bright', 100) - 100) > 0.5: opt.append(f'bright={pk["bright"] / 100:.2f}')
        if abs(pk.get('light', 100) - 100) > 0.5: opt.append(f'light={pk["light"] / 100:.2f}')
        # (Noah, 2026-11-26: "the beam is not coming from the sky. It's rotating around on the ground sideways") Auriel's sunbeam runs
        # down its own +Y (the bow's shot): stood up on end, a shaft of light from the sky
        if 'aurielsunbeam' in m: opt.append('r=-90,0,0')
        out.append(f'{m} ' + ' '.join(opt))
    return out


def build_final():
    """the effects map's own block from Noah's final picks (docs/workshop/final_picks.json) - the cards he kept as they are, or
    never set, stay as they were built; and each owner's reach (area=) for the code"""
    import abilityopts
    picks = json.loads(FINAL.read_text())['abilities']
    abil = all_abilities()          # (the cards as built before: the "as it is now" of every option list)
    opts = abilityopts.options()
    plays = _plays()
    lines, n = [], 0
    for ab in abil:
        pk = picks.get(ab['card'])
        # (Noah, 2026-11-26, the doc's decision "Where the javelin lands": "Each element its own") his Flame Burst is the fire
        # javelin's landing; every other element lands in its own colour, as built
        if ab['owner'].startswith('fly_') and ab['owner'] not in ('fly_fire',): pk = None
        if pk and pk.get('choice') == 'change':
            parts = final_parts(ab, pk, opts, plays); n += 1
            area = pk.get('area', 100)
            line = f'CurseOfAlduin\\fx\\w\\{ab["owner"]}_burst.nif = ' + ' ; '.join(parts)
            lines.append(line)
            if area != 100: lines.append(f'CurseOfAlduin\\fx\\w\\{ab["owner"]}_area.nif = none area={area / 100:.2f}')
        else:
            parts = compose(ab)
            lines.append(f'CurseOfAlduin\\fx\\w\\{ab["owner"]}_burst.nif = ' + ' ; '.join(f'{p["model"]} {p["opts"]}' + (' r=-90,0,0' if 'aurielsunbeam' in p['model'].lower() else '') for p in parts))
    txt = FXMAP.read_text()
    txt = txt[:txt.index(MARK0)].rstrip('\n') + '\n'
    block = ['', MARK0,
             "# (Noah, 2026-11-26: his workshop picks, final) each ability its own effect, as he set it in the workshop doc: its option's real",
             '# parts, in his colour and size, his area on the ground parts, his speed on their beats, fewer parts at a lower density,',
             '# bright= its glow and light= the light its parts cast (src/Gun.cpp). area= on an owner\'s _area line: its reach in the game.',
             'CurseOfAlduin\\fx\\spell_hit.nif = none'] + lines + [MARK1]
    FXMAP.write_text(txt + '\n'.join(block) + '\n')
    print(len(abil), 'owners,', n, 'from his picks')


if __name__ == '__main__' and len(__import__('sys').argv) > 1 and __import__('sys').argv[1] == 'final':
    build_final()
