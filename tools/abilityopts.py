"""Each ability's own five options (Noah, 2026-11-25: "my options for the different styles should be different variations of what
the weapon is actually doing. So the Storm Talons, every third hit chains 2 or more of the dead, should look like Chain Lightning,
and the Heavy should look like an Electricity Stun in an area ... the recommended changes for the style should be unique to each
weapon ... make 5 options total").

What an ability does picks its kind (a chain, a stun round you, a freeze on the one struck, a fog, a pull...); its element picks
the real parts (tools/realfx.py PARTS, Skyrim's and the magic mods' own effects). Each kind has a pool of ways to show it; each
ability takes four from its kind's pool (a different four for two weapons of the same kind, where the pool allows) after the one
it plays now ("As it is now"), so each has five of its own. Every option is a set of real parts on their own beats - what the
game would play (tools/realfx.py writes the picked one into fxmap.ini) - and, for a chain or a pull, the beam between the dead.

    import abilityopts; abilityopts.options() -> {card: [{id, name, desc, parts: [...], link}]}
"""
import hashlib, json, pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import realfx as RF

CARDS = json.loads((ROOT / 'docs' / 'workshop' / 'ability_cards.json').read_text())
BEAM = 'magic\\staffofmagnusbolt01.nif'   # (a beam between the dead: the Staff of Magnus's, painted in the ability's colour)

# card: (its kind, its element's parts)
KIND = {
 'vahlok_hit': ('shackle', 'spirit'), 'vahlok_heavy': ('pull', 'spirit'), 'nahkriin_hit': ('blind', 'spirit'), 'nahkriin_heavy': ('fog', 'spirit'),
 'volsung_light': ('gust', 'wind'), 'volsung_heavy': ('cone', 'wind'), 'zahkriisos_hit': ('freeze', 'frost'), 'zahkriisos_heavy': ('shatter', 'frost'),
 'otar_hit': ('chain', 'shock'), 'otar_heavy': ('arcs', 'shock'), 'rahgot_hit': ('poison', 'poison'), 'rahgot_heavy': ('burst_all', 'poison'),
 'krosis_hit': ('leech', 'blood'), 'krosis_heavy': ('wail', 'blood'), 'hevnoraak_hit': ('armour', 'earth'), 'hevnoraak_heavy': ('stagger', 'earth'),
 'vokun_hit': ('sleep', 'shadow'), 'vokun_heavy': ('fog', 'shadow'), 'morokei_hit': ('magic', 'arcane'), 'morokei_heavy': ('mana', 'arcane'),
 'stormtalons_third': ('chain', 'shock'), 'stormtalons_heavy': ('stun', 'shock'), 'gravebell_fifth': ('toll', 'sun'), 'talons_hit': ('bleed', 'blood'),
 'riverstone_hit': ('knock', 'earth'), 'riverstone_heavy': ('split', 'earth'), 'wyrmspine_pike_hit': ('pierce', 'fire'), 'wyrmspine_pike_heavy': ('stagger', 'fire'),
 'lanternwick_light': ('freeze', 'frost'), 'lanternwick_heavy': ('blizzard', 'frost'), 'cinderclaws_hit': ('burn', 'fire'), 'cinderclaws_heavy': ('embers', 'fire'),
 'eclipse_hit': ('twin', 'sun'), 'eclipse_heavy': ('twin_ring', 'frost'), 'wanderstar_cast': ('star', 'sun'), 'wanderstar_fifth': ('fling', 'sun'),
 'frostmourn_hit': ('slow', 'frost'), 'frostmourn_heavy': ('freeze_ring', 'frost'), 'silverscythe_fourth': ('flash', 'spirit'), 'bonereaper_kill': ('raise', 'spirit'),
 'wyrmtooth_hit': ('bleed', 'blood'), 'wyrmtooth_heavy': ('stagger', 'earth'), 'bloodmoon_hit': ('leech', 'blood'), 'bloodmoon_heavy': ('drain_area', 'blood'),
 'javelin_throw': ('javelin', 'fire'), 'javelin_land': ('javelin', 'fire'), 'javelin_bounce': ('javelin', 'fire'),
 'soulcaller_beam': ('soul', 'arcane'), 'soulcaller_wolves': ('portal', 'arcane'), 'mutagen_flasks': ('flask', 'fire'), 'mutagen_elixir': ('elixir', 'sun'),
 'slayer_light': ('fireball', 'fire'), 'slayer_heavy': ('meteor', 'fire'), 'bane_light': ('meteor', 'fire'), 'bane_heavy': ('blast', 'wind'),
 'scythe_light': ('fireball', 'fire'), 'scythe_heavy': ('tornado', 'fire'), 'wand_light': ('bolts', 'arcane'), 'wand_hit': ('magic', 'arcane'),
 'wand_heavy': ('bolts', 'arcane'), 'staff_light': ('sunball', 'sun'), 'staff_heavy': ('sunbeam', 'sun'), 'rapier_light': ('fireball', 'fire'),
 'rapier_heavy': ('tornado', 'fire'), 'rapier_line': ('ice_line', 'frost'), 'ffire_land': ('pool', 'fire'), 'ffire_power': ('firestorm', 'fire'),
 'ffrost_land': ('pool', 'frost'), 'ffrost_power': ('freeze_ring', 'frost'), 'fshock_land': ('pool', 'shock'), 'fshock_power': ('strike', 'shock'),
 'facid_land': ('pool', 'poison'), 'fholy_land': ('pool', 'sun'), 'fvoid_land': ('pull_in', 'arcane'), 'fvoid_power': ('push', 'arcane'),
 'summon_arrive': ('portal', 'arcane'),
}
# how the preview moves it (the game's own): 'hit' where it strikes; 'chain' leaping on to two more of the dead
PATTERN = {'chain': 'chain', 'arcs': 'arcs', 'pull': 'pullbeam', 'pull_in': 'pullbeam', 'leech': 'leech', 'drain_area': 'leech', 'soul': 'show'}

# ---- the ways to show each kind: (name, what you see, parts, link). A part: (role, delay, size, on the ground) from the element,
# or ('elem:role', ...) from another element, or ('m:model', ...) one model by name; link: a beam between the dead, or None
P = lambda r, d=0.0, k=1.0, g=False: (r, d, k, g)
POOL = {
 'chain': [
    ('Forked Lightning', 'A bright fork leaping from one to the next, a crack of sparks on each.', [P('spark'), P('core', 0.04, 0.9)], BEAM),
    ('Thunder Jump', 'A white flash on the first, then the bolt jumping on, smaller each time.', [P('big', 0, 0.7), P('spark', 0.05, 0.8)], BEAM),
    ('Static Web', 'A crackling web of discharge hanging on each one it reaches.', [P('mist'), P('spark', 0.05, 0.7)], BEAM),
    ('Sky Fork', 'Each one struck from above in turn, as if the sky chose them.', [P('sky', 0, 0.8, True), P('core', 0.06, 0.8)], None),
    ('Arc Flash', 'A tight blue arc and a flash of light on each, quick and clean.', [P('drain'), P('m:magic\\impactlightspell.nif', 0.03, 0.8)], BEAM),
    ('Storm Knot', 'A ball of charge bursting on each, throwing sparks about.', [P('spikes', 0, 0.7), P('spark', 0.05, 0.6)], BEAM)],
 'arcs': [
    ('Lightning Crown', 'Arcs reaching out from you to every one of the dead around, each bursting where it lands.', [P('spark'), P('core', 0.04, 0.9)], BEAM),
    ('Thunder Nova', 'A ring of lightning racing out, then arcs to each.', [P('nova', 0, 0.9, True), P('ring', 0.02, 1.0, True), P('spark', 0.1, 0.7)], BEAM),
    ('Storm Burst', 'A great storm-burst round you, sparks raining on the dead.', [P('big'), P('spikes', 0.06, 0.9)], BEAM),
    ('Discharge', 'Charge bleeding out of the ground round you, crackling over every one.', [P('mist', 0, 1.0, True), P('spark', 0.05, 0.8)], BEAM),
    ('Skyfall', 'Bolts from the sky on every one at once.', [P('sky', 0, 0.9, True), P('ring', 0.05, 0.8, True)], None)],
 'stun': [
    ('Thunder Ring', 'A ring of lightning racing out over the ground, the dead locked stiff where it passes.', [P('ring', 0, 1.0, True), P('spikes', 0.03, 0.9, True), P('core', 0.05)], None),
    ('Static Field', 'Charge spreading over the ground and crackling up through the dead.', [P('mist', 0, 1.1, True), P('nova', 0.05, 0.6, True)], None),
    ('Crackling Dome', 'A dome of lightning swelling out round the blow and snapping shut.', [P('big', 0, 0.9), P('ring', 0.04, 0.9, True)], None),
    ('Shock Nova', 'A flat blue nova sweeping out, sparks thrown up behind it.', [P('nova', 0, 1.0, True), P('spark', 0.06, 0.8)], None),
    ('Storm Pillar', 'A column of lightning on the spot and a ring round its foot.', [P('pillar', 0, 0.9, True), P('ring', 0.05, 0.8, True)], None),
    ('Overload', 'Sparks bursting off every surface at once, then a heavy crack.', [P('spikes', 0, 1.1, True), P('core', 0.1, 1.1)], None)],
 'toll': [
    ('Funeral Rings', 'Three rings rolling out like the bell\'s sound.', [P('ring', 0, 1.0, True), P('ring', 0.3, 1.3, True), P('ring', 0.6, 1.6, True), P('core')], None),
    ('Grave Light', 'A burst of pale gold light and the dead staggering in it.', [P('big', 0, 0.8), P('rune', 0.05, 0.9, True)], None),
    ('Turning Toll', 'The undead-turning light of the Dawnguard spreading round you.', [P('nova', 0, 1.0, True), P('spark', 0.05)], None),
    ('Bronze Echo', 'A deep shockwave, then a ring of gold sparks hanging in the air.', [P('wave', 0, 1.0, True), P('spikes', 0.1, 0.8)], None),
    ('Last Bell', 'A sun pillar on the bell and its ring rolling out.', [P('pillar', 0, 0.8, True), P('ring', 0.1, 1.2, True)], None)],
 'freeze': [
    ('Ice Shell', 'Ice closing over the one struck in a burst of shards.', [P('core'), P('spikes', 0.05, 0.6, True)], None),
    ('Frost Bloom', 'A bloom of frost bursting out of it and hanging in the air.', [P('big', 0, 0.6), P('mist', 0.1, 0.6)], None),
    ('Rime Lock', 'Frost spreading over the ground under it and creeping up.', [P('rune', 0, 0.8, True), P('spark', 0.05, 0.8)], None),
    ('Glacial Spike', 'An icicle shattering into it, ice dust left hanging.', [P('m:magic\\icicleimpactactor01.nif', 0, 1.2), P('mist', 0.08, 0.5)], None),
    ('Deep Cold', 'A cold blue haze sinking over it from above.', [P('sky', 0, 0.6, True), P('core', 0.08, 0.8)], None),
    ('Frozen Breath', 'A gust of freezing breath, white and blue.', [P('drain'), P('spark', 0.06, 0.7)], None)],
 'slow': [
    ('Chill Touch', 'A small cold burst and frost clinging after.', [P('spark'), P('mist', 0.06, 0.4)], None),
    ('Frost Haze', 'A cold haze wrapped round the one struck.', [P('mist', 0, 0.6), P('core', 0.05, 0.6)], None),
    ('Rime Mark', 'A ring of rime on the ground under it.', [P('rune', 0, 0.7, True), P('spark', 0.05, 0.6)], None),
    ('Ice Needle', 'A thin icicle and a puff of frost.', [P('core', 0, 0.8), P('spark', 0.04, 0.6)], None),
    ('Winter Breath', 'A breath of cold that hangs round it.', [P('drain', 0, 0.8), P('mist', 0.06, 0.4)], None)],
 'freeze_ring': [
    ('Frost Ring', 'A ring of frost racing out, ice bursting up where it passes.', [P('ring', 0, 1.0, True), P('spikes', 0.05, 1.0, True), P('mist', 0.2, 0.8)], None),
    ('Frost Nova', 'Apocalypse\'s frost nova bursting out round you.', [P('nova', 0, 1.0, True), P('ring', 0.02, 1.0, True)], None),
    ('Glacier', 'Ice shards bursting out of the ground in a circle.', [P('spikes', 0, 1.2, True), P('rune', 0.05, 1.0, True)], None),
    ('Winter Bloom', 'A great frost explosion and the cold hanging after.', [P('big', 0, 1.0), P('mist', 0.15, 0.9)], None),
    ('Shiver', 'Shards rising round you, then a flat cold wave.', [P('pillar', 0, 0.9, True), P('wave', 0.1, 0.9, True)], None)],
 'shatter': [
    ('Shatter', 'Every frozen one bursting into shards at once.', [P('spikes', 0, 0.9, True), P('core', 0.03)], None),
    ('Ice Storm', 'A ring of frost then every frozen one breaking.', [P('ring', 0, 1.0, True), P('rune', 0.05, 0.9, True), P('spark', 0.08)], None),
    ('Avalanche', 'A crashing frost explosion round you.', [P('big', 0, 1.1), P('nova', 0.05, 0.9, True)], None),
    ('Crystal Burst', 'A glitter of ice dust bursting off every one.', [P('mist', 0, 0.9), P('spikes', 0.04, 0.7, True)], None),
    ('Howling Shatter', 'A howl of cold and the ice breaking.', [P('drain'), P('wave', 0.05, 1.0, True), P('core', 0.08)], None)],
 'blizzard': [
    ('Blizzard', 'A white storm of snow and frost round the dead for six seconds.', [P('mist', 0, 1.1), P('rune', 0.1, 1.2, True)], None),
    ('Ice Storm', 'Skyrim\'s ice storm whirling over the ground.', [P('rune', 0, 1.4, True), P('spark', 0.2, 0.8)], None),
    ('Frost Cloud', 'A cold cloud settling and frost bursting in it.', [P('big', 0, 0.9), P('mist', 0.2, 1.2)], None),
    ('Whiteout', 'Freezing wind sweeping out and a frost fog after.', [P('wave', 0, 1.0, True), P('mist', 0.1, 1.2)], None),
    ('Hailstorm', 'Ice shards falling all over the ground.', [P('spikes', 0, 1.1, True), P('spikes', 0.4, 1.1, True), P('mist', 0.2, 0.9)], None)],
 'poison': [
    ('Venom Splash', 'A splash of green venom over the one struck.', [P('spark'), P('core', 0.05, 0.8)], None),
    ('Toxic Bloom', 'A cloud of poison blooming out of it.', [P('mist', 0, 0.6), P('core', 0.05, 0.7)], None),
    ('Rot', 'A rotting burst and green smoke curling up.', [P('nova', 0, 0.5, True), P('rune', 0.05, 0.8)], None),
    ('Spitting Venom', 'Venom spraying off it in all directions.', [P('spikes', 0, 0.8), P('spark', 0.04, 0.7)], None),
    ('Sickness', 'A sickly green glow sinking into it.', [P('drain'), P('rune', 0.05, 0.6)], None)],
 'burst_all': [
    ('Plague Burst', 'Every poisoned one bursting in a green blast.', [P('big', 0, 0.8), P('spark', 0.05)], None),
    ('Toxic Pop', 'Each one popping in a cloud of poison.', [P('core'), P('mist', 0.1, 0.6)], None),
    ('Rot Bloom', 'A rot blast and a green fog over the ground.', [P('nova', 0, 0.9, True), P('mist', 0.15, 0.8)], None),
    ('Venom Fountain', 'Venom gushing up out of each one.', [P('pillar', 0, 0.9, True), P('spikes', 0.06, 0.8)], None),
    ('Gas Trap', 'A burst of trapped gas going off round the dead.', [P('sky', 0, 0.9, True), P('core', 0.05, 0.8)], None)],
 'leech': [
    ('Blood Drain', 'A red stream drawn out of the one struck and into you.', [P('drain'), P('spark', 0.05, 0.6)], BEAM),
    ('Crimson Burst', 'A burst of blood and red mist.', [P('core'), P('spark', 0.05, 0.7)], None),
    ('Vampire\'s Kiss', 'Dawnguard\'s vampire drain pulling blood out.', [P('m:dlc01\\magic\\dlc1vampiredrainimpact.nif', 0, 1.0), P('mist', 0.08, 0.5)], BEAM),
    ('Blood Rune', 'A blood rune flaring under it.', [P('rune', 0, 0.8, True), P('drain', 0.05, 0.8)], None),
    ('Heart Strike', 'A sharp red flash and a spray.', [P('spikes', 0, 0.7), P('core', 0.04, 0.6)], None)],
 'bleed': [
    ('Bleeding Wound', 'Blood spraying from the cut and dripping after.', [P('spark'), P('core', 0.05, 0.6)], None),
    ('Blood Mist', 'A fine red mist bursting off it.', [P('mist', 0, 0.5), P('spark', 0.04, 0.6)], None),
    ('Deep Gash', 'A heavy burst of blood and a dark red flash.', [P('core', 0, 1.0), P('spikes', 0.05, 0.6)], None),
    ('Blood Rune', 'A red rune flaring on the ground under it.', [P('rune', 0, 0.7, True), P('spark', 0.05, 0.7)], None),
    ('Hemorrhage', 'Blood bursting out and pooling.', [P('big', 0, 0.5), P('drain', 0.06, 0.6)], None)],
 'drain_area': [
    ('Blood Moon', 'Red streams drawn out of every one round you.', [P('mist', 0, 0.8), P('drain', 0.05)], BEAM),
    ('Crimson Wave', 'A wave of blood rolling out over the ground.', [P('wave', 0, 1.0, True), P('spark', 0.06, 0.8)], None),
    ('Feast', 'A burst of blood off every one and a red fog.', [P('big', 0, 0.8), P('mist', 0.15, 0.9)], None),
    ('Blood Ring', 'A blood rune spreading under them and red light rising.', [P('rune', 0, 1.2, True), P('pillar', 0.08, 0.8, True)], None),
    ('Vampiric Storm', 'Dawnguard\'s vampire burst, red and black.', [P('m:dlc01\\magic\\dlc1vampireabsorbexplosion.nif', 0, 0.6), P('drain', 0.08)], BEAM)],
 'shackle': [
    ('Spectral Shackle', 'A green flash and ghostly sparks pinning the one struck.', [P('spark'), P('m:effects\\impacteffects\\fxmetalsparkimpactblunt.nif', 0.03, 1.0)], None),
    ('Binding Rune', 'A rune of binding flaring under it.', [P('rune', 0, 0.7, True), P('spark', 0.05, 0.8)], None),
    ('Gaol Light', 'A column of pale light holding it in place.', [P('pillar', 0, 0.6, True), P('core', 0.05, 0.8)], None),
    ('Iron Grip', 'A ghostly burst and iron sparks.', [P('core'), P('m:effects\\impacteffects\\fxmetalsparkimpactblunt.nif', 0.03, 1.2)], None),
    ('Paralysis', 'Skyrim\'s paralysis flash, cold and still.', [P('m:magic\\paralyzeimpact01.nif', 0, 1.2), P('mist', 0.06, 0.4)], None)],
 'pull': [
    ('Chain Haul', 'Spectral chains catching every one near and hauling them in.', [P('spark'), P('core', 0.05, 0.7)], BEAM),
    ('Grave Pull', 'A pull of ghostly light dragging them together, a burst where they meet.', [P('big', 0, 0.7), P('spark', 0.05)], BEAM),
    ('Binding Circle', 'A binding rune under them all as they are dragged in.', [P('rune', 0, 1.0, True), P('spark', 0.06, 0.8)], BEAM),
    ('Soul Tether', 'Pale tethers of soul-light from you to each.', [P('drain'), P('mist', 0.1, 0.6)], BEAM),
    ('Warden\'s Call', 'A ghostly nova and the dead stumbling inward.', [P('nova', 0, 0.8, True), P('core', 0.05, 0.8)], BEAM)],
 'blind': [
    ('Blinding Flash', 'A white flash in its face.', [P('m:magic\\impactlightspell.nif', 0, 1.2), P('spark', 0.04, 0.8)], None),
    ('Mist Veil', 'A cold white mist wrapping its head.', [P('mist', 0, 0.5), P('core', 0.05, 0.7)], None),
    ('Glare', 'A bright burst of light like the sun on snow.', [P('m:magic\\explosionillusionlight01.nif', 0, 0.8), P('spark', 0.05, 0.7)], None),
    ('Ghost Light', 'A ghostly flare and pale wisps.', [P('core'), P('sky', 0.04, 0.6)], None),
    ('Frost Glint', 'A glint of frost and white mist.', [P('frost:spark', 0, 0.8), P('mist', 0.05, 0.5)], None)],
 'fog': [
    ('Lost Fog', 'A thick fog where the dead wander lost.', [P('mist', 0, 1.0), P('mist', 0.5, 1.2)], None),
    ('Creeping Mist', 'Mist rolling out low over the ground.', [P('wave', 0, 0.9, True), P('mist', 0.15, 1.0)], None),
    ('Dream Haze', 'A haze settling, lit from inside now and then.', [P('mist', 0, 1.1), P('core', 0.3, 0.7)], None),
    ('Veil', 'A pale veil rising out of the ground round them.', [P('rune', 0, 1.0, True), P('mist', 0.1, 0.9)], None),
    ('Smother', 'A burst of mist and then stillness.', [P('big', 0, 0.8), P('mist', 0.2, 1.0)], None)],
 'gust': [
    ('Gale', 'A gust of wind bursting ahead of you.', [P('core'), P('spark', 0.05, 0.8)], None),
    ('Cutting Wind', 'A sharp blast of wind and flying dust.', [P('big', 0, 0.6), P('earth:mist', 0.05, 0.6, True)], None),
    ('Whirl', 'A whirl of mist spinning off the blow.', [P('mist', 0, 0.5), P('core', 0.04, 0.7)], None),
    ('Thu\'um Push', 'The force of a shout rolling ahead.', [P('wave', 0, 0.8, True), P('core', 0.04, 0.7)], None),
    ('Updraft', 'Wind and dust thrown up from the ground.', [P('nova', 0, 0.6, True), P('earth:mist', 0.05, 0.7, True)], None)],
 'cone': [
    ('Unrelenting Force', 'Skyrim\'s shout wave knocking them down.', [P('big'), P('wave', 0.05, 1.0, True)], None),
    ('Storm Wall', 'A wall of wind and dust rolling over them.', [P('wave', 0, 1.1, True), P('earth:mist', 0.06, 1.0, True)], None),
    ('Peak Gale', 'A gale off the mountain, mist and force.', [P('mist', 0, 0.8), P('core', 0.04)], None),
    ('Shout Blast', 'A bright blast of force and a ring on the ground.', [P('spikes'), P('ring', 0.04, 1.0, True)], None),
    ('Battle Tide', 'Apocalypse\'s battle tide crashing over them.', [P('nova', 0, 1.0, True), P('core', 0.05)], None)],
 'stagger': [
    ('Shockwave', 'A ring racing over the ground and the dead staggering.', [P('ring', 0, 1.0, True), P('core'), P('wave', 0.08, 1.0, True)], None),
    ('Ground Slam', 'The ground bursting round you in dust and stone.', [P('nova', 0, 1.0, True), P('mist', 0.05, 0.9, True)], None),
    ('Quake', 'A heavy quake, stone thrown up round you.', [P('big', 0, 0.9, True), P('spikes', 0.06, 0.8, True)], None),
    ('Stomp', 'A dragon\'s stomp: dust rolling out flat.', [P('earth:wave', 0, 1.0, True), P('spark', 0.05)], None),
    ('Force Ring', 'A bright ring of force and sparks.', [P('ring', 0, 1.1, True), P('spark', 0.03, 1.0)], None),
    ('Heavy Blow', 'A great impact and a cloud round it.', [P('sky', 0, 0.9, True), P('mist', 0.1, 0.8)], None)],
 'armour': [
    ('Shatter Plate', 'Sparks and shards flying off as the armour breaks.', [P('spark'), P('m:effects\\impacteffects\\fxmetalsparkimpactblunt.nif', 0.02, 1.3)], None),
    ('Crushing Blow', 'A heavy dusty impact.', [P('core'), P('mist', 0.05, 0.6, True)], None),
    ('Rending', 'A flash of light and a spray of grit.', [P('spikes', 0, 0.6), P('core', 0.04, 0.6)], None),
    ('Stone Crack', 'Stone cracking under the blow.', [P('rune', 0, 0.8, True), P('spark', 0.04)], None),
    ('Seal Break', 'A burst of green light breaking out of it.', [P('spirit:core', 0, 0.9), P('spark', 0.04, 0.8)], None)],
 'knock': [
    ('Knockback', 'A blast of dust and force throwing it back.', [P('core'), P('spark', 0.04, 0.9)], None),
    ('River Push', 'A gush of force like a burst of water.', [P('frost:mist', 0, 0.5), P('core', 0.04, 0.8)], None),
    ('Stone Fist', 'A heavy stone impact.', [P('spikes', 0, 0.6), P('core', 0.04)], None),
    ('Shove', 'A flat wave of force.', [P('wave', 0, 0.6, True), P('spark', 0.04)], None),
    ('Rockfall', 'Stone thrown up and dust after.', [P('pillar', 0, 0.6, True), P('mist', 0.06, 0.6, True)], None)],
 'split': [
    ('Earth Split', 'The ground splitting in a line ahead, dust and stone bursting up.', [P('pillar', 0, 1.0, True), P('mist', 0.06, 1.0, True)], None),
    ('Talons of Nirn', 'Apocalypse\'s stone talons bursting out of the ground.', [P('spikes', 0, 1.0, True), P('core', 0.06)], None),
    ('Grindstone', 'Grinding stone erupting where it strikes.', [P('big', 0, 0.8, True), P('spark', 0.05)], None),
    ('Fault Line', 'A long burst of dust rolling forward.', [P('wave', 0, 1.0, True), P('mist', 0.1, 1.0, True)], None),
    ('Riverbed', 'Water and stone bursting up together.', [P('frost:mist', 0, 0.7), P('spikes', 0.05, 0.8, True)], None)],
 'pierce': [
    ('Piercing Flame', 'Fire bursting through each one in a line.', [P('core'), P('spark', 0.04, 0.8)], None),
    ('Dragon Fang', 'A sharp burst of dragonfire.', [P('sky', 0, 0.6, True), P('core', 0.04, 0.8)], None),
    ('Skewer', 'A thin flash of fire and sparks.', [P('spark'), P('spikes', 0.04, 0.5)], None),
    ('Wyrm Breath', 'A gout of flame along the thrust.', [P('m:magic\\magfirebreathshout.nif', 0, 0.6), P('spark', 0.05)], None),
    ('Ember Lance', 'Embers burst out behind the point.', [P('mist', 0, 0.5), P('core', 0.04, 0.7)], None)],
 'burn': [
    ('Set Alight', 'Flames catching on the one struck.', [P('core'), P('spark', 0.05, 0.8)], None),
    ('Ember Burst', 'A burst of embers flying off it.', [P('spark'), P('spikes', 0.04, 0.5)], None),
    ('Firebrand', 'A fiery rune flaring on it.', [P('rune', 0, 0.6, True), P('core', 0.05, 0.8)], None),
    ('Searing', 'A white-hot flash then smoke.', [P('big', 0, 0.5), P('mist', 0.08, 0.5)], None),
    ('Wildfire', 'Flames spreading out over the ground under it.', [P('pillar', 0, 0.5, True), P('spark', 0.05, 0.8)], None)],
 'embers': [
    ('Ember Fan', 'A fan of embers thrown ahead.', [P('spikes'), P('spark', 0.05)], None),
    ('Firewave', 'A wave of fire rolling ahead.', [P('wave', 0, 1.0, True), P('core', 0.05)], None),
    ('Cinder Storm', 'A cloud of burning cinders.', [P('mist', 0, 0.9), P('spark', 0.05)], None),
    ('Flame Burst', 'A big burst of fire.', [P('big', 0, 0.8), P('spikes', 0.05, 0.6)], None),
    ('Hearth Blast', 'Fire bursting up out of the ground.', [P('pillar', 0, 0.7, True), P('nova', 0.05, 0.6, True)], None)],
 'twin': [
    ('Frost and Sun', 'Frost on one blow, sunfire on the next.', [P('frost:core'), P('core', 0.05)], None),
    ('Eclipse', 'Cold light and warm light bursting together.', [P('frost:spark', 0, 0.9), P('spark', 0.03)], None),
    ('Corona', 'A ring of sunlight round a cold core.', [P('frost:core', 0, 0.8), P('rune', 0.04, 0.8, True)], None),
    ('Moonfire', 'Pale fire with frost glittering in it.', [P('mist', 0, 0.6), P('frost:spark', 0.04, 0.8)], None),
    ('Day and Night', 'A sunburst, then ice.', [P('big', 0, 0.6), P('frost:spikes', 0.08, 0.6, True)], None)],
 'twin_ring': [
    ('Ice then Sun', 'An ice shockwave, then a sun shockwave.', [P('nova', 0, 1.0, True), P('ring', 0.02, 1.0, True), P('sun:nova', 0.5, 1.0, True)], None),
    ('Eclipse Ring', 'A frost ring and a ring of sunfire racing after it.', [P('ring', 0, 1.0, True), P('spikes', 0.05, 0.9, True), P('sun:wave', 0.4, 1.0, True)], None),
    ('Solstice', 'A frost explosion and a sun burst on top of it.', [P('big', 0, 0.9), P('sun:big', 0.4, 0.9)], None),
    ('Cold Dawn', 'Ice bursting up round you, then dawn light washing over.', [P('spikes', 0, 1.0, True), P('sun:mist', 0.4, 0.9)], None),
    ('Twin Novas', 'Two novas, cold and warm, back to back.', [P('nova', 0, 0.9, True), P('sun:spikes', 0.45, 0.9)], None)],
 'star': [
    ('Wandering Star', 'A small gold ball bouncing from one to the next, a gold burst on each.', [P('m:magus\\mysticism\\sunbeamexplosion01.nif', 0, 0.35)], None),
    ('Falling Star', 'A star-flash and sparkle on each.', [P('spark', 0, 0.8), P('sky', 0.03, 0.3)], None),
    ('Comet', 'A burst of sunlight and a trail of motes on each.', [P('core', 0, 0.8), P('rune', 0.03, 0.6, True)], None),
    ('Twinkle', 'A small bright twinkle on each, quick and light.', [P('m:magic\\impactlightspell.nif', 0, 0.8)], None),
    ('Sunspark', 'Dawnguard\'s sun-fire popping on each.', [P('core', 0, 1.0)], None)],
 'fling': [
    ('Star Burst', 'A bright golden blast throwing it far.', [P('big', 0, 0.8), P('spark', 0.04)], None),
    ('Skybolt', 'Auriel\'s skybolt blasting it away.', [P('sky', 0, 0.5, True), P('core', 0.04)], None),
    ('Sun Hammer', 'A heavy sun-flash and a ring.', [P('core'), P('ring', 0.03, 0.8, True)], None),
    ('Radiant Push', 'A wave of light sweeping it away.', [P('wave', 0, 0.8, True), P('spark', 0.04)], None),
    ('Nova Kick', 'An undead-turning nova on the one struck.', [P('nova', 0, 0.6, True), P('core', 0.04, 0.8)], None)],
 'flash': [
    ('Silver Flash', 'A silver flash of light that slows them.', [P('m:magic\\impactlightspell.nif', 0, 1.2), P('spark', 0.04, 0.8)], None),
    ('Moonbeam', 'A thin beam of moonlight down onto it.', [P('sun:pillar', 0, 0.4, True), P('spark', 0.05, 0.8)], None),
    ('Reaper\'s Gleam', 'A gleam and a puff of pale mist.', [P('core'), P('mist', 0.05, 0.4)], None),
    ('Moon Rune', 'A pale rune flaring under it.', [P('rune', 0, 0.6, True), P('spark', 0.04, 0.7)], None),
    ('Ghost Glint', 'A ghostly glint and pale wisps.', [P('spark'), P('sky', 0.03, 0.5)], None)],
 'raise': [
    ('Raise Dead', 'Necromantic light pulling it back up.', [P('m:magic\\reanimateimpact01.nif', 0, 1.2), P('spirit:rune', 0.6, 0.8, True)], None),
    ('Soul Harvest', 'Its soul torn out in a green flash and put back.', [P('drain'), P('core', 0.6, 1.0)], None),
    ('Grave Rising', 'A grave-light pillar raising it.', [P('pillar', 0, 0.8, True), P('spark', 0.6)], None),
    ('Life\'s Finale', 'Apocalypse\'s necromantic burst.', [P('big', 0, 0.8), P('rune', 0.6, 0.8, True)], None),
    ('Restless', 'A cold ghostly burst and pale mist.', [P('nova', 0, 0.7, True), P('mist', 0.6, 0.6)], None)],
 'wail': [
    ('Mourning Wail', 'A wail rolling out round you, the dead cowering.', [P('wave', 0, 1.0, True), P('mist', 0.1, 0.7)], None),
    ('Scream of Pain', 'Apocalypse\'s scream of pain.', [P('shadow:wave', 0, 0.9, True), P('core', 0.05, 0.7)], None),
    ('Grief Ring', 'A red ring and a red mist.', [P('ring', 0, 1.0, True), P('mist', 0.1, 0.8)], None),
    ('Sorrow', 'A blood rune spreading and weeping light.', [P('rune', 0, 1.1, True), P('drain', 0.1)], None),
    ('Lament', 'A dark nova and ash in the air.', [P('nova', 0, 0.9, True), P('spark', 0.08)], None)],
 'sleep': [
    ('Dreamless Sleep', 'A violet haze settling over it.', [P('mist', 0, 0.5), P('core', 0.05, 0.6)], None),
    ('Night Veil', 'A dark veil falling over it.', [P('sky', 0, 0.6, True), P('spark', 0.05, 0.7)], None),
    ('Lull', 'A soft illusion pulse.', [P('spark'), P('rune', 0.05, 0.6, True)], None),
    ('Nightmare Kiss', 'A shadowy nightmare burst.', [P('big', 0, 0.5), P('spark', 0.05, 0.6)], None),
    ('Somnus', 'A soul-rend flash and sleep.', [P('m:dlc01\\magic\\dlc01_soulrendimpact01.nif', 0, 1.0), P('mist', 0.05, 0.4)], None)],
 'magic': [
    ('Arcane Burst', 'A burst of raw magic.', [P('core'), P('spark', 0.05, 0.8)], None),
    ('Mana Bloom', 'Arcane light blooming out.', [P('big', 0, 0.35), P('rune', 0.05, 0.7, True)], None),
    ('Mystic Flash', 'Mysticism\'s flash and motes.', [P('mist', 0, 0.6), P('spark', 0.04)], None),
    ('Magnus Echo', 'The Staff of Magnus\'s impact.', [P('sky', 0, 0.9), P('spark', 0.04, 0.8)], None),
    ('Constellation', 'Stars bursting from the blow.', [P('pillar', 0, 0.6, True), P('core', 0.05, 0.7)], None)],
 'mana': [
    ('Magicka Wave', 'A wave of magicka rolling out.', [P('wave', 0, 1.0, True), P('core', 0.05)], None),
    ('Lunar Tide', 'Apocalypse\'s lunar tide washing round you.', [P('big', 0, 1.0), P('ring', 0.05, 1.0, True)], None),
    ('Mana Void', 'A void opening and snapping shut.', [P('nova', 0, 1.0, True), P('spark', 0.05)], None),
    ('Arcane Ring', 'A ring of arcane light and motes rising.', [P('ring', 0, 1.0, True), P('mist', 0.05, 0.9)], None),
    ('Stargate', 'A constellation burst round you.', [P('pillar', 0, 0.9, True), P('rune', 0.05, 0.9, True)], None)],
 'portal': [
    ('Skyrim\'s Portal', 'Skyrim\'s own conjuring portal.', [P('m:magic\\summontargetfx.nif', 0, 1.0)], None),
    ('Soul Gate', 'The portal and a ring of soul-light.', [P('m:magic\\summontargetfx.nif', 0, 1.0), P('ring', 0.05, 0.6, True)], None),
    ('Summoning Circle', 'Apocalypse\'s summoning burst.', [P('m:apocnew\\conjurationsummon_explosion.nif', 0, 0.9), P('spark', 0.1, 0.7)], None),
    ('Star Door', 'A constellation opening on the ground.', [P('pillar', 0, 0.8, True), P('m:magic\\summontargetfx.nif', 0.05, 0.8)], None),
    ('Spirit Door', 'A pale ghost-light door.', [P('spirit:pillar', 0, 0.9, True), P('spirit:spark', 0.05)], None)],
 'tornado': [
    ('Fire Tornado', 'A whirling tornado of fire.', [P('big', 0, 1.0, True), P('wave', 0.1, 1.0, True), P('mist', 0.2, 1.0)], None),
    ('Pillar of Flame', 'A standing pillar of fire, flames spiralling up it.', [P('pillar', 0, 1.0, True), P('nova', 0.1, 0.8, True)], None),
    ('Firestorm', 'Fire bursting round the funnel.', [P('nova', 0, 1.0, True), P('spikes', 0.1, 0.9)], None),
    ('Ashen Whirl', 'A dark whirl of ash and embers.', [P('mist', 0, 1.1), P('spark', 0.1)], None),
    ('Inferno', 'A great fire burst and burning ground.', [P('big', 0, 1.1), P('rune', 0.1, 1.0, True)], None)],
 'fireball': [
    ('Fireball', 'A fireball bursting on the one struck.', [P('spark'), P('core', 0.05, 0.8)], None),
    ('Firebolt', 'A tight bolt of fire and sparks.', [P('core'), P('spikes', 0.04, 0.5)], None),
    ('Flame Burst', 'A bigger burst and smoke.', [P('big', 0, 0.6), P('mist', 0.08, 0.5)], None),
    ('Dragonfire', 'A spit of dragonfire.', [P('sky', 0, 0.5, True), P('core', 0.04, 0.8)], None),
    ('Fire Rune', 'A rune of fire flaring where it lands.', [P('rune', 0, 0.6, True), P('spark', 0.04)], None)],
 'sunball': [
    ('Sun Ball', 'A ball of sunlight bursting on it.', [P('core'), P('spark', 0.05, 0.8)], None),
    ('Sunburst', 'A sunburst and rays.', [P('wave', 0, 0.6, True), P('core', 0.04)], None),
    ('Turn Undead', 'The undead-turning flash.', [P('rune', 0, 0.8, True), P('spark', 0.04)], None),
    ('Daybreak', 'A bright burst like dawn.', [P('big', 0, 0.6), P('mist', 0.06, 0.6)], None),
    ('Radiance', 'A soft radiant flash and motes.', [P('mist', 0, 0.6), P('spark', 0.03, 0.8)], None)],
 'sunbeam': [
    ('Sunbeam', 'A sunbeam out of the sky and its burst.', [P('pillar', 0, 1.0, True), P('big', 0.0, 0.9), P('ring', 0.5, 1.2, True)], None),
    ('Skybolt', 'Auriel\'s skybolt slamming down.', [P('sky', 0, 0.6, True), P('ring', 0.1, 1.2, True)], None),
    ('Solar Pillar', 'A pillar of sunlight with turning motes.', [P('pillar', 0, 1.1, True), P('core', 0.1), P('spark', 0.3)], None),
    ('Judgement', 'A sun burst and undead-turning light spreading.', [P('big', 0, 1.0), P('nova', 0.1, 1.0, True)], None),
    ('Daybreak Ring', 'A beam and a ring of dawn rolling out.', [P('pillar', 0, 0.9, True), P('wave', 0.2, 1.1, True)], None)],
 'meteor': [
    ('Meteor', 'A fireball falling from the sky and a ring where it lands.', [P('big', 0, 0.9, True), P('core', 0.04), P('ring', 0.05, 1.1, True)], None),
    ('Dragon\'s Fall', 'A dragon\'s landing: fire and dust.', [P('sky', 0, 0.9, True), P('earth:mist', 0.05, 1.0, True)], None),
    ('Hellfire', 'Apocalypse\'s hellfire burst.', [P('nova', 0, 0.9, True), P('spark', 0.05)], None),
    ('Pyroclasm', 'A volcanic burst of fire and rock.', [P('spikes', 0, 1.0, True), P('mist', 0.1, 0.8)], None),
    ('Starfall', 'A blazing burst and burning motes.', [P('core', 0, 1.2), P('rune', 0.05, 1.0, True)], None)],
 'blast': [
    ('Wind Blast', 'A white shockwave round you that hurls the dead far.', [P('ring', 0, 1.0, True), P('core'), P('wave', 0.08, 1.0, True), P('mist', 0.2, 0.9, True)], None),
    ('Unrelenting Force', 'Skyrim\'s great shout wave.', [P('big', 0, 1.1), P('ring', 0.04, 1.0, True)], None),
    ('Battle Tide', 'Apocalypse\'s battle tide crashing out.', [P('nova', 0, 1.1, True), P('wave', 0.06, 1.0, True)], None),
    ('Dust Storm', 'Wind and dust exploding outward.', [P('wave', 0, 1.1, True), P('earth:nova', 0.04, 1.0, True)], None),
    ('Storm Burst', 'A burst of wind and a crack of thunder.', [P('spikes', 0, 1.0), P('sky', 0.05, 0.8, True)], None)],
 'push': [
    ('Lunar Tide', 'A ring that throws them back out.', [P('big', 0, 0.9), P('ring', 0.03, 1.0, True)], None),
    ('Repulse', 'A flat arcane wave pushing out.', [P('wave', 0, 1.0, True), P('spark', 0.05)], None),
    ('Void Burst', 'The void bursting outward.', [P('nova', 0, 1.0, True), P('core', 0.04)], None),
    ('Star Push', 'A constellation burst and a ring.', [P('pillar', 0, 0.8, True), P('ring', 0.04, 1.0, True)], None),
    ('Mystic Wave', 'Mysticism\'s burst and a wave.', [P('mist', 0, 0.9), P('wave', 0.05, 0.9, True)], None)],
 'pull_in': [
    ('Mana Void', 'The dead dragged into a void.', [P('nova', 0, 0.9, True), P('spark', 0.05)], BEAM),
    ('Arcane Drain', 'Arcane streams pulling them in.', [P('drain'), P('core', 0.05, 0.8)], BEAM),
    ('Singularity', 'A tight bright knot drawing them in.', [P('core', 0, 1.0), P('rune', 0.05, 0.8, True)], BEAM),
    ('Whirlpool', 'A swirl of magic pulling round and in.', [P('mist', 0, 0.9), P('ring', 0.05, 0.8, True)], BEAM),
    ('Black Star', 'A dark star collapsing.', [P('shadow:nova', 0, 0.9, True), P('spark', 0.05)], BEAM)],
 'pool': [
    ('Burst and Pool', 'A burst and the ground left in the element.', [P('core'), P('rune', 0.05, 1.0, True)], None),
    ('Splash', 'A splash spreading wide over the ground.', [P('spark'), P('wave', 0.05, 0.8, True)], None),
    ('Lingering Cloud', 'A burst and a cloud hanging over the pool.', [P('core'), P('mist', 0.1, 0.8)], None),
    ('Eruption', 'The pool bursting up a moment after.', [P('rune', 0, 1.0, True), P('pillar', 0.3, 0.7, True)], None),
    ('Shatterflask', 'The flask bursting with shards and a flash.', [P('spikes', 0, 0.7), P('core', 0.04, 0.8)], None)],
 'firestorm': [
    ('Fire Storm', 'Skyrim\'s fire storm ring of fire rolling out.', [P('m:magic\\firestormareaeffect.nif', 0, 1.0, True)], None),
    ('Hellfire Ring', 'A hellfire burst and a ring of fire.', [P('nova', 0, 1.0, True), P('ring', 0.03, 1.1, True)], None),
    ('Inferno', 'A huge fireball blast and burning ground.', [P('big', 0, 1.1), P('rune', 0.08, 1.1, True)], None),
    ('Fire Wave', 'A flat wave of fire sweeping out.', [P('wave', 0, 1.2, True), P('spikes', 0.06, 0.9)], None),
    ('Pyre', 'Fire columns round you.', [P('pillar', 0, 1.0, True), P('nova', 0.06, 0.9, True)], None)],
 'strike': [
    ('Lightning Strike', 'Apocalypse\'s lightning strike from the sky.', [P('m:apocnew\\destructionshock_hazard_lightningstrike.nif', 0, 1.0)], None),
    ('Finger of the Mountain', 'A great bolt from above.', [P('sky', 0, 1.0, True), P('pillar', 0.04, 0.8, True)], None),
    ('Thunderclap', 'A thunder nova on the ground.', [P('nova', 0, 1.0, True), P('spark', 0.05)], None),
    ('Storm Fall', 'A mass shock burst.', [P('big', 0, 1.0), P('ring', 0.04, 1.0, True)], None),
    ('Static Strike', 'A strike and crackling ground after.', [P('sky', 0, 0.9, True), P('mist', 0.1, 1.0, True)], None)],
 'ice_line': [
    ('Ice Spike Volley', 'Skyrim\'s ice spike shards, one after another along your aim.', [P('spikes', 0, 1.0, True)], None),
    ('Frost Line', 'Frost bursting up along the line.', [P('rune', 0, 0.7, True), P('spark', 0.03, 0.7)], None),
    ('Glacier Path', 'Ice shards and frost mist along the line.', [P('pillar', 0, 0.7, True), P('mist', 0.05, 0.5)], None),
    ('Shiver Line', 'Sharp ice bursting in a row.', [P('core', 0, 1.0), P('spikes', 0.03, 0.6, True)], None),
    ('Winter Road', 'A cold wave rolling along the line.', [P('nova', 0, 0.6, True), P('spark', 0.04, 0.6)], None)],
 'elixir': [
    ('Golden Elixir', 'A golden burst and a cloud that turns the dead.', [P('big', 0, 0.9), P('poison:mist', 0.1, 0.9)], None),
    ('Midas Bloom', 'Gold light blooming over the ground.', [P('rune', 0, 1.1, True), P('mist', 0.1, 0.9)], None),
    ('Aetherius Mist', 'A shining mist settling.', [P('mist', 0, 1.1), P('spark', 0.2, 0.8)], None),
    ('Turning Light', 'The undead-turning nova in gold.', [P('nova', 0, 1.0, True), P('poison:mist', 0.1, 0.8)], None),
    ('Sun Cloud', 'A sunburst and a slow gold cloud.', [P('wave', 0, 1.0, True), P('poison:mist', 0.15, 1.0)], None)],
}
# the shows (several beats: the javelin, the flasks, the bolts, the beam): their bursts take the option's parts
for k in ('javelin', 'flask', 'bolts', 'soul'):
    POOL[k] = POOL['fireball'] if k in ('javelin', 'flask') else POOL['magic']


def _part(spec, theme):
    role, d, k, g = spec
    if role.startswith('m:'):
        m = role[2:]; lab = next((p['label'] for t in RF.PARTS.values() for r, p in t.items() if r != 'native' and p['model'].lower() == m.lower()), None)
        import docshow
        return dict(model=m.lower(), s=round(k, 3), delay=d, ground=g, nat=False, label=lab or docshow.label(m.lower()))
    th, own = theme, True
    if ':' in role: (th, role), own = role.split(':'), False
    p = RF.RING if role == 'ring' else RF.PARTS[th][role]
    # (role and k kept: on a show whose beats are each their own element - the Carian Wand's fire, lightning, ice - the page
    # fills the same roles from each beat's element)
    return dict(model=p['model'].lower(), s=round(p['s'] * k, 3), delay=d, ground=g, nat=bool(p.get('nat')), label=p['label'], role=role if own and role != 'ring' else None, k=k)


def options():
    abil = {a['card']: a for a in RF.all_abilities()}
    picks = json.loads((ROOT / 'docs' / 'workshop' / 'ability_picks.json').read_text())
    out = {}
    for card, c in CARDS.items():
        kind, theme = KIND[card]
        pool = POOL[kind]
        # (four of its kind's ways, the four chosen by the card itself: two weapons of one kind get different ones where the pool allows)
        h = int(hashlib.md5(card.encode()).hexdigest(), 16)
        idx = list(range(len(pool)))
        start = h % len(pool)
        order = idx[start:] + idx[:start]
        chosen = order[:4] if len(pool) > 4 else order
        opts = []
        # the one it plays now
        ab = abil.get(card)
        if ab:
            now = [dict(model=p['model'].lower(), s=p['scale'], delay=p['delay'], ground=p['ground'], nat=False, label=p['label']) for p in RF.compose(ab)]
        else:
            now = [_part(sp, theme) for sp in pool[0][2]]
        opts.append(dict(id='a1', name='As it is now', desc='The effect the game plays today.', parts=now, link=pool[chosen[0]][3] if PATTERN.get(kind) in ('chain', 'arcs', 'pullbeam', 'leech') else None))
        for n, i in enumerate(chosen[:4]):
            name, desc, specs, link = pool[i]
            opts.append(dict(id=f'a{n + 2}', name=name, desc=desc, parts=[_part(sp, theme) for sp in specs], link=link))
        # (sizes as a base: the card's own size scales them in the page; parts listed for the card)
        out[card] = dict(kind=kind, theme=theme, pattern=PATTERN.get(kind, 'hit'), opts=opts)
    return out


def models():
    return sorted({p['model'] for c in options().values() for o in c['opts'] for p in o['parts']} | {BEAM})


if __name__ == '__main__':
    o = options()
    for k, v in list(o.items())[:6]: print(k, v['kind'], [x['name'] for x in v['opts']])
    print(len(o), 'cards,', len(models()), 'models')
