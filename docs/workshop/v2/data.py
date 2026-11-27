"""The permanent workshop's data (Noah, 2026-11-26 night: "completely redesign the Curse of Alduin workshop so it has everything
we need with a much stronger foundation ... use the workshop, test it in game, completed").

Everything the workshop shows is kept here and written to data.json by `python3 docs/workshop/v2/data.py`; the page itself never
holds a list of its own. Every feature of the mod is one row in SYSTEMS, with its status on the lifecycle Noah chose:

    idea -> building -> ready (in a build installed on his PC, waiting for his test) -> passed / failed -> done

His marks (pass / fail + note / skip, or a whole category passed at once) are saved in the workshop's own database; at each build
they are folded back here: a pass becomes done, a fail goes back to building with his note.
"""
import json, pathlib, subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent

INSTALLED = {'sha': 'f696bbb', 'date': '2026-11-26', 'label': 'Stabilization build',
             'extra': 'The nine scythes turned the right way out (installed on its own, Nov 26 20:05)'}

# ------------------------------------------------------------------------------------------------ the systems (categories)
SYSTEMS = [
    ('movement', 'Movement', 'Sprint, slide, jump, the glider and the dragon ride: how it feels to move.'),
    ('combat', 'Combat feel', 'Hits, deaths, bodies and ash, weapon sounds: what every blow feels like.'),
    ('classes', 'Class weapons', 'The four classes\' weapons, their tiers and abilities.'),
    ('spells', 'Class spells', 'The spells each class learns at the altar.'),
    ('flasks', 'Flasks', 'The Alchemist\'s six flasks.'),
    ('relics', 'Dragon Priest relics', 'The ten priests\' weapons, one per freed city.'),
    ('tablet', 'Tablet of Mysteries', 'The gamble: donated weapons with limited uses.'),
    ('wonders', 'Wonders and secrets', 'The four wonders, the hidden quests and Alduin\'s Bane.'),
    ('summons', 'Summons and thralls', 'What fights for you: conjurations, raised dead, the Soulcaller\'s pride.'),
    ('armour', 'Armour sets', 'The four class sets and their tiers.'),
    ('nights', 'Nights and the dead', 'Rounds, enemies, bosses, special nights, essence.'),
    ('dragons', 'Dragons', 'Dragon bosses and the three word-dragons.'),
    ('altars', 'Echo Altars and trials', 'Freeing each city: the four trials and the priest.'),
    ('felldir', 'Felldir\'s voice', 'The altar\'s voice, its Talk and whispers.'),
    ('hud', 'HUD and menus', 'Everything drawn on screen and every menu of ours.'),
    ('loading', 'Loading and new game', 'Loading screens, the new-character flow.'),
    ('world', 'World and mod stack', 'Grass, light, the mods we run with, performance.'),
    ('stability', 'Stability', 'Crashes and anything that stops a night.'),
]

# ------------------------------------------------------------------------------------------------ every feature
# F(id, system, title, status, what, how=..., look=..., note=..., since=...)
FEATURES = []
def F(i, s, t, st, what, how='', look='', note='', since=''):
    FEATURES.append(dict(id=i, sys=s, t=t, st=st, what=what, how=how, look=look, note=note, since=since))

B = 'f696bbb'   # (the build installed now)

# ---- movement
F('mv_sprint', 'movement', 'Sprint winds up', 'done', 'The sprint builds over 180 ms to 440 and widens the view 7 degrees.', since='286a000')
F('mv_slide', 'movement', 'Slide from a sprint', 'ready', 'Sprint for a moment, then crouch: a slide that tilts the view; jump out of it and keep the speed. Ours now (SkyParkour\'s slide is off).',
  how='Sprint for two or three seconds, then crouch. Then slide again and jump while sliding.',
  look='You slide at any point of a sprint (not only right after starting it). The jump carries the slide\'s speed forward.',
  note='Last time: "can\'t jump out of it to carry my momentum ... only lets me slide right after a sprint".', since=B)
F('mv_omni', 'movement', 'Sprint in any direction', 'ready', 'Sprint sideways and backwards too, in first and third person (your pick: "Third person too").',
  how='Hold sprint and move sideways and back, in first person, then in third person.', look='Full sprint speed in every direction, both views.', since=B)
F('mv_swing', 'movement', 'Quicker light swings', 'done', 'Light attacks 1.2x as fast; no early swing cancel (your pick: "the speed is enough").', since='286a000')

# ---- combat feel
F('cb_hitpause', 'combat', 'No hit pause', 'ready', 'The hit pause is gone everywhere: ours was already 0, Precision\'s own pause is now off in its MCM settings.',
  how='Light and heavy hits on the dead with a sword and a greatsword.', look='No pause and no camera shake on any hit.', note='You asked: "remove the hit pause".', since=B)
F('cb_soul', 'combat', 'No soul torn out', 'ready', 'Shadow, spirit and arcane kills no longer tear the soul out (removed as you asked).',
  how='Kill the dead with the Shadowflame Scythe and the Soulcaller.', look='No white beam rising from the bodies.', since=B)
F('cb_ash', 'combat', 'Ash piles gone after 5 s', 'ready', 'Fire kills burn to ash; the ash pile and the body are cleared 5 s later.',
  how='Kill the dead with fire (a fire spell, the Fire Flask).', look='They burn to a pile of ash that disappears after about 5 seconds.', since=B)
F('cb_sounds', 'combat', 'Every weapon its own sounds', 'done', 'Draw, swing, heavy and hit sounds per weapon, all at 20%.', since='15cfd57')
F('cb_ragdoll', 'combat', 'Bodies fly half as far', 'done', 'Heavy hits throw bodies at half strength; Alduin\'s Bane as before.', since='76167f7')
F('cb_marks', 'combat', 'No hit markers', 'done', 'No crosshair marks; the night\'s last kill in slow motion.', since='5865977')
F('cb_hitfeel', 'combat', 'Hit feel options', 'done', 'Removed as you asked ("Remove this").')

# ---- class weapons
F('cw_onemodel', 'classes', 'One model per class weapon', 'idea', 'Your Nov 26 decision: each class keeps one model through every tier; the tiers change its look only (stronger each tier, like Pack-a-Punch). You pick the model in Weapons & effects.')
F('cw_tierlooks', 'classes', 'Tier looks: one look, stronger each tier', 'idea', 'Tier I faint, Tier II a glowing edge, Tier III a full aura and trail, Legendary all of it plus a signature effect. Built on the element kits.')
F('cw_ownmodels', 'classes', 'Our own models from Skyrim\'s meshes', 'idea', 'Each class weapon (and later the Tablet\'s) built from Skyrim, Dawnguard and Dragonborn parts: no mod needed for it.')
F('cw_abilities', 'classes', 'Class weapon abilities rebuilt', 'idea', 'Every light and heavy ability redone on the new foundation (element kits, the Test Range).',
  note='"They all look very poorly done ... redo the abilities and have a starting foundation".')
F('cw_ults', 'classes', 'Ultimates and the kill meter removed', 'ready', 'The Tier III ultimates and their meter are gone (Tier III keeps its cards as before).',
  how='Tester\'s Kit: a class weapon at Tier III; kill 30 of the dead.', look='No meter above the perks, nothing extra on a heavy.', since=B)
F('cw_card', 'classes', 'Weapon card removed', 'ready', 'The held weapon\'s card bottom right is gone.', how='Draw any weapon.', look='Nothing drawn bottom right.', since=B)
F('cw_ascend', 'classes', 'Ascend grows the abilities', 'done', 'Each Ascend +15% damage and +10% size.', since='650d613')
F('cw_scythes', 'classes', 'Scythes solid (not inside out)', 'ready', 'All nine scythes: patches of the blades faced inward and the cut poles were open, so you saw their inside and through them. Every piece now faces out; open cuts drawn from both sides.',
  how='Tester\'s Kit: the Shadowflame Scythe at each tier, the four Tablet scythes; look at each from every side, close.',
  look='Solid from every side: no seeing the inside of the texture, no see-through.', note='"the model was inside out, so I can see the inside of the textures, and at some angles I could see through".', since='2384ca5')
F('cw_mutagen1p', 'classes', 'Mutagen Staff in first person', 'ready', 'A first-person model of its own, set lower in the hand so your hand no longer passes through its head.',
  how='Alchemist with the Mutagen Staff: first person, idle and swinging.', look='Your hand on the shaft, the head well above it.', note='"my players hand clipping through the head of the staff".', since=B)
F('cw_sheath', 'classes', 'Sheathing the Mutagen Staff', 'building', 'It must stay the Mutagen Staff when drawn and sheathed.', note='Failed last test (no note).')
F('cw_rapiershock', 'classes', 'Spellbound Rapier shock look', 'building', 'Part of the tier-look rebuild.', note='"The shock effect on the spellbound rapier still looks wrong".')
F('cw_radiant', 'classes', 'Radiant Quarterstaff heavy pillar', 'building', 'Part of the abilities rebuild.', note='"looks like a poorly done white line standing vertically".')

# ---- class spells
F('sp_own', 'spells', 'Class spells play their own effects', 'done', 'Each spell plays its mod\'s own impact, nothing of ours over it.', since='9b0de93')
F('sp_immolate', 'spells', 'Replace Immolate in the pool', 'idea', 'You asked to replace Immolate (Destruction, fire Tier III). Pick its replacement in Decisions.', note='"I want to replace the immolate from the spell pool".')

# ---- flasks
F('fl_fire', 'flasks', 'Fire Flask bursts in flames', 'ready', 'Dragonfire\'s impact and a ball-of-fire burst: flames, not a flat explosion.',
  how='Throw the Fire Flask at the ground and at a group.', look='A burst of real flames.', note='"The Fire Flask explosion should be flames".', since=B)
F('fl_holy', 'flasks', 'Holy Water without the golden streaks', 'ready', 'The flat golden wind streaks that stayed on the ground are gone.',
  how='Throw Holy Water.', look='Its sunbeam and burst; nothing flat and streaky left standing.', note='"golden wind streaky effect that\'s stagnant and looks 2D".', since=B)
F('fl_rest', 'flasks', 'The other flasks\' bursts', 'idea', 'Frost, Shock, Acid and Voidglass rebuilt on the element kits with the rest of the effects.')

# ---- relics
F('rl_abilities', 'relics', 'Relic abilities rebuilt', 'idea', 'The ten relics\' hits and heavies redone on the element kits.', note='Vahlok\'s chains and the real-parts effects failed your last test.')
F('rl_list', 'relics', 'The ten relics and what they do', 'done', 'Keyhammer, Mistblade, Windcutter, Glacier Greatsword, Storm Blade, Sacrifice Knife, Grief Blade, Sealbreaker, Nightcaller\'s Scythe, Arcane Maul.')

# ---- tablet
F('tb_lineup', 'tablet', 'The Tablet lineup redone', 'idea', 'Your Nov 26 decision: a new lineup (keep / cut / new) on our own models, each with a BO3-style ability, approved item by item in Weapons & effects.')
F('tb_claws', 'tablet', 'Both claws from the Tester\'s Kit', 'ready', 'The kit gives each claw\'s left-hand twin too, and equipping a claw puts its twin in your other hand.',
  how='Tester\'s Kit: the Tablet weapons; equip Stormtalons, Talons of Woe and Cinderclaws from the inventory.', look='A claw in each hand every time.', note='"The test kit was only giving me one claw".', since=B)
F('tb_storm', 'tablet', 'Stormtalons: lightning, not ice', 'ready', 'The heavy hit\'s stun no longer coats the dead in ice: a lightning arc and a shock burst.',
  how='Stormtalons: heavy hits, and three light hits in a row.', look='Lightning on the heavy and on every 3rd hit; no ice anywhere.', since=B)
F('tb_woe', 'tablet', 'Talons of Woe show their effect', 'ready', 'Its hit effect was drawn at a fraction of its size inside the body; now full size (Vampiric Drain, a blood burst and mist).',
  how='Talons of Woe: light and heavy hits.', look='A red blood burst and mist on every hit, bigger on the heavy.', since=B)
F('tb_bonereaper', 'tablet', 'Bonereaper', 'building', 'Its raise was the likely cause of the round 20 crash (fixed). You said it can be replaced if it was the cause: it goes into the lineup review.')

# ---- wonders
F('wd_javhand', 'wonders', 'Javelin held palm to you', 'ready', 'The hand turned so the palm faces you while charging and throwing; the charge effect over your head removed.',
  how='Destruction wonder: hold to charge, look around, throw.', look='Palm towards you on the shaft; nothing floating above your head.', note='"hand twisted ... palm facing towards the player"; "remove the charge element".', since=B)
F('wd_bane', 'wonders', 'Alduin\'s Bane light meteors', 'ready', 'Smaller impacts: no white flash and no firestorm stacked on each meteor.',
  how='Light hits on the dead with Alduin\'s Bane.', look='A meteor on each hit with a fiery impact that doesn\'t blind you.', note='"a massive explosion and it\'s blinding".', since=B)
F('wd_soulcaller', 'wonders', 'Soulcaller look', 'done', 'White soul wisps on its glass head.', since='76167f7')

# ---- summons
F('sm_portal', 'summons', 'Summons come out of the portal', 'building', 'Conjurations and the Soulcaller\'s pride should step out of Skyrim\'s conjuring portal.', note='"Doesn\'t come out of the Skyrim summon portal they just pop up".')
F('sm_thrall', 'summons', 'Raised thralls safe', 'ready', 'A thrall raised from a leveled enemy (the Frost Brute) used a temporary copy of its record that is thrown away with the body: the round 20 crash. Now made from the real record.',
  how='Bonereaper (or any raise) on many kinds of the dead, rounds 15 to 20.', look='Thralls rise and fight; no crash.', since=B)

# ---- armour
F('ar_ssbody', 'armour', 'Spellsword set shows your body', 'ready', 'The cuirass now uses a slot of its own, so the body slot stays free and your body shows under it.',
  how='Tester\'s Kit: Spellsword set at each tier; look at yourself in third person.', look='Your body under the cuirass. If the cuirass itself is missing now, tell me.',
  note='"Still makes my body invisible".', since=B)
F('ar_inv', 'armour', 'Class sets in the inventory', 'done', 'Each piece has a model in the inventory and on the ground.', since='76167f7')

# ---- nights
F('nt_enemies', 'nights', 'Only the dead you kept', 'done', 'Your keep/remove picks of Nov 26.', since='e93f679')
F('nt_n5', 'nights', 'Night 5 mini-boss', 'done', 'One of your kept mini-bosses at random.', since='a4584e6')
F('nt_loot', 'nights', 'No loot on the night\'s dead', 'ready', 'Looking at a night enemy\'s body or ash shows no loot list.',
  how='Look at bodies and ash piles of the night\'s dead; then at a chest.', look='Nothing listed for the dead; chests still list their loot.', since=B)
F('nt_float', 'nights', 'No floating dead', 'done', 'Any of the dead held in the air is set down.', since='76167f7')

# ---- dragons
F('dr_stuck', 'dragons', 'Dragons don\'t stay stuck flying', 'ready', 'A dragon aloft more than 20 s is made to land for 12 s, then may fly again.',
  how='Tester\'s Kit: night 20 (Grahzah), and a dragon in the wild.', look='Dragons come down to fight within about 20 seconds.', note='"The dragons get stuck flying in the air".', since=B)
F('dr_half', 'dragons', 'Dragons at half size', 'done', 'Every dragon half size; the bosses\' breath and called dead on the ground.', since='76167f7')

# ---- altars
F('al_redesign', 'altars', 'Trials redesigned', 'idea', 'All four trials redone with their sounds and visuals: the rings you couldn\'t see, the souls, the seals that were only magic visuals.',
  note='"We are going to have to redesign these with their sounds and visuals".')
F('al_rings', 'altars', 'Trial rings visible', 'building', 'Part of the trials redesign.', note='"The ring is still invisible for all the quests".')
F('al_souls', 'altars', 'Lay the Restless to Rest', 'building', 'Part of the trials redesign.', note='"Very visually wrong, the circle is invisible ... the audio effects are overwhelming".')
F('al_seals', 'altars', 'Break the Priest\'s Seals', 'building', 'Part of the trials redesign.', note='"the seals for the last trial are just magic ability visuals".')

# ---- felldir
F('fd_finish', 'felldir', 'Answers play to the end', 'ready', 'A question asked while Felldir is speaking waits until the answer ends, instead of cutting it off.',
  how='At a freed altar, Talk: ask a question, then click another while he answers.', look='The first answer finishes, then the second starts.', note='"The Echo Altar\'s voice sometimes cuts out".', since=B)
F('fd_four', 'felldir', 'Four questions a topic', 'ready', 'Each topic lists at most four questions, the ones you haven\'t asked first.',
  how='At a freed altar, open each Talk topic.', look='Four questions at most in each list.', note='"way too many dialogue options".', since=B)
F('fd_quiet', 'felldir', 'The altar quiet during trials', 'done', 'No subtitles until the altar is freed.', since='76167f7')

# ---- hud
F('hd_night', 'hud', 'Bigger night counter and quests', 'ready', 'The night box and the quest list under it are larger.',
  how='Start a night; have a trial and a Skyrim quest tracked.', look='Easier to read at a glance, top left.', note='"Make it larger as well as the quests under it".', since=B)
F('hd_banner', 'hud', 'Quest banner style', 'building', 'Goes into the In-game UI redesign.', note='"the banner looks off and the size needs to be a bit larger".')
F('hd_impacts2d', 'hud', 'Impact effects in 2D look', 'building', 'Impacts that look flat: part of the element kits rebuild.', note='"impact effects ... look really low quality in 2D".')

# ---- loading
F('ld_continue', 'loading', 'Continue shows our screen', 'ready', 'Our Curse of Alduin loading screen whenever a save loads from the main menu (Continue or Load).',
  how='From the main menu, Continue; then Load a save.', look='Our own screen the whole time, faded off when the world is in.', note='"Loading in a save still shows the loading screens from the mod".', since=B)
F('ld_doors', 'loading', 'No loading screens for doors', 'done', 'Doors load in about a second: no screen needed (your note).')
F('ld_newchar', 'loading', 'New character flow', 'done', 'One smooth start, menu music from launch.', since='5865977')

# ---- world
F('wo_grass', 'world', 'Floating grass at Riverwood\'s water', 'building', 'One grass piece floating over the water at Riverwood.', note='"one grass piece that was floating over the water at riverwood other than that it\'s good".')
F('wo_world', 'world', 'One grass, Screen-Space GI', 'done', 'Folkvangr and the pine grass; Screen-Space GI\'s light bounce.', since='15cfd57')
F('wo_mods', 'world', 'The 20 mods off', 'done', 'No missing masters; the world as before.', since='a4584e6')

# ---- stability
F('st_r20', 'stability', 'Round 20 crash', 'ready', 'The game fell 0.6 s after a Bonereaper kill raised a Frost Brute. The raise is fixed; Crash Logger is installed to catch anything else.',
  how='Play to round 20, raising thralls with the Bonereaper in big crowds.', look='No crash. If one comes, Crash Logger writes a log I can read.', note='"I crashed on round 20 when I did a heavy attack on a crowd using one of the tablet scythes".', since=B)


# ------------------------------------------------------------------------------------------------ the class weapons: one model each (his pick)
CLASSES = [
    dict(id='alchemist', name='Alchemist', weapon='Shadowflame Scythe', now='Forsworn at Tier I, Falmer II, Dwarven III, Dragon Priest Legendary',
         cands=[dict(key='cls_forsworn_alch', name='Forsworn scythe', src='Built from Skyrim\'s Forsworn axe and sword (ours)'),
                dict(key='cls_falmer_alch', name='Falmer scythe', src='Built from Skyrim\'s Falmer axe and longsword (ours)'),
                dict(key='cls_dwarven_alch', name='Dwarven scythe', src='Built from Skyrim\'s Dwarven battleaxe and greatsword (ours)'),
                dict(key='cls_dragonpriest_alch', name='Dragon Priest scythe', src='Built from a Dragon Priest staff and dagger (ours)')]),
    dict(id='summoner', name='Summoner', weapon='Radiant Quarterstaff', now='Steel, Ebony, Daedric, Dragonbone by tier (Animated Armoury)',
         cands=[dict(key='cls_steelstaff_radiant', name='Steel quarterstaff', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_ebonystaff_radiant', name='Ebony quarterstaff', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_daedricstaff_radiant', name='Daedric quarterstaff', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_dragonbonestaff_radiant', name='Dragonbone quarterstaff', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes')]),
    dict(id='destruction', name='Destruction', weapon='Carian Wand', now='Steel, Stalhrim, Glass, Daedric by tier (Fulcimentum)',
         cands=[dict(key='cls_steelwand_carian', name='Steel wand', src='Fulcimentum - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_stahlrimwand_carian', name='Stalhrim wand', src='Fulcimentum - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_glasswand_carian', name='Glass wand', src='Fulcimentum - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_daedricwand_carian', name='Daedric wand', src='Fulcimentum - to be rebuilt from Skyrim\'s meshes')]),
    dict(id='spellsword', name='Spellsword', weapon='Spellbound Rapier', now='Steel, Ebony, Daedric, Dragonbone by tier (Animated Armoury)',
         cands=[dict(key='cls_steelrapier_ss_none', name='Steel rapier', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_ebonyrapier_ss_none', name='Ebony rapier', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_daedricrapier_ss_none', name='Daedric rapier', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_dragonbonerapier_ss_none', name='Dragonbone rapier', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes')]),
]

# ------------------------------------------------------------------------------------------------ the Tablet's lineup today (keep / cut / rework, his marks)
TABLET = [
    dict(id='stormtalons', key='tab_stormtalons', name='Stormtalons', kind='Glass claws - worn', does='Every 3rd hit chains lightning to 2 more. Heavy: a lightning stun.'),
    dict(id='gravebell', key='tab_gravebell', name='Gravebell Mace', kind='Mace - worn', does='Every 5th hit tolls the bell, stunning the dead within 5 m.'),
    dict(id='riverstone', key='tab_riverstone', name='Riverstone Halberd', kind='Halberd - worn', does='Hits knock the dead back. Heavy: splits the ground in a line ahead.'),
    dict(id='cinderclaws', key='tab_cinderclaws', name='Cinderclaws', kind='Ember claws - worn', does='Every swipe sets the dead burning. Heavy: a fan of embers.'),
    dict(id='frostmourn', key='tab_frostmourn', name='Frostmourn Rapier', kind='Rapier - worn', does='Thrusts slow the dead. Heavy: freezes a ring of the dead round you.'),
    dict(id='silverscythe', key='tab_silverscythe', name='Silver Reaping Scythe', kind='Scythe - worn', does='Every 4th hit: a silver flash that briefly slows the dead.'),
    dict(id='wyrmtooth', key='tab_wyrmtooth', name='Wyrmtooth Scythe', kind='Scythe - worn', does='Hits make the dead bleed. Heavy: a shockwave that staggers within 8 m.'),
    dict(id='lanternwick', key='tab_lanternwick', name='Rimewick Quarterstaff', kind='Quarterstaff - battered', does='Light: freezes the dead for 3 s. Heavy: a 10 m blizzard for 6 s.'),
    dict(id='talons', key='tab_talons', name='Talons of Woe', kind='Dragonbone claws - battered', does='Their bleed runs twice as deep; kills heal you a little.'),
    dict(id='wyrmspine', key='tab_wyrmspine', name='Wyrmspine Pike', kind='Pike - battered', does='Thrusts pierce 2 more in a line. Heavy: a shockwave within 8 m.'),
    dict(id='eclipse', key='tab_eclipse', name='Eclipse Edge', kind='Glass pike - battered', does='Hits alternate frost and sun. Heavy: an ice shockwave, then a sun shockwave.'),
    dict(id='wanderstar', key='', name='The Wandering Star', kind='Wand - battered', does='Hold to cast a gold star every 0.5 s that bounces to up to 4 of the dead.'),
    dict(id='bonereaper', key='tab_bonereaper', name='Bonereaper', kind='Scythe - battered', does='1 kill in 4 rises to fight for you for 20 s. (Its raise caused the round 20 crash - fixed.)'),
    dict(id='bloodmoon', key='tab_bloodmoon', name='Bloodmoon Scythe', kind='Scythe - battered', does='Hits drain blood to heal you. Heavy: drains everything it touches.'),
]

# ------------------------------------------------------------------------------------------------ decisions waiting for Noah
DECISIONS = [
    dict(id='immolate', sys='spells', q='What replaces Immolate (the Destruction fire spell at Tier III)?',
         opts=[('fireStorm', 'Fire Storm', 'Skyrim\'s master fire spell: a ring of flame bursting out round you.'),
               ('meteor', 'Meteor', 'A meteor called down where you aim (the look of Alduin\'s Bane\'s, smaller).'),
               ('flameWall', 'Wall of Flames', 'A burning wall laid along the ground ahead.'),
               ('inferno', 'Inferno Bolt', 'A slow, heavy fire bolt that bursts in a wide blast.')]),
    dict(id='bonereaper', sys='tablet', q='The Bonereaper\'s raise caused the round 20 crash (fixed now). Keep it in the Tablet?',
         opts=[('keep', 'Keep it', 'Fixed; it stays as it is until the lineup review.'),
               ('review', 'Decide in the lineup review', 'Keep, cut or rework it with the rest of the Tablet.'),
               ('cut', 'Cut it now', 'Take it out of the Tablet from the next build.')]),
]


# ------------------------------------------------------------------------------------------------ the design bible
BIBLE = dict(
    motto='Skyrim look, BO3 punch.',
    map=[   # (BO3 Zombies, The Curse of Alduin)
        ('Rounds', 'Nights and waves', 'Each night climbs; every 5th a boss, every 10th a dragon.'),
        ('Points', 'Cursed Essence', 'Earned on every kill, spent at the Echo Altar; +10s rise where the kill falls.'),
        ('Mystery Box', 'Tablet of Mysteries', 'The only gamble: a donated weapon with limited uses; the mask makes it fly to another city.'),
        ('Pack-a-Punch', 'Tier upgrades', 'One model per class weapon; each tier makes its look and ability visibly stronger.'),
        ('Perk-a-Colas', 'Nordic brews', 'Mead, ales and draughts bought at the altar, shown as a row of icons.'),
        ('Wonder Weapons', 'The four wonders', 'Soulcaller\'s Staff, Wyrmspine Javelin, Mutagen Staff, Dragon Slayer: each earned by a hidden quest.'),
        ('Easter eggs', 'Hidden quests and Felldir', 'Felldir the Old whispers the steps; his story opens as cities are freed.'),
        ('Map bosses', 'Dragon Priests and dragons', 'Each city\'s priest; the three word-dragons and the Echo of Alduin.'),
        ('Downed / revive', 'Fall and rise', 'In a party a fallen player can be lifted; alone, the night ends.'),
        ('Power-ups', 'Nordic boons (planned)', 'Short drops from the dead - to be designed.'),
    ],
    rules=[
        ('Reads in a split second', 'Every weapon, ability and enemy is clear by its shape, colour and sound before you think about it - BO3\'s clarity.'),
        ('Made of Skyrim', 'Everything is built from Skyrim\'s own meshes, textures and sounds (and the DLCs\'): nothing flat, nothing that looks 2D or pasted on.'),
        ('One element, one look', 'Fire, frost, shock, shadowfire, holy, poison, arcane and blood each have one kit - colour, textures, sounds - used by every weapon, spell and flask of that element. No purple bolt that burns red.'),
        ('Punch without clutter', 'A hit confirms with a sound and a short burst; no hit pause, no screen shake, no banners.'),
        ('Power you can see', 'Every tier is visibly stronger than the last on the same model; Legendary adds a signature you can spot across the field.'),
        ('Less on screen', 'Only what a BO3 player looks for: the night, the essence, the brews, the compass. Everything else appears when it matters.'),
        ('Thirty of the dead', 'Every effect is budgeted for a crowd of 30 or more; nothing that stutters the first time it plays.'),
        ('Our own, not borrowed', 'Fewer mod requirements over time: our own models from Skyrim\'s meshes, our own effects from Skyrim\'s parts.'),
    ],
    flow=[
        ('Idea', 'A change you ask for, or one I propose and you approve, written as a feature in its system.'),
        ('Building', 'I build it; previews go in the workshop where they help you decide.'),
        ('Test Range', 'Each new effect is played in the Test Range on dummies; you record it with NVIDIA, and the clip comes into the workshop.'),
        ('Ready to test', 'Installed on your PC; it appears in Now under its category with how to test it and what you should see.'),
        ('Passed / Failed', 'You mark it (or pass a whole category at once). A fail goes straight back to Building with your note.'),
        ('Done', 'Passed and unchanged since; it stays in its system so it never comes back as a suggestion.'),
    ],
)

# ------------------------------------------------------------------------------------------------ the element kits (the effects foundation, next)
ELEMENTS = [
    ('fire', 'Fire', '#ff7a2e', 'Skyrim\'s fire spells and Dragonfire: real flames, embers, scorch.'),
    ('frost', 'Frost', '#9fd6ff', 'Ice spikes, frost mist, rime on the dead; shatter bursts.'),
    ('shock', 'Shock', '#b9a8ff', 'Real branching lightning, sparks, a static crackle on the dead.'),
    ('shadowfire', 'Shadowfire', '#9a3bff', 'Violet-black flame that burns violet all the way through (the Shadowflame Scythe).'),
    ('holy', 'Holy and sun', '#ffd56a', 'Dawnguard\'s sunlight: beams from the sky, warm bursts, motes; never flat streaks.'),
    ('poison', 'Poison and acid', '#9be03a', 'Bubbling green, dripping, a lingering cloud low on the ground.'),
    ('arcane', 'Arcane', '#8fb4ff', 'Magicka itself: runes, blue-white light, a soft hum.'),
    ('blood', 'Blood', '#c0182a', 'Vampiric drain, blood mist and spray.'),
]
KIT_PARTS = [
    ('Projectile', 'What flies: a bolt, a ball, a thrown flask.'),
    ('Impact', 'Where it lands: one hit.'),
    ('Burst', 'A big one: a heavy, an explosion.'),
    ('Ground', 'What lingers on the ground for a few seconds.'),
    ('On the dead', 'Burning, frozen, shocked: the shader on the one hit.'),
    ('Weapon aura', 'Tier I, II, III and Legendary on a weapon of that element.'),
    ('Trail', 'Behind a swing or a projectile.'),
    ('Sounds', 'Cast, travel, impact, linger - one set per element.'),
]

# ------------------------------------------------------------------------------------------------ the builds (history)
def builds():
    out = []
    log = subprocess.run(['git', '-C', str(ROOT), 'log', '--since=2026-11-24', '--format=%h|%ad|%s', '--date=format:%Y-%m-%d %H:%M'], capture_output=True, text=True).stdout
    for line in log.splitlines():
        sha, when, subj = line.split('|', 2)
        if 'skip ci' in subj or subj.startswith('Workshop') or subj.startswith('The mod now'):
            continue
        out.append(dict(sha=sha, when=when, what=subj.replace('\n', ' ')[:400]))
    return out

INSTALLS = {   # (each build installed on Noah's PC, and when)
    'f696bbb': '2026-11-26 20:00', '15cfd57': '2026-11-26 17:20', 'a4584e6': '2026-11-26 15:01', '5865977': '2026-11-26 01:30',
}


def main():
    ids = [f['id'] for f in FEATURES]
    assert len(ids) == len(set(ids)), 'duplicate feature ids'
    sys_ids = {s[0] for s in SYSTEMS}
    for f in FEATURES:
        assert f['sys'] in sys_ids, f['id']
        if f['st'] == 'ready':
            assert f['how'] and f['look'], 'a ready feature needs how and look: ' + f['id']
    data = dict(installed=INSTALLED, systems=[dict(id=a, name=b, about=c) for a, b, c in SYSTEMS], features=FEATURES,
                decisions=[dict(d, opts=[dict(id=a, name=b, about=c) for a, b, c in d['opts']]) for d in DECISIONS],
                builds=builds(), installs=INSTALLS, bible=BIBLE,
                elements=[dict(id=a, name=b, col=c, about=d) for a, b, c, d in ELEMENTS], kit=[dict(name=a, about=b) for a, b in KIT_PARTS],
                classes=CLASSES, tablet=TABLET)
    for k in ('modstack', 'nexus', 'modnow'):
        data[k] = json.loads((ROOT / 'docs' / 'workshop' / f'{k}.json').read_text())
    (HERE / 'data.json').write_text(json.dumps(data, ensure_ascii=False, indent=1))
    print('data.json:', len(FEATURES), 'features,', sum(f['st'] == 'ready' for f in FEATURES), 'ready to test,', len(DECISIONS), 'decisions,', len(data['builds']), 'builds')


if __name__ == '__main__':
    main()
