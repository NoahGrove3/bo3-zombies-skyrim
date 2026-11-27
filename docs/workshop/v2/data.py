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

INSTALLED = {'sha': '6c22c73', 'date': '2026-11-27', 'label': 'Your 3edd0b7 test fixed, the 11 new Tablet weapons and the Wheel, Ember Tally HUD and menus, the preview\'s effects in the game',
             'extra': 'Builds 3 and 4 (the Tablet\'s 11 new weapons and the Wheel; the Ember Tally HUD, the altar menu, the Inventory recoloured and the Magic and Tab menus remade) and every note of your 3edd0b7 test: the effects baked from the workshop\'s own previews, the weapon and armour looks\' aura, the moves by tier, the flasks, the main menu and loading screens'}

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

B = '6c22c73'   # (the build installed now: builds 3 and 4 of Nov 27 and the fixes of your 3edd0b7 test)
B0 = '3edd0b7'   # (the one before it)

# ---- movement
F('mv_sprint', 'movement', 'Sprint winds up', 'done', 'The sprint builds over 180 ms to 440 and widens the view 7 degrees.', since='286a000')
F('mv_slide', 'movement', 'Slide from a sprint', 'ready', 'Crouch while sprinting (any time in it, the omni-sprint too): you slide, the view tilts, and a jump out of it keeps the speed. Fixed: Skyrim took the crouch key first, so the sprint had ended and you were sneaking before we heard it - no slide, and the sneak it left stood you up still sneaking. Now how you were a moment before the press is what counts.',
  how='Sprint for two or three seconds, then crouch. Then slide again and jump while sliding. Stand still and crouch once, then stand.',
  look='A slide every time from a sprint; after it you stand up and the sneak eye goes away. Crouching from a stand still sneaks normally.',
  note='Last test (3938d82): "now it wont slide at all and my sneak eye pops up while im standing".', since=B)
F('mv_omni', 'movement', 'Sprint in any direction', 'done', 'Sprint sideways and backwards too, in first and third person (your pick: "Third person too").',
  how='Hold sprint and move sideways and back, in first person, then in third person.', look='Full sprint speed in every direction, both views.', since='3938d82')
F('mv_swing', 'movement', 'Quicker light swings', 'done', 'Light attacks 1.2x as fast; no early swing cancel (your pick: "the speed is enough").', since='286a000')

# ---- combat feel
F('cb_hitpause', 'combat', 'No hit pause', 'done', 'The hit pause is gone everywhere: ours was already 0, Precision\'s own pause is now off in its MCM settings.',
  how='Light and heavy hits on the dead with a sword and a greatsword.', look='No pause and no camera shake on any hit.', note='You asked: "remove the hit pause".', since='3938d82')
F('cb_soul', 'combat', 'No soul torn out', 'done', 'Shadow, spirit and arcane kills no longer tear the soul out (removed as you asked).',
  how='Kill the dead with the Shadowflame Scythe and the Soulcaller.', look='No white beam rising from the bodies.', since='3938d82')
F('cb_ash', 'combat', 'Ash piles gone after 5 s', 'done', 'Fire kills burn to ash; the ash pile and the body are cleared 5 s later.',
  how='Kill the dead with fire (a fire spell, the Fire Flask).', look='They burn to a pile of ash that disappears after about 5 seconds.', since='3938d82')
F('cb_sounds', 'combat', 'Every weapon its own sounds', 'done', 'Draw, swing, heavy and hit sounds per weapon, all at 20%.', since='15cfd57')
F('cb_ragdoll', 'combat', 'Bodies fly half as far', 'done', 'Heavy hits throw bodies at half strength; Alduin\'s Bane as before.', since='76167f7')
F('cb_marks', 'combat', 'No hit markers', 'done', 'No crosshair marks; the night\'s last kill in slow motion.', since='5865977')
F('cb_hitfeel', 'combat', 'Hit feel options', 'done', 'Removed as you asked ("Remove this").')

# ---- class weapons
F('cw_onemodel', 'classes', 'One model per class weapon', 'building', 'Your picks: the Dragon Priest scythe (Alchemist), the Ebony quarterstaff (Summoner), the Daedric wand (Destruction, changed from the Glass wand in your final picks), the Daedric rapier (Spellsword) - in the game; next, our own models from Skyrim\'s meshes.')
F('cw_tierlooks', 'classes', 'Tier looks: one look, stronger each tier', 'building', 'Your picks saved for all four. Now made in 3D on the model itself (its own surface glowing, particles born on it, a volume of light round each part) - never flat or an outline at any angle; the game gets the same from 4K baked textures with a 2K option.')
F('nov27_models', 'classes', 'One model per class weapon, in game', 'ready', 'The Dragon Priest scythe, the Daedric wand (your final pick), the Ebony quarterstaff and the Daedric rapier at every tier; your looks faint at Tier I and stronger each tier. (The wand\'s Tier I in Sorcery, your pick, comes with the looks build.)',
  how='Tester\'s Kit: each class weapon at Tier I, then III, then Legendary.', look='The same model every tier, the wand now the Daedric one; its glow faint at I, full at III, brightest at Legendary.', since=B)
F('nov27_tablet', 'tablet', 'Tablet lineup and rarity', 'ready', 'Only your seven kept weapons roll (the seven cut ones never come up). Rare 60%, Epic 30%, Legendary 10% (Legendary from night 8); the glow blue, purple or gold; a Legendary win rings the bell.',
  how='Roll the Tablet several times (Tester\'s Kit: tablet roll).', look='Stormtalons, Rimewick and Eclipse come up purple, the rest blue; no Bloodmoon, Bonereaper, Riverstone, Silver Scythe, Talons, Wandering Star or Wyrmtooth.', since=B0)
F('nov27_firestorm', 'spells', 'Fire Storm replaces Immolate', 'ready', 'Destruction\'s Tier III fire spell is Skyrim\'s Fire Storm: a ring of flame bursting out round you.',
  how='Altar: Destruction\'s fire ladder at Tier III; cast it among the dead.', look='Fire Storm in the list where Immolate was; flames burst out all round you.', since=B0)
F('nov27_fixes', 'classes', 'Your fixes: javelin, auto-switch, class change', 'ready', 'The javelin is held and put away like a staff (over the shoulder, onto your back; the press only throws). Class weapons are no longer taken off and on when put away (the switching you saw). Changing class costs 20,000.',
  how='Draw and put away the javelin; carry two class weapons and put one away; change class at an altar.', look='The javelin goes onto your back with a matching animation; the weapon you held stays the one you hold; the altar asks 20,000.', since=B0)
F('nov27_small', 'hud', 'Gold class cards, menu music, Slay the Undead', 'ready', 'The class cards\' words in Ember Tally gold (the screen as it was); the main menu plays Skyrim\'s main theme; the first trial is Slay the Undead.',
  how='Start the game; open the class choice at an altar; look at a city\'s trials.', look='Dragonborn at the menu; gold names and words on the class cards; "Slay the Undead" first.', since=B0)
F('ui_coop', 'hud', 'Co-op UI', 'idea', 'The party list, a friend down and lifting them, you down, names over the party, the scoreboard, a shared trial, joins and falls - in the workshop to approve.')
F('ui_fades', 'hud', 'Every box fades in and out', 'idea', 'A soft gradient fade in and out on every pop-up box (your note); drawn by our HUD, so it goes in with the new direction.')
F('ui_scale', 'hud', 'The UI fits any screen', 'idea', 'Sizes follow the screen\'s height, places the corners: 2560x1600, 1920x1080, 2560x1440 and 3440x1440 all previewed. Loading screens fill the screen on any shape.')
F('ui_menus', 'hud', 'Inventory, Magic and Tab menus ours', 'idea', 'Recoloured (Untarnished UI\'s layout in our colours) or remade (our own pages): pick one of each in the workshop.')
F('tb_rarity', 'tablet', 'Rarity: Rare, Epic, Legendary', 'idea', 'Each Tablet weapon is Rare (blue), Epic (purple) or Legendary (gold) by strength; rarer ones roll less often (60/30/10 to start, yours to set). Uses rebalanced once the lineup is final.')
F('tb_roll', 'tablet', 'A new roll for the Tablet', 'idea', 'Four rolls to pick from in the workshop: The Reel, Pillar of Light, The Wheel, Soul Storm - each ending in the rarity\'s colour.')
F('cw_ownmodels', 'classes', 'Our own models from Skyrim\'s meshes', 'idea', 'Each class weapon (and later the Tablet\'s) built from Skyrim, Dawnguard and Dragonborn parts: no mod needed for it.')
F('cw_abilities', 'classes', 'Each class\'s own moves', 'done', 'In the game since 7eb037f: see the four tests below.', since=B)
F('mv_summoner', 'classes', 'Summoner: Radiant Strike, Pillar of Dawn, Ancestor\'s Guard', 'ready', 'Light: each strike of the quarterstaff flares with sunlight that sears the dead and staggers them for a moment. Heavy: a pillar of sunlight falls where you strike. Special: every 20 kills, a ghost of light rises from the 20th body out of a pillar of light and fights beside you for 12 s.',
  how='Summoner, the quarterstaff at any tier: light hits, heavies, then 20 kills.', look='A small sun-flare and burn on each hit; the white pillar on heavies; a see-through warrior rising out of light on the 20th kill.', since=B)
F('mv_destruction', 'classes', 'Destruction: Glintstone Bolt, Arcane Ray, Starfall', 'ready', 'The wand is Destruction\'s own now (no spell needed in the other hand). Tap attack: a glintstone bolt from the tip that bends a little toward the nearest of the dead, at most one every 0.5 s. Hold attack: the Arcane Ray from the tip, wider and hotter the longer you hold it, draining your magicka (it stops when you run out). Every 25th bolt: Starfall, twelve great bolts raining in front of you.',
  how='Destruction, the wand: tap attack quickly several times; then hold it for a few seconds; then tap 25 bolts.', look='Bolts no faster than every half second; the violet beam from the wand\'s tip with your magicka bar dropping; twelve bolts falling ahead on the 25th.', since=B)
F('mv_alchemist', 'classes', 'Alchemist: Reaping Arc, Shadowfire Eruption, Philosopher\'s Edge', 'ready', 'Light: a wide violet sweep through every one of the dead in front of you, setting them smouldering in shadowfire; the burn stacks with each sweep (up to three). Heavy: geysers of shadowfire burst up in a ring round you. Special: every 10th kill, your next kill bursts into Cursed Essence (+150) and shadowfire.',
  how='Alchemist, the scythe: light swings into a crowd, a heavy among them, then 10 kills and one more.', look='A wide sweep hitting several at once; eight violet bursts in a ring on the heavy; a hint on the 10th kill and a burst with an Essence count on the next.', since=B)
F('mv_spellsword', 'classes', 'Spellsword: Spellthrust, Spell Pillar, Elemental Wake', 'ready', 'All follow the spell in your other hand. Light: each thrust that lands carries it - fire burns, frost slows, shock arcs to one more of the dead. Heavy: a pillar of your element pours down on the one ahead. Special: your kills leave a patch of the element on the ground for 4 s that hurts the dead crossing it.',
  how='Spellsword with fire, then frost, then shock in the other hand: thrusts, heavies, kills.', look='Fire burns, frost slows, lightning jumping to a second; a pillar in the element on heavies; element patches where the dead fall.', since=B)
F('cw_ults', 'classes', 'Ultimates and the kill meter removed', 'done', 'The Tier III ultimates and their meter are gone (Tier III keeps its cards as before).',
  how='Tester\'s Kit: a class weapon at Tier III; kill 30 of the dead.', look='No meter above the perks, nothing extra on a heavy.', since='3938d82')
F('cw_card', 'classes', 'Weapon card removed', 'done', 'The held weapon\'s card bottom right is gone.', how='Draw any weapon.', look='Nothing drawn bottom right.', since='3938d82')
F('cw_ascend', 'classes', 'Ascend grows the abilities', 'done', 'Each Ascend +15% damage and +10% size.', since='650d613')
F('cw_scythes', 'classes', 'Scythes solid (not inside out)', 'done', 'All nine scythes: patches of the blades faced inward and the cut poles were open, so you saw their inside and through them. Every piece now faces out; open cuts drawn from both sides.',
  how='Tester\'s Kit: the Shadowflame Scythe at each tier, the four Tablet scythes; look at each from every side, close.',
  look='Solid from every side: no seeing the inside of the texture, no see-through.', note='"the model was inside out, so I can see the inside of the textures, and at some angles I could see through".', since='2384ca5')
F('cw_mutagen1p', 'classes', 'Mutagen Staff in first person', 'done', 'A first-person model of its own, set lower in the hand so your hand no longer passes through its head.',
  how='Alchemist with the Mutagen Staff: first person, idle and swinging.', look='Your hand on the shaft, the head well above it.', note='"my players hand clipping through the head of the staff".', since='3938d82')
F('cw_sheath', 'classes', 'Sheathing the Mutagen Staff', 'building', 'It must stay the Mutagen Staff when drawn and sheathed.', note='Failed last test (no note).')
F('cw_rapiershock', 'classes', 'Spellbound Rapier shock look', 'building', 'Part of the tier-look rebuild.', note='"The shock effect on the spellbound rapier still looks wrong".')
F('cw_radiant', 'classes', 'Radiant Quarterstaff heavy pillar', 'building', 'Part of the abilities rebuild.', note='"looks like a poorly done white line standing vertically".')

# ---- class spells
F('sp_own', 'spells', 'Class spells play their own effects', 'done', 'Each spell plays its mod\'s own impact, nothing of ours over it.', since='9b0de93')
F('sp_immolate', 'spells', 'Fire Storm replaces Immolate', 'building', 'Your pick (Decisions): Skyrim\'s Fire Storm takes Immolate\'s place in the Destruction fire spells, in the next build.', note='"I want to replace the immolate from the spell pool".')

# ---- flasks
F('fl_fire', 'flasks', 'Fire Flask bursts in flames', 'done', 'Dragonfire\'s impact and a ball-of-fire burst: flames, not a flat explosion.',
  how='Throw the Fire Flask at the ground and at a group.', look='A burst of real flames.', note='"The Fire Flask explosion should be flames".', since='3938d82')
F('fl_holy', 'flasks', 'Holy Water leaves no glow behind', 'ready', 'The golden light it casts is cut back and its motes and burst now end with the beam, so no golden hue stays after it.',
  how='Throw Holy Water at Tier III (its sunbeam), then watch the ground after the beam is gone.', look='The beam and burst, then nothing: no golden tint left on the ground or the dead.', note='Last test (3938d82): "there was a lingering hue after the beam disappears".', since=B)
F('fl_rest', 'flasks', 'The other flasks\' bursts', 'idea', 'Frost, Shock, Acid and Voidglass rebuilt on the element kits with the rest of the effects.')

# ---- relics
F('rl_abilities', 'relics', 'Relic abilities rebuilt', 'idea', 'The ten relics\' hits and heavies redone on the element kits.', note='Vahlok\'s chains and the real-parts effects failed your last test.')
F('rl_list', 'relics', 'The ten relics and what they do', 'done', 'Keyhammer, Mistblade, Windcutter, Glacier Greatsword, Storm Blade, Sacrifice Knife, Grief Blade, Sealbreaker, Nightcaller\'s Scythe, Arcane Maul.')

F('cw_flasklook', 'flasks', 'The flasks\' swirling potions', 'ready', 'Your picks for all six: a skin of light over the potion with its swirls turning round and climbing through it, in the flask\'s colour, so the liquid moves in the glass; the element\'s wisp still curls off the cork.',
  how='Hold each flask in your hand and look at it; throw one and watch it fly.', look='The potion swirling inside the glass, in its element\'s colour; a wisp off the cork.', since=B)
F('fl_size', 'flasks', 'Flask bursts half the size, with a ring', 'ready', 'Your pick: the Ring impact at 50%. Each flask\'s burst is half its old size, and a low ring of its element runs out along the ground to the edge of what it catches. The glass shatter and the Legendary bursts are smaller too.',
  how='Throw each flask you have at a crowd, at its tier.', look='A small burst and a ring spreading on the ground, not a screen-filling explosion.', note='"the explosion size for the flasks needs to be much less its a bit overwhelming currently".', since=B)
F('ui_bars', 'hud', 'The bars hide when full', 'ready', 'Your health, magicka and stamina plate keeps its look, and fades out once all three have been full for two seconds (and your shout is ready); it comes back the moment any drops or you shout.',
  how='Stand still with everything full; then take a hit, cast, sprint, shout.', look='The plate fades away when full and returns the moment a bar drops.', since=B)
F('ui_cursor', 'hud', 'The Rune Arrow cursor', 'ready', 'Your pick: a Nordic arrowhead in dark iron with a gold edge and one rune cut in it, glowing - in every menu (it replaces the cursor drawn there before).',
  how='Open the main menu, the inventory, the map and the altar with the mouse.', look='The iron arrowhead with its gold edge and a slowly glowing rune, its point exactly where you click.', since=B)
# ---- tablet
F('tb_lineup', 'tablet', 'The Tablet lineup redone', 'building', 'Your lineup: the seven you kept and the 11 you added (Voice of the Mountain, Soulrender, Chaurus Fang, Mace of the Bound, Giant\'s Toe Club, Sweetroll of the Damned, Lil\' Atronach, Grasp of Apocrypha, Frostbite Egg Sac, Spriggan\'s Heartroot, Dragonguard Ward-Shield), by rarity, rolled with the Wheel.')
F('tb_claws', 'tablet', 'Both claws from the Tester\'s Kit', 'ready', 'The kit gives each claw\'s left-hand twin too, and equipping a claw puts its twin in your other hand.',
  how='Tester\'s Kit: the Tablet weapons; equip Stormtalons and Cinderclaws from the inventory.', look='A claw in each hand every time.', note='"The test kit was only giving me one claw".', since=B0)
F('tb_storm', 'tablet', 'Stormtalons: lightning, not ice', 'ready', 'The heavy hit\'s stun no longer coats the dead in ice: a lightning arc and a shock burst.',
  how='Stormtalons: heavy hits, and three light hits in a row.', look='Lightning on the heavy and on every 3rd hit; no ice anywhere.', since=B0)
F('tb_bonereaper', 'tablet', 'Seven Tablet weapons cut', 'building', 'Your marks: Bloodmoon, Bonereaper, Riverstone, Silver Reaping, Talons of Woe, the Wandering Star and Wyrmtooth no longer roll (since 3938d82); the 11 new ones you added take their place in the next build.')

# ---- wonders
F('wd_javhand', 'wonders', 'Javelin held palm to you', 'ready', 'The hand turned so the palm faces you while charging and throwing; the charge effect over your head removed.',
  how='Destruction wonder: hold to charge, look around, throw.', look='Palm towards you on the shaft; nothing floating above your head.', note='"hand twisted ... palm facing towards the player"; "remove the charge element".', since=B0)
F('wd_bane', 'wonders', 'Alduin\'s Bane: Ash of Helgen', 'ready', 'Your pick for its Light: each hit sets the one struck burning, a patch of embers smoulders under it, and a moment later the fire spreads to those beside it (the meteors are gone). Its Heavy is still the white tornado.',
  how='Light hits on a crowd with Alduin\'s Bane; then a heavy.', look='Fire on the one hit and embers under it, then the ones next to it catch; the heavy\'s white tornado as before.', since=B)
F('wd_javlook', 'wonders', 'The javelin\'s look follows your other hand', 'ready', 'Your pick, Elemental Edge with Dragonfire over it, now in the element of the spell in your other hand: fire orange, frost ice-blue, shock violet-blue.',
  how='Hold the javelin with a fire spell in your other hand, then frost, then shock.', look='Its look changes colour with the spell in your other hand.', since=B)
F('wd_looks', 'wonders', 'The wonders\' looks, each with its second', 'ready', 'Your picks: the Soulcaller Spectral Mist + Soul Fire; the Mutagen Staff Transmutation Runes + Living Mutagen (on its orb); the Dragon Slayer Dragonfire Runes + Slayer\'s Runes; Alduin\'s Bane Words of Power + World-Eater\'s Fire.',
  how='Draw each wonder and look at it.', look='Each in your picked look with the second laid over it.', since=B)
F('cw_layers', 'classes', 'Tier looks: Destruction\'s Tier I, the Legendary layers', 'ready', 'Your tier picks: the Destruction wand in Sorcery at Tier I, then Glintstone Mist; at Legendary (Ascend) the Summoner\'s quarterstaff adds Dawn Halo and the Spellsword\'s rapier adds Spellbound over its look; the Alchemist and Destruction at their brightest.',
  how='Tester\'s Kit: the wand at Tier I then II; the quarterstaff and rapier at Tier III then Ascend.', look='The wand\'s look changes from Tier I to II; the quarterstaff and rapier gain a second look at Ascend.', since=B)
F('wd_soulmana', 'wonders', 'Soulcaller: the beam costs magicka; cats out of the portal', 'ready', 'Its held beam now drains your magicka (it stops when there\'s none left). Each spectral sabre cat comes out of Skyrim\'s purple conjuring portal: the portal opens, and the cat steps out of it.',
  how='Hold the Soulcaller\'s beam on the dead; let your magicka run out; take 20 souls.', look='Your magicka dropping while it runs; the purple portal, then the cat out of it.', note='"the spectral cat should spawn from skyrims purple portals. and the beam should cost mana."', since=B)
F('wd_mutagenstrike', 'wonders', 'Mutagen Staff: Mutating Strike', 'ready', 'Your pick for its staff strike: each strike marks the one struck, and a second later the mark bursts as the next flask\'s element in turn - fire, frost, shock, acid, holy water, voidglass - like the block\'s flasks.',
  how='Strike the dead several times with the Mutagen Staff.', look='A burst a second after each strike, a different element each time, cycling through all six.', note='"got the mutating strike make it cycle through the flask effects like the block does."', since=B)
F('wd_soulcaller', 'wonders', 'Soulcaller look', 'done', 'White soul wisps on its glass head.', since='76167f7')

# ---- summons
F('sm_portal', 'summons', 'Summons come out of the portal', 'building', 'Conjurations and the Soulcaller\'s pride should step out of Skyrim\'s conjuring portal.', note='"Doesn\'t come out of the Skyrim summon portal they just pop up".')
F('sm_thrall', 'summons', 'Raised thralls safe', 'ready', 'A thrall raised from a leveled enemy (the Frost Brute) used a temporary copy of its record that is thrown away with the body: the round 20 crash. Now made from the real record.',
  how='Any raise on many kinds of the dead, rounds 15 to 20 (the Bonereaper no longer rolls from the Tablet).', look='Thralls rise and fight; no crash.', since=B0)

# ---- armour
F('ar_ssbody', 'armour', 'Spellsword set shows your body', 'ready', 'The cuirass now uses a slot of its own, so the body slot stays free and your body shows under it.',
  how='Tester\'s Kit: Spellsword set at each tier; look at yourself in third person.', look='Your body under the cuirass. If the cuirass itself is missing now, tell me.',
  note='"Still makes my body invisible".', since=B0)
F('ar_looks', 'armour', 'Each set\'s own look', 'ready', 'Your picks, once the set is upgraded and worn: the Summoner a Sheen (a rim of light round you, sun-gold), Destruction Embers (ribbons of violet embers rising round you), the Alchemist a Mantle (a soft column of shadowfire light), the Spellsword a Sheen in its element\'s colour. At Legendary each adds its second: the Summoner and the Spellsword the Embers, Destruction and the Alchemist the Sheen. Hidden in first person (the Sheen\'s rim may show on your arms).',
  how='Tester\'s Kit: each class\'s set at Tier I, then III, then Ascend (Legendary); look in third person.', look='Faint at Tier I, stronger each tier; the second look joining at Legendary; nothing blocking your view in first person.', since=B)
F('ar_inv', 'armour', 'Class sets in the inventory', 'done', 'Each piece has a model in the inventory and on the ground.', since='76167f7')

# ---- nights
F('nt_enemies', 'nights', 'Only the dead you kept', 'done', 'Your keep/remove picks of Nov 26.', since='e93f679')
F('nt_n5', 'nights', 'Night 5 mini-boss', 'done', 'One of your kept mini-bosses at random.', since='a4584e6')
F('nt_loot', 'nights', 'No loot on the night\'s dead', 'ready', 'Looking at a night enemy\'s body or ash shows no loot list.',
  how='Look at bodies and ash piles of the night\'s dead; then at a chest.', look='Nothing listed for the dead; chests still list their loot.', since=B0)
F('nt_float', 'nights', 'No floating dead', 'done', 'Any of the dead held in the air is set down.', since='76167f7')

# ---- dragons
F('dr_stuck', 'dragons', 'Dragons don\'t stay stuck flying', 'ready', 'A dragon aloft more than 20 s is made to land for 12 s, then may fly again.',
  how='Tester\'s Kit: night 20 (Grahzah), and a dragon in the wild.', look='Dragons come down to fight within about 20 seconds.', note='"The dragons get stuck flying in the air".', since=B0)
F('dr_half', 'dragons', 'Dragons at half size', 'done', 'Every dragon half size; the bosses\' breath and called dead on the ground.', since='76167f7')

# ---- altars
F('al_redesign', 'altars', 'Trials redesigned', 'idea', 'All four trials redone with their sounds and visuals: the rings you couldn\'t see, the souls, the seals that were only magic visuals.',
  note='"We are going to have to redesign these with their sounds and visuals".')
F('al_rings', 'altars', 'Trial rings visible', 'building', 'Part of the trials redesign.', note='"The ring is still invisible for all the quests".')
F('al_souls', 'altars', 'Lay the Restless to Rest', 'building', 'Part of the trials redesign.', note='"Very visually wrong, the circle is invisible ... the audio effects are overwhelming".')
F('al_seals', 'altars', 'Break the Priest\'s Seals', 'building', 'Part of the trials redesign.', note='"the seals for the last trial are just magic ability visuals".')

# ---- felldir
F('fd_finish', 'felldir', 'Answers play to the end', 'ready', 'A question asked while Felldir is speaking waits until the answer ends, instead of cutting it off.',
  how='At a freed altar, Talk: ask a question, then click another while he answers.', look='The first answer finishes, then the second starts.', note='"The Echo Altar\'s voice sometimes cuts out".', since=B0)
F('fd_four', 'felldir', 'Four questions a topic', 'ready', 'Each topic lists at most four questions, the ones you haven\'t asked first.',
  how='At a freed altar, open each Talk topic.', look='Four questions at most in each list.', note='"way too many dialogue options".', since=B0)
F('fd_quiet', 'felldir', 'The altar quiet during trials', 'done', 'No subtitles until the altar is freed.', since='76167f7')

# ---- hud
F('hd_night', 'hud', 'Bigger night counter and quests', 'ready', 'The night box and the quest list under it are larger.',
  how='Start a night; have a trial and a Skyrim quest tracked.', look='Easier to read at a glance, top left.', note='"Make it larger as well as the quests under it".', since=B0)
F('hd_banner', 'hud', 'Quest banner style', 'building', 'Goes into the In-game UI redesign.', note='"the banner looks off and the size needs to be a bit larger".')
F('hd_impacts2d', 'hud', 'Impact effects in 2D look', 'building', 'Impacts that look flat: part of the element kits rebuild.', note='"impact effects ... look really low quality in 2D".')

# ---- loading
F('ld_continue', 'loading', 'Continue shows our screen', 'ready', 'Our Curse of Alduin loading screen whenever a save loads from the main menu (Continue or Load).',
  how='From the main menu, Continue; then Load a save.', look='Our own screen the whole time, faded off when the world is in.', note='"Loading in a save still shows the loading screens from the mod".', since=B0)
F('ld_doors', 'loading', 'No loading screens for doors', 'done', 'Doors load in about a second: no screen needed (your note).')
F('ld_newchar', 'loading', 'New character flow', 'done', 'One smooth start, menu music from launch.', since='5865977')

# ---- world
F('wo_grass', 'world', 'Floating grass at Riverwood\'s water', 'building', 'One grass piece floating over the water at Riverwood.', note='"one grass piece that was floating over the water at riverwood other than that it\'s good".')
F('wo_world', 'world', 'One grass, Screen-Space GI', 'done', 'Folkvangr and the pine grass; Screen-Space GI\'s light bounce.', since='15cfd57')
F('wo_mods', 'world', 'The 20 mods off', 'done', 'No missing masters; the world as before.', since='a4584e6')

# ---- stability
F('st_r20', 'stability', 'Round 20 crash', 'ready', 'The game fell 0.6 s after a Bonereaper kill raised a Frost Brute. The raise is fixed; Crash Logger is installed to catch anything else.',
  how='Play to round 20, heavy attacks on big crowds.', look='No crash. If one comes, Crash Logger writes a log I can read.', note='"I crashed on round 20 when I did a heavy attack on a crowd using one of the tablet scythes".', since=B0)



# ---- (Nov 27, after your 3edd0b7 test) each mark folded in: a pass done, a fail fixed and back to test with your note
def U(i, **kw):
    for f in FEATURES:
        if f['id'] == i: f.update(kw); return
    raise SystemExit(f'no feature {i}')
T3 = '3edd0b7'
for i in ('nt_loot', 'tb_storm', 'wd_mutagenstrike'): U(i, st='done', since=T3)
U('mv_alchemist', st='ready', what='Reaping Arc (Tier I): the sweep itself catches every one of the dead in front of you - your Shadowfire impact on each, the burn stacking (as the preview: no ball). Shadowfire Eruption (Tier II): the preview\'s eruption, its geysers the reach of the blow. Philosopher\'s Edge (Tier III): every 10th kill, the next bursts into Essence and shadowfire.',
  how='Tester\'s Kit: the scythe at base (plain), Tier I (light), II (heavy), III (10 kills and one more).', look='Nothing extra at base; the sweep from I, the eruption from II, the burst from III.',
  note='Last test (3edd0b7): "it all works from base tier when it should be unlocked as it\'s fully upgraded"; "Light attack from base is shooting the purple fire ball but it explodes like 3m in front".', since=B)
U('mv_destruction', st='ready', what='The tap is the Glintstone Bolt from base (your pick); the Arcane Ray from Tier II (hold attack), each start written to the log; Starfall from Tier III (every 25th bolt, the preview\'s arcane impact where each lands).',
  how='Wand at Tier II: hold attack 2-3 s. Tier III: tap 25 bolts.', look='The violet beam from the tip, magicka dropping; twelve bolts falling ahead.', note='Last test (3edd0b7): "Arcane ray doesn\'t work"; "doesn\'t cast the beam when holding attack".', since=B)
U('mv_summoner', st='ready', what='Radiant Strike (Tier I): the preview\'s holy impact on each strike. Pillar of Dawn (Tier II): the preview\'s holy pillar on the one ahead (the old sunbeam gone). Ancestor\'s Guard (Tier III): every 20 kills.',
  how='Quarterstaff at Tier I, II, III: light hits, heavies, 20 kills.', look='The same golden impact and pillar as the workshop\'s preview.', note='Last test (3edd0b7): "I want the preview effects in this workshop to be exactly how it shows in game if not better"; "still using the old ... attacks".', since=B)
U('mv_spellsword', st='ready', what='Spellthrust (Tier I) with the preview\'s impact in your element; Spell Pillar (Tier II): the preview\'s pillar, one beam; Elemental Wake (Tier III).',
  how='Rapier at Tier I, II, III with fire, frost, shock in the other hand.', look='One pillar in the element, as the preview.', note='Last test (3edd0b7): "The heavy attacks ... look very poor in game"; "casts the heavy attack at base tier"; "casting 2 beams instead of one".', since=B)
U('mv_slide', st='ready', note='Last test (3edd0b7): "Nothing changed". Now also started by the crouch itself (sneaking a moment after a sprint), and every try written to the log.', since=B)
U('nov27_models', st='ready', what='Every class weapon in your picked model at every tier, its look now with the preview\'s aura (a skin of light on the steel, motes rising, a glow); the wand\'s tip light at its tip at base too; your summons ignore your hits; the cards list each move as it unlocks.',
  how='Tester\'s Kit: each class weapon at base, I, III, Legendary; hit one of your summons.', look='The aura on the weapon from every side; the summon never turns on you.', note='Last test (3edd0b7): see History - the looks, the summons, the wand, the moves, the descriptions.', since=B)
U('cw_layers', st='ready', note='Last test (3edd0b7): "it\'s the old preview selections ... the low quality 2D effects". The aura the preview draws is now in the game on every look.', since=B)
U('cw_tierlooks', st='done', what='Your picks in the game with the preview\'s 3D aura: the skin of light on the model\'s own triangles, motes born on it, a glow round each part.', how='Each class weapon at I, III and Legendary.', look='Glowing from every side, never a flat card.', since=B)
U('nov27_firestorm', st='ready', t='Fire Storm replaces Inferno', what='Your correction: Immolate stays Destruction\'s Tier III fire spell; Fire Storm takes Inferno\'s place (Walls, Tier III).',
  how='Altar: the fire ladder at Tier III, then the Walls ladder at Tier III.', look='Immolate in Fire; Fire Storm in Walls; no Inferno.', note='Last test (3edd0b7): "the spell I actually want to remove is inferno".', since=B)
U('sp_immolate', st='done', t='Immolate kept', what='Immolate stays (your correction).', since=B)
U('nov27_fixes', st='done', since=T3)
U('wd_javhand', st='ready', t='Javelin held overhand', what='While charging, the palm turns toward your head - the overhand grip of a javelin throw.', how='Hold to charge the javelin, look around, throw.', look='Overhand grip, palm toward your head.', note='Last test (3edd0b7): "My palm is facing the sky now".', since=B)
U('wd_javlook', st='ready', note='Last test (3edd0b7): "It stays the same looking like a white flaming effect". The aura in the element\'s colour is new.', since=B)
U('wd_looks', st='ready', note='Last test (3edd0b7): Soulcaller flat from some angles; the Mutagen Staff\'s look below its orb. Now the aura on each, and the Mutagen look on the orb in first person.', since=B)
U('wd_bane', st='ready', t='Alduin\'s Bane as the preview', what='Your decision: the Heavy is the Wonders page\'s white tornado - a pillar of white wind 3 m before you, the dead near it hurled away, the rest staggering. The Light is Ash of Helgen as the preview plays it: the fire pulled into the one struck until it pops, the embers\' mist rolling over the ground under it, the fire spreading to the ones beside it.',
  how='Light hits on a crowd with Alduin\'s Bane; then a heavy.', look='The preview\'s pull-in pop and ember mist; the white tornado pillar ahead of you.', note='Your decision note: "Doesn\'t cast the tornado looking pillar from the preview and the light attack just does a fire explosion".', since=B)
F('ld_menuwords', 'hud', 'Our words on the main menu', 'ready', 'Your pick: the menu\'s words drawn by us, centred under the title in Ember Tally - the chosen one ember orange and glowing. Edge UI\'s list is hidden but still works for keys and pad.',
  how='Launch: move through the words with the mouse, the keys and the pad; press Continue, New, Load, Co-op Party, Quit.', look='The words under the title; each does what Edge\'s did. If Edge\'s words show instead, the log says why.', since=B)
U('wd_soulmana', st='ready', note='Last test (3edd0b7): "No mana usage no purple spawn portal ... the cats blue holographic effect is only visible from certain angles". Fixed: the cast read in every state, the portal kept 2.5 s, the cat\'s body glowing blue.', since=B)
U('cw_flasklook', st='ready', what='The potion\'s swirl seen from every side (the glass no longer hides it) and slow enough to read as moving; the neck 1.5x long (your pick).', note='Last test (3edd0b7): "the swirling animation is broken".', since=B)
U('fl_holy', st='ready', t='Flask bursts as the preview', what='Every flask\'s burst is the preview\'s Ring (your pick), baked, its ring reaching exactly as far as the flask hurts, facing you from every side, played once; the Legendary rings too. No gold flickers, no line on the floor, no white flames.',
  how='Throw each flask at a crowd; look from the side and from above.', look='The same ring as the workshop, the size of what it catches.', note='Last test (3edd0b7): see History.', since=B)
U('fl_size', st='done', t='Flask bursts half the size (folded into "Flask bursts as the preview")', since=B)
U('ar_looks', st='ready', what='The Embers are motes rising all round you, from every side; the Sheen a flowing glow brightest at its edges. The Mantle as you approved it.', note='Last test (3edd0b7): "They don\'t look in game how they look in the preview".', since=B)
U('ar_ssbody', st='ready', t='Spellsword legs: Black Mage trousers and greaves', what='Your picks: the Black Mage robe\'s trousers and greaves (cut B) in the Draugr boots\' place, fitted to your body in the set\'s tier looks; the cuirass\'s back cut off at the belt, so nothing hangs through your legs. Built from the Black Mage Armor in your install (another author\'s model: on your PC only, never in the mod\'s files).',
  how='Tester\'s Kit: the Spellsword set at Tier I, III and Ascend; walk, run and sprint in third person; look from behind.', look='Dark trousers and greaves, no skin showing, nothing through the legs; the inventory still shows the Draugr boots\' model.', note='Last test (3edd0b7): "the back is clipping through my legs and I don\'t like that my legs are visible".', since=B)
U('nov27_tablet', st='ready', t='The Wheel', what='The Tablet turns six of its weapons round on a wheel of runes and eases the prize to the front; its rarity\'s colour.', how='Roll the Tablet.', look='The Wheel, not the old roll.', note='Last test (3edd0b7): "It\'s still using the old roll animation" (the Wheel came in build 3).', since=B)
U('tb_claws', st='done', since=T3)
U('ld_continue', st='ready', what='The Ember Tally main menu (the Alduin painting, embers rising, the title); our loading screen for every save you load - from the menu, the journal, or after you fall - reading LOADING SAVE; Continue never taken for a new game.',
  how='Launch; Continue; then load from the journal.', look='The painting with embers; our screen with LOADING SAVE each time.', note='Last test (3edd0b7): see History.', since=B)
U('tb_lineup', st='ready', how='Roll the Tablet many times; Tester\'s Kit: the Tablet weapons.', look='The 11 new weapons and your seven, by rarity; each with its uses ring.', since=B)
U('tb_bonereaper', st='done', since=B)
U('tb_rarity', st='ready', what='Rare 100 uses, Epic 60, Legendary 30 (your pick); blue, purple, gold.', how='Roll several Tablet weapons and look at their uses ring.', look='100 / 60 / 30 uses by rarity, in blue, purple or gold.', since=B)
U('tb_roll', st='done', t='The Wheel (tested as "The Wheel" above)', what='Your pick: the Wheel.', since=B)
U('ui_menus', st='ready', what='The Inventory recoloured (Untarnished UI\'s layout in our colours - your latest pick); the Magic menu remade (our categories and list); the Tab menu\'s four medallions.', how='Open the inventory, the magic menu, Tab.', look='Warm Ember Tally colours on the inventory; our list on magic.', since=B)
U('hd_night', st='ready', t='Ember Tally HUD', what='The night\'s ember tally, quests under it (a trial over its altar), the compass, Essence with kill pops, brews, the boss bar, the hold prompt, the countdown, the Tablet uses ring, the priest\'s card, falling.', how='Play a night.', look='As the workshop\'s In-game UI.', since=B)
U('hd_banner', st='ready', t='Banners and lore cards', what='Ember Tally banners and lore fading in and out.', how='Free a trial step; read a lore card.', look='Ember Tally words fading in and out.', since=B)
U('hd_impacts2d', st='done', t='Impacts from the previews (tested with each class\'s moves)', what='The class moves\' impacts and pillars, the flasks and the Bane\'s fire are the workshop\'s own effects baked for the game.', since=B)
F('st_crash27', 'stability', 'Random crash (Nov 27, 03:53)', 'ready', 'The dead heard one of our burst effects and the game fell reading its sound: every burst of ours is silent to the dead now.',
  how='Play nights as usual.', look='No crash.', note='"my game just randomly crashed".', since=B)
F('fd_runes', 'felldir', 'Trial riddles carved on the altar', 'ready', 'Your pick: before an altar is freed, Felldir is silent - the Whisper trial\'s riddle is carved on the stone (quest line, journal, Codex), never his speech.',
  how='Take a city\'s Stone\'s Whisper trial.', look='"Carved on the altar: ..." - no quotes, no voice.', note='"the echo alter trials show Feldirs voice lines and hints ... but his voice is turned off until the echo alter is freed".', since=B)
F('tb_new11', 'tablet', 'The 11 new Tablet weapons', 'ready', 'Voice of the Mountain, Soulrender, Chaurus Fang, Mace of the Bound, Giant\'s Toe Club, Spriggan\'s Heartroot, Grasp of Apocrypha, Dragonguard Ward-Shield (left hand, your right kept), Sweetroll of the Damned, Lil\' Atronach and the Frostbite Egg Sac (thrown).',
  how='Tester\'s Kit: the Tablet weapons; try each on the dead.', look='Each its own power as in the Tablet tab.', since=B)
F('mv_tiers', 'classes', 'Moves unlock by tier', 'ready', 'Your pick: Light at Tier I, Heavy at Tier II, Special at Tier III (the wand\'s tap always its Glintstone Bolt).',
  how='Each class weapon at base, I, II, III.', look='Base plain; one more move each tier.', since=B)
F('sm_ignore', 'summons', 'Your summons ignore your hits', 'ready', 'Your pick: your blows and spells pass without hurting them, and they never turn on you.', how='Hit your own summon a few times.', look='It keeps fighting the dead.', note='"my summons will fight me back if I hit them and I can\'t kill them".', since=B)
F('tb_mend', 'tablet', 'The Tablet says when it mends', 'ready', 'Holding a worn Tablet weapon, the hold mends it: the prompt says "Hold to mend the <weapon>", and a hint shows it.', how='Hold a used Tablet weapon at the Tablet.', look='The mend prompt; to roll, put the weapon away.', note='"sometimes when I roll the tablet it takes my essence then doesn\'t roll".', since=B)
F('cb_jump', 'combat', 'The dead jumping when struck', 'building', 'A probe: any of the dead that moves more than 45 units right after your hit is written to the log, with why (ragdoll, stagger).', note='"when hitting enemies with the frostmourn rapier they twitch or teleport for a split second ... this needs to be fixed for all weapons".')

# ------------------------------------------------------------------------------------------------ the class weapons: one model each (his pick)
CLASSES = [
    dict(id='alchemist', lk='alch', name='Alchemist', weapon='Shadowflame Scythe', now='Forsworn at Tier I, Falmer II, Dwarven III, Dragon Priest Legendary',
         cands=[dict(key='cls_forsworn_alch', name='Forsworn scythe', src='Built from Skyrim\'s Forsworn axe and sword (ours)'),
                dict(key='cls_falmer_alch', name='Falmer scythe', src='Built from Skyrim\'s Falmer axe and longsword (ours)'),
                dict(key='cls_dwarven_alch', name='Dwarven scythe', src='Built from Skyrim\'s Dwarven battleaxe and greatsword (ours)'),
                dict(key='cls_dragonpriest_alch', name='Dragon Priest scythe', src='Built from a Dragon Priest staff and dagger (ours)')]),
    dict(id='summoner', lk='radiant', name='Summoner', weapon='Radiant Quarterstaff', now='Steel, Ebony, Daedric, Dragonbone by tier (Animated Armoury)',
         cands=[dict(key='cls_steelstaff_radiant', name='Steel quarterstaff', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_ebonystaff_radiant', name='Ebony quarterstaff', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_daedricstaff_radiant', name='Daedric quarterstaff', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_dragonbonestaff_radiant', name='Dragonbone quarterstaff', src='Animated Armoury - to be rebuilt from Skyrim\'s meshes')]),
    dict(id='destruction', lk='carian', name='Destruction', weapon='Carian Wand', now='Steel, Stalhrim, Glass, Daedric by tier (Fulcimentum)',
         cands=[dict(key='cls_steelwand_carian', name='Steel wand', src='Fulcimentum - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_stahlrimwand_carian', name='Stalhrim wand', src='Fulcimentum - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_glasswand_carian', name='Glass wand', src='Fulcimentum - to be rebuilt from Skyrim\'s meshes'),
                dict(key='cls_daedricwand_carian', name='Daedric wand', src='Fulcimentum - to be rebuilt from Skyrim\'s meshes')]),
    dict(id='spellsword', lk='ss_none', name='Spellsword', weapon='Spellbound Rapier', now='Steel, Ebony, Daedric, Dragonbone by tier (Animated Armoury)',
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


# ------------------------------------------------------------------------------------------------ new Tablet weapons to choose from (Nov 26 night)
# (Noah: "give me new weapon ideas in the tablet lineup ... that really depict Skyrim and Black Ops 3 zombies" - 12 to choose from)
# each: built from Skyrim's own meshes, an element from the kits, and a nod to a BO3 Zombies wonder weapon or box item
TABLET_NEW = [
    dict(id='fushammer', name='Voice of the Mountain', kind='Nordic warhammer', el='wind', base='Skyrim\'s Nordic warhammer with a Word Wall\'s carved dragon-script round its head',
         light='Every hit staggers the dead back a step.', heavy='FUS: a short roaring cone (5 m) that knocks down the dead in front of you.',
         special='Every 20 kills it charges a full FUS RO DAH: the next heavy hurls every one of the dead 12 m ahead off their feet - always stronger than a heavy (your note: light < heavy < special).', bo3='The Thundergun'),
    dict(id='jester', name='The Jester\'s Cane', kind='Daedric staff', el='arcane', base='The Wabbajack\'s shape, reshaped as a twisted cane with a grinning head',
         light='Each bolt rolls a random fate: the dead shrink to knee height, turn into a cheese wheel that rolls off and bursts, fight each other, or pop in confetti fire.',
         heavy='A wide burst that gives every one of the dead around you its own random fate.', special='Sheogorath laughs now and then.', bo3='The Wave Gun and the Shrink Ray'),
    dict(id='soulrender', name='Soulrender', kind='Daedric mace', el='spirit', base='A Daedric mace whose head is a cracked black soul gem',
         light='Every kill traps a soul: the gem fills and glows brighter.', heavy='With a full gem: a soul storm - the trapped souls rise as spectral warriors and fight beside you for 12 s.',
         special='The gem hums when it is full.', bo3='The Wrath of the Ancients (its charged shots)'),
    dict(id='chaurus', name='Chaurus Fang', kind='Falmer sword', el='poison', base='A Falmer sword edged with chaurus chitin and dripping venom',
         light='Hits poison the dead.', heavy='A spray of venom in a cone.', special='The poisoned dead burst into acid when they die, poisoning the ones beside them - a chain through a crowd.', bo3='The Apothicon Servant\'s chain kills'),
    dict(id='piston', name='Dwemer Piston Lance', kind='Dwarven spear', el='earth', base='A Dwarven spear on a Centurion\'s piston, steam venting from its joints',
         light='Thrusts with a hiss of steam that staggers.', heavy='The piston fires: a spike punches through a line of the dead and pins them to the ground for 4 s.',
         special='Steam bursts from it on every heavy.', bo3='The Ragnarok DG-4 (pinning the horde)'),
    dict(id='bound', name='Mace of the Bound', kind='Daedric mace', el='shadowfire', base='The Mace of Molag Bal\'s spiked head on a Daedric haft',
         light='Hits brand the dead with a violet mark.', heavy='A slam that brands every one of the dead within 6 m.', special='A branded one that dies bursts into violet shadowfire, burning the dead around it.', bo3='The Kraken / the Thundergun\'s mass kills'),
    dict(id='aetherium', name='Aetherium Disc Blade', kind='Dwemer glaive', el='arcane', base='Dwemer metal round a glowing Aetherium core, a round blade',
         light='A spinning slash.', heavy='Thrown: the blade hovers ahead of you and spins through the dead for 5 s before flying back.', special='Its core lights the dead it cuts in blue.', bo3='The Apothicon Sword\'s spinning attack'),
    dict(id='bloodbite', name='Harkon\'s Bloodbite', kind='Vampire dagger', el='blood', base='A vampire\'s curved dagger with a blood-red gem',
         light='Fast stabs that drain a little health.', heavy='A cloud of bats bursts from you and tears at the dead around you for 4 s.', special='Kills leave blood orbs that heal you when you walk through them.', bo3='The Vril device\'s life drain'),
    dict(id='whisper', name='Whisper Edge', kind='Ebony sword', el='shadowfire', base='An Ebony sword wrapped in shadow, like the Ebony Blade',
         light='Kills make you unseen for 2 s: the dead lose you.', heavy='Shadow step: you blink 8 m forward through the dead, cutting every one you pass.', special='The blade whispers when it kills.', bo3='Who\'s Who and the Apothicon Sword\'s lunge'),
    dict(id='giantclub', name='Giant\'s Toe Club', kind='Giant\'s club', el='wind', base='A giant\'s club, a mammoth toe bound to its head',
         light='Every hit sends one of the dead flying into the sky.', heavy='A ground stomp that knocks down every one of the dead within 8 m.', special='Yes - the famous giant launch.', bo3='The Ragnarok DG-4\'s slam'),
    dict(id='sweetroll', name='Sweetroll of the Damned', kind='Throwable (5 throws)', el='fire', base='A sweetroll with a lit fuse in it',
         light='Throw it: it sings a little Nord tune, and every one of the dead nearby runs to it.', heavy='-', special='After 6 s it explodes in a fireball. Someone always steals it first.', bo3='The Monkey Bomb'),
    dict(id='atronach', name='Lil\' Atronach', kind='Throwable (3 throws)', el='fire', base='A soul gem holding a tiny Flame Atronach',
         light='Throw it: a knee-high Flame Atronach pops out and spins, blasting the dead around it with fire for 10 s.', heavy='-', special='It dances when it\'s done.', bo3='Li\'l Arnie'),
]

# (Nov 26 late: "add the tablet suggested and give me more new ones") - 12 more, none like the five you said no to
TABLET_NEW2 = [
    dict(id='dagon', name='Razor of Dagon', kind='Daedric dagger', el='shadowfire', base='A Daedric dagger, its blade a sliver of Mehrunes Dagon\'s Razor',
         light='Quick cuts; 1 in 8 hits slays one of the ordinary dead outright.', heavy='A rending slash that cuts every one of the dead in a 6 m line.', special='Slain ones split apart in violet fire.', bo3='Insta-Kill'),
    dict(id='apocrypha', name='Grasp of Apocrypha', kind='Black Book (held)', el='poison', base='A Black Book of Hermaeus Mora, open in your hand',
         light='A tentacle lashes up from the ground at the one you aim at.', heavy='A ring of tentacles bursts round you and drags the dead within 6 m under for 5 s.', special='The book whispers in Hermaeus Mora\'s voice.', bo3='Revelations\' Apothicon tentacles'),
    dict(id='azura', name='Azura\'s Fallen Star', kind='Throwable (3 throws)', el='spirit', base='A shard of Azura\'s Star, dusk-light inside',
         light='Throw it: it hangs in the air and pulls every one of the dead near it into a spinning knot, then bursts.', heavy='-', special='The ones it takes leave their souls as motes that drift to you.', bo3='The Gersh Device'),
    dict(id='skyforge', name='Skyforge Thunder Maul', kind='Skyforge warhammer', el='shock', base='A Skyforge steel warhammer, its head ringed with copper and storm-glass',
         light='Hits arc to 2 more of the dead.', heavy='Calls lightning down on up to 5 of the dead around you.', special='Thunder rolls over the city when it strikes.', bo3='The Storm Bow (Kimat\'s Bite)'),
    dict(id='spriggan', name='Spriggan\'s Heartroot', kind='Living wood mace', el='poison', base='A gnarled mace of spriggan heartwood, its knot glowing green',
         light='Hits heal you a little.', heavy='Roots burst from the ground and hold every one of the dead within 6 m for 4 s.', special='Swarms of glowing insects circle it.', bo3='Zetsubou\'s KT-4 and its plants'),
    dict(id='eggsac', name='Frostbite Egg Sac', kind='Throwable (4 throws)', el='poison', base='A frostbite spider\'s egg sac, webbing trailing from it',
         light='Throw it: it bursts into webs that stick the dead in place for 6 s while spiderlings bite them.', heavy='-', special='Webbed ones take double damage.', bo3='Widow\'s Wine'),
    dict(id='tusk', name='Tusk of the Herd', kind='Mammoth-tusk spear', el='wind', base='A spear with a mammoth\'s tusk for a head, bound in hide',
         light='Thrusts knock the dead down.', heavy='A stampede of ghostly mammoths tramples the street ahead.', special='The ground shakes as they pass.', bo3='The Thundergun\'s sweep, made Skyrim'),
    dict(id='hagraven', name='Hagraven\'s Fetish', kind='Throwable (3 throws)', el='shadowfire', base='A hagraven\'s feathered fetish of bones and twine',
         light='Throw it: it curses 3 of the dead - they turn and fight for you for 10 s.', heavy='-', special='They screech like hagravens while turned.', bo3='The GobbleGum "Turned"'),
    dict(id='soulcairn', name='Soul Cairn Lantern', kind='Lantern flail', el='spirit', base='A Soul Cairn lantern on a chain, a trapped soul burning inside',
         light='Hits draw a thread of soul light from the dead.', heavy='Opens a rift to the Soul Cairn: spirits drag the dead near it down.', special='The lantern brightens with every soul.', bo3='The Apothicon portals'),
    dict(id='stalhrim', name='Stalhrim Shatter Axe', kind='Stalhrim war axe', el='frost', base='A Stalhrim war axe, deathly cold to hold',
         light='Hits freeze the dead; a frozen one hit again shatters and freezes the ones beside it.', heavy='A cleave that freezes a cone in front of you.', special='Frost creeps over your gauntlet.', bo3='The Staff of Ice (Ull\'s Arrow)'),
    dict(id='daedraheart', name='Daedra Heart Flail', kind='Flail', el='fire', base='A flail with a burning daedra heart for a head',
         light='Hits burn.', heavy='Slams the heart down: a crack of magma runs ahead, burning a line of the dead.', special='The heart still beats.', bo3='The Staff of Fire (Kagutsuchi\'s Blood)'),
    dict(id='wardshield', name='Dragonguard Ward-Shield', kind='Shield (left hand)', el='holy', base='A Blades\' dragonguard shield with a ward glowing in its face',
         light='Hold it up: the dead can\'t hit you from the front.', heavy='A shield charge that flings the dead in your path.', special='It breaks after enough hits - the Tablet gives a fresh one.', bo3='The Rocket Shield'),
]

# rarity (Nov 26 late: "3 rarity weapons based on how strong their abilities are: Rare Blue, Purple Epic, Gold Legendary" -
# rarer rolls less often, his pick). These are my suggestion; he moves any of them in the workshop.
RARITY_DEFAULT = {
    'stormtalons': 'epic', 'cinderclaws': 'rare', 'frostmourn': 'rare', 'lanternwick': 'epic', 'wyrmspine': 'rare', 'eclipse': 'epic',
    'fushammer': 'legendary', 'soulrender': 'legendary', 'chaurus': 'rare', 'bound': 'epic', 'giantclub': 'rare', 'sweetroll': 'epic', 'atronach': 'epic',
    'dagon': 'legendary', 'apocrypha': 'legendary', 'azura': 'epic', 'skyforge': 'epic', 'spriggan': 'rare', 'eggsac': 'rare', 'tusk': 'legendary',
    'hagraven': 'epic', 'soulcairn': 'epic', 'stalhrim': 'rare', 'daedraheart': 'epic', 'wardshield': 'rare',
}
ODDS_DEFAULT = {'rare': 60, 'epic': 30, 'legendary': 10}
ROLLS = [
    dict(id='reel', name='The Reel', about='The design we have now, remade: the glyph ring grinds round, ghostly weapons flicker in the centre like a reel slowing down, and the won one rises out of the face in its rarity\'s light.'),
    dict(id='pillar', name='Pillar of Light', about='BO3\'s box beam: a column of the rarity\'s light shoots into the sky from the Tablet; the weapons flick up inside it, faster then slower, and the won one hangs in the beam.'),
    dict(id='wheel', name='The Wheel', about='The weapons circle the Tablet on a ring of runes, spinning fast and easing to a stop with the won one in front; it floats in to you.'),
    dict(id='storm', name='Soul Storm', about='A vortex of souls rises off the Tablet with the weapons tumbling in it; the storm collapses into the won one with a burst of its rarity\'s colour.'),
]

# ------------------------------------------------------------------------------------------------ decisions waiting for Noah
DECISIONS = []   # (none waiting: the three of Nov 27 afternoon are below in DECIDED)



# ------------------------------------------------------------------------------------------------ decisions you've made (Nov 27: "save the older ones I make"):
# the question, your answer, where you gave it, and what was done with it - kept here for good, newest first. A pick you make in the
# workshop moves here at the next build.
DECIDED = [
    dict(when='2026-11-27', sys='wonders', q='Alduin\'s Bane\'s heavy, the preview\'s white tornado pillar: where should it land, and who gets flung?', pick='In front, like the preview', where='chat', done='The pillar 3 m before you; the dead near it hurled away, the rest stagger.'),
    dict(when='2026-11-27', sys='armour', q='The Draugr cuirass\'s back flap that clips through your legs: what should happen to it?', pick='Cut it off', where='chat', done='The cuirass ends at the belt behind; the trousers show there.'),
    dict(when='2026-11-27', sys='armour', q='Which cut of the Black Mage legs should the Spellsword set wear?', pick='B: Trousers + greaves', where='chat', done='The trousers and greaves in the Draugr boots\' place, in the set\'s tier looks.'),
    dict(when='2026-11-27', sys='wonders', q='Your note on Alduin\'s Bane: "It\'s still the old version". What feels old?', pick='The heavy\'s white tornado - "Doesn\'t cast the tornado looking pillar from the preview and the light attack just does a fire explosion"', where='workshop', done='Both now as the Wonders page plays them: the white tornado pillar for the Heavy, Ash of Helgen (the fire pulled in, the embers\' mist) for the Light.'),
    dict(when='2026-11-27', sys='hud', q='The main menu\'s words: ours, centred under the title in Ember Tally, or Edge UI\'s?', pick='Our centred words', where='workshop', done='Drawn by us under the title; Edge\'s list hidden but still working for keys and pad.'),
    dict(when='2026-11-27', sys='armour', q='Which full-length legs should the Spellsword set wear?', pick='Black Mage legs', where='workshop', done='The Black Mage robe\'s trousers and greaves, fitted to your body (cut picked in chat).'),
    dict(when='2026-11-27', sys='armour', q='The Spellsword pants clip at the back and show your legs. What should replace them?', pick='Show me options', where='chat', done='The options are in Decisions waiting for you.'),
    dict(when='2026-11-27', sys='flasks', q='How much longer should the flask\'s spout be?', pick='1.5x as long', where='chat', done='Held and thrown flasks, the neck from the shoulder to the lip half again as long.'),
    dict(when='2026-11-27', sys='felldir', q='Felldir is silent until an altar is freed, yet the Whisper trial showed his riddle as speech. How to close the gap?', pick='Runes on the altar', where='chat', done='Before an altar is freed its riddle is carved on the stone: the quest line, the journal, the Codex, the trial\'s words.'),
    dict(when='2026-11-27', sys='summons', q='What should happen when you hit your own summon?', pick='Ignore your hits', where='chat', done='Your blows and spells pass harmlessly and they never turn on you.'),
    dict(when='2026-11-27', sys='classes', q='Reaping Arc threw a ball that burst 3 m ahead; the preview has no ball. Which?', pick='Match the preview', where='chat', done='A sweep in front of you; each of the dead caught gets your Shadowfire impact.'),
    dict(when='2026-11-27', sys='classes', q='What does the Carian wand\'s tap do before its Light unlocks?', pick='Glintstone Bolt from base', where='chat', done='The tap is always the Glintstone Bolt; the Ray waits for Tier II, Starfall for Tier III.'),
    dict(when='2026-11-27', sys='classes', q='At which tiers do each class\'s Light, Heavy and Special unlock?', pick='Light I, Heavy II, Special III', where='chat', done='In the game; the weapon cards list each move as it unlocks.'),
    dict(when='2026-11-27', sys='hud', q='The co-op screens: now or later?', pick='Later, with co-op testing', where='chat', done='Kept for the co-op test (In-game UI keeps them).'),
    dict(when='2026-11-27', sys='hud', q='The HUD, the altar menu and the menus: in one build or in parts?', pick='All in one build', where='chat', done='Build 4 (installed with build 3, a2ffc6b).'),
    dict(when='2026-11-27', sys='tablet', q='Soulrender: how many kills, how many warriors?', pick='10 kills, 3 warriors', where='chat', done='In build 3.'),
    dict(when='2026-11-27', sys='tablet', q='Grasp of Apocrypha: its power when?', pick='Every cast, every 5th ring', where='chat', done='In build 3.'),
    dict(when='2026-11-27', sys='tablet', q='The Dragonguard Ward-Shield: what happens to your right hand?', pick='Keep my right-hand weapon', where='chat', done='The shield goes in your left hand only (build 3).'),
    dict(when='2026-11-27', sys='tablet', q='How many uses does a Tablet weapon have?', pick='By rarity: 100/60/30', where='chat', done='Rare 100, Epic 60, Legendary 30 (build 3).'),
    dict(when='2026-11-27', sys='tablet', q='The Bonereaper\'s raise caused the round 20 crash (fixed). Keep it in the Tablet?', pick='Cut it now', where='workshop', done='Cut with the six others in the lineup (since 3938d82).'),
    dict(when='2026-11-27', sys='loading', q='Which music for the launch screen and main menu?', pick='Dragonborn (the main theme)', where='workshop', done='In the game since 3938d82.'),
    dict(when='2026-11-27', sys='hud', q='Keep the altar menu you have, or the new one (class painting, tab icons, every brew\'s icon)?', pick='The new one', where='workshop', done='The new Echo Altar menu in Ember Tally (build 4).'),
    dict(when='2026-11-27', sys='spells', q='What replaces Immolate (the Destruction fire spell at Tier III)?', pick='Fire Storm - then corrected: Immolate stays, Fire Storm replaces Inferno', where='workshop and your test', done='Immolate is back; Fire Storm takes Inferno\'s place in Walls (this build).'),
]

# ------------------------------------------------------------------------------------------------ each class's own moves (Nov 27: "how can we set up the unique
# abilities each class has? not all of them cast the same spells or powers like the tablet weapon pool" - "Own moves, like the
# Tablet": a Light, a Heavy and a Special that only that class has; the element kits only decide how they look; each tier stronger)
# each option: id, name, what it does, and the kit part its preview plays (role:design) with how you move (swing / slam / cast / hold)
def A(a, t=.2, d=None): return dict(k='act', a=a, t=t, **({'d': d} if d else {}))
def FX(t, role, opt, at='t0', size=1, el=None): return dict(k='fx', t=t, role=role, opt=opt, at=at, size=size, **({'el': el} if el else {}))
def R(t, who, r='elem', **kw): return dict(k='react', t=t, who=who, r=r, **kw)
def BOLT(t, to='t0', d=.35, curve=False, hit=None, frm='hand', size=1, el=None): return dict(k='bolt', t=t, to=to, d=d, curve=curve, hit=hit, **{'from': frm}, size=size, **({'el': el} if el else {}))
def ARC(t, frm, to, d=.3): return dict(k='arc', t=t, **{'from': frm}, to=to, d=d)
def SMALL(role='impact', opt='core', size=.6): return dict(role=role, opt=opt, size=size)
# each class's own moves: (id, name, what it does, the script its preview plays - the description, beat by beat)
CLASS_MOVES = {
    'alchemist': dict(el='shadowfire', why='The Alchemist turns the dead into fuel: brands, catalysts and transmutation.', moves=[
        ('light', 'Light attack', [
            ('reaping', 'Reaping Arc', 'A wide sweep that cuts every one of the dead in front of you and sets them smouldering in shadowfire; the burn stacks with each swing.',
             [A('swing', .2), FX(.32, 'impact', 'spark', 't0', .8), FX(.36, 'impact', 'spark', 't4', .7), FX(.4, 'impact', 'spark', 't2', .7), R(.34, 't0'), R(.38, 't4'), R(.42, 't2')]),
            ('catalyst', 'Catalyst Cut', 'Each hit marks the dead with a violet catalyst; a marked one that dies bursts, burning the ones beside it.',
             [A('swing', .2), FX(.32, 'impact', 'core', 't0', .7), dict(k='mark', t=.35, at='t0', d=1.2), R(1.3, 't0', 'die'), FX(1.35, 'burst', 'big', 't0', .7), R(1.4, 'near:260@t0', 'burn')]),
            ('corrode', 'Corroding Edge', 'Hits leave a corroding brand that eats armour and spreads to the next of the dead it touches.',
             [A('swing', .2), FX(.32, 'impact', 'drain', 't0', .7), R(.34, 't0', 'poison'), ARC(.9, 't0', 't4', .5), FX(1.0, 'impact', 'drain', 't4', .6), R(1.0, 't4', 'poison')])]),
        ('heavy', 'Heavy attack', [
            ('eruption', 'Shadowfire Eruption', 'The blade driven into the ground: geysers of shadowfire burst up in a ring round you (your kit pick).', [A('slam', .2), FX(.42, 'burst', 'spikes', 'me', 1.1), R(.55, 'near:340@me', 'stagger'), R(.6, 'near:340@me')]),
            ('harvest', 'Grim Harvest', 'A full spinning reap that pulls the dead within 5 m in toward you and cuts them all.', [A('swing', .15), A('swing', .45), FX(.5, 'burst', 'nova', 'me', 1), R(.5, 'near:420@me', 'pull'), R(.9, 'near:420@me')]),
            ('transmute', 'Transmutation', 'The slam turns the dead within 4 m to burning ash statues that shatter when struck.', [A('slam', .2), FX(.42, 'ground', 'wave', 't0', 1), R(.6, 'near:300@t0', 'freeze'), R(1.6, 'near:300@t0', 'shatter')])]),
        ('special', 'Special', [
            ('philosopher', 'Philosopher\'s Edge', 'Every 10th kill transmutes: the next of the dead you kill bursts into Cursed Essence and shadowfire.', [A('swing', .2), R(.34, 't0', 'die'), FX(.36, 'burst', 'big', 't0', .8), dict(k='motes', t=.5, **{'from': 't0'}, to='me', d=.8, c=['#5a3a00', '#ffc54a', '#fff2c0'])]),
            ('volatile', 'Volatile Brew', 'Kills sometimes drop a cracked flask that bursts in shadowfire a second later.', [A('swing', .2), R(.34, 't0', 'die'), BOLT(.5, 't0', .4, True, None, 't0', .5), FX(1.4, 'burst', 'big', 't0', .7), R(1.45, 'near:260@t0', 'burn')]),
            ('reaper', 'Reaper\'s Shade', 'Every 4th hit, a violet ghost of the scythe slashes beside you on its own.', [A('swing', .2), FX(.32, 'impact', 'core', 't0', .6), R(.34, 't0'), dict(k='ghost', t=.5, d=1.8, at=[-120, -60], face='t4', swings=[.5, 1.1]), FX(1.05, 'impact', 'spark', 't4', .8), R(1.05, 't4', 'die')])]),
    ]),
    'summoner': dict(el='holy', why='The Summoner fights beside what it calls: marks for its servants, light from the sky, spirits on command.', moves=[
        ('light', 'Light attack', [
            ('radiant', 'Radiant Strike', 'Each strike flares with sunlight that sears the dead and blinds them for a moment.', [A('swing', .2), FX(.32, 'impact', 'core', 't0', .9), R(.34, 't0', 'burn'), R(.5, 't0', 'lost')]),
            ('lash', 'Spirit Lash', 'Each strike sends a small spirit off the staff that seeks out another of the dead.', [A('swing', .2), FX(.32, 'impact', 'core', 't0', .6), R(.34, 't0'), BOLT(.45, 't1', .6, True, SMALL(), 't0', .6, 'spirit'), R(1.05, 't1', 'lost')]),
            ('command', 'Command', 'Each strike marks the dead: your summons go for the marked first, and hit them harder.', [A('swing', .2), FX(.32, 'impact', 'core', 't0', .6), dict(k='mark', t=.35, at='t0', d=1.8), dict(k='ghost', t=.6, d=1.6, at=[90, 120], face='t0', swings=[.6, 1.0]), R(1.2, 't0', 'die')])]),
        ('heavy', 'Heavy attack', [
            ('dawn', 'Pillar of Dawn', 'Both heads driven down: a pillar of sunlight pours onto the spot (your kit pick).', [A('slam', .2), FX(.42, 'above', 'pillar', 't0', 1), R(.6, 'near:300@t0', 'burn'), R(1.6, 't0', 'die')]),
            ('pack', 'Call the Pack', 'The slam calls two spectral wolves out of the light to hunt for 8 s.', [A('slam', .2), FX(.42, 'burst', 'nova', 'me', .8), dict(k='ghost', t=.55, d=1.9, at=[-60, 40], face='t0', swings=[.5, 1.1], el='spirit'), dict(k='ghost', t=.7, d=1.8, at=[-80, -90], face='t4', swings=[.6, 1.2], el='spirit'), R(1.2, 't0', 'die'), R(1.4, 't4', 'die')]),
            ('consecrate', 'Consecrate', 'A circle of runes where your summons heal and the dead burn.', [A('slam', .2), FX(.42, 'ground', 'rune', 'front', 1.1), R(.7, 'near:330@front', 'burn')])]),
        ('special', 'Special', [
            ('bond', 'Soul Bond', 'Every 15 kills, your summons blaze with light: half again as strong for 10 s.', [A('cast', .2), FX(.4, 'burst', 'big', 'me', .7), dict(k='ghost', t=.5, d=1.8, at=[40, 140], face='t0', swings=[.5, .9, 1.3]), R(1.0, 't0', 'die')]),
            ('ancestor', 'Ancestor\'s Guard', 'Every 20 kills, a spectral Nord warrior rises from the light and fights beside you for 12 s.', [A('cast', .2), FX(.3, 'above', 'pillar', [-60, 70], .7), dict(k='ghost', t=.6, d=2.4, at=[-60, 70], face='t0', swings=[.9, 1.5, 2.0]), FX(1.55, 'impact', 'core', 't0', .8), R(1.55, 't0', 'die'), R(2.05, 't4', 'stagger')]),
            ('orbit', 'Radiant Orbit', 'Three orbs of light circle you and strike the dead that come close.', [dict(k='orbit', t=.1, d=2.4, n=3), BOLT(.8, 't4', .3, False, SMALL(), 'me', .5), R(1.1, 't4', 'burn'), BOLT(1.5, 't0', .35, False, SMALL(), 'me', .5), R(1.85, 't0', 'burn')])]),
    ]),
    'destruction': dict(el='arcane', why='The Battle-mage is the caster: a tap fires (0.5 s between shots), a hold channels a beam that costs magicka, and power builds the longer you keep it up.', moves=[
        ('light', 'Tap: the bolt (0.5 s between shots)', [
            ('glint', 'Glintstone Bolt', 'A tap fires a glintstone bolt that bends a little toward the nearest of the dead.', [A('cast', .2), BOLT(.25, 't0', .4, True, SMALL()), R(.66, 't0'), A('cast', .75), BOLT(.8, 't4', .4, True, SMALL()), R(1.2, 't4'), A('cast', 1.3), BOLT(1.35, 't2', .45, True, SMALL()), R(1.8, 't2', 'die')]),
            ('spread', 'Arcane Spread', 'A tap fires three smaller bolts in a fan.', [A('cast', .2), BOLT(.25, 't0', .4, False, SMALL(size=.5), size=.6), BOLT(.25, 't4', .4, False, SMALL(size=.5), size=.6), BOLT(.25, 't2', .45, False, SMALL(size=.5), size=.6), R(.66, 't0'), R(.66, 't4'), R(.7, 't2')]),
            ('pierce', 'Piercing Ray', 'A tap fires a narrow ray that passes through the first of the dead into the next.', [A('cast', .2), BOLT(.25, 't0', .25, False, SMALL(size=.5), size=.6), BOLT(.5, 't1', .25, False, SMALL(size=.5), 't0', .6), R(.5, 't0'), R(.75, 't1')])]),
        ('heavy', 'Hold: the beam (costs magicka)', [
            ('ray', 'Arcane Ray', 'Hold: a beam of raw magicka from the wand\'s tip that grows wider and hotter the longer you hold it, draining your magicka while it runs.', [A('hold', .2, 2.2), dict(k='beam', mana=1, t=.3, d=2.1, to='t0'), R(.4, 't0', 'shock'), R(2.2, 't0', 'die'), FX(2.25, 'impact', 'core', 't0', .8)]),
            ('fork', 'Forking Beam', 'Hold: a beam that forks to two more of the dead near the one it holds (it costs magicka while it runs).', [A('hold', .2, 2.2), dict(k='beam', mana=1, t=.3, d=2.1, to='t0', fork=['t1', 't4']), R(.4, 't0', 'shock'), R(.5, 't1', 'shock'), R(.5, 't4', 'shock'), R(2.2, 't0', 'die')]),
            ('disintegrate', 'Disintegrate', 'Hold: a beam that burns a line through the dead; what it kills bursts in magicka (it costs magicka while it runs).', [A('hold', .2, 2.2), dict(k='beam', mana=1, t=.3, d=2.1, to='t1'), R(.5, 'line', 'burn'), R(1.4, 't0', 'die'), FX(1.45, 'burst', 'big', 't0', .6), R(2.2, 't1', 'die'), FX(2.25, 'burst', 'big', 't1', .6)])]),
        ('special', 'Special', [
            ('starfall', 'Starfall', 'Every 25th bolt, twelve great bolts rain down in front of you.', [A('cast', .2), dict(k='rain', t=.4, n=12, d=1.4, at='t0', R=230, size=.9, hit=SMALL('impact', 'core', .7)), R(.9, 'near:260@t0', 'stagger'), R(1.6, 'near:260@t0', 'die')]),
            ('echo', 'Arcane Echo', 'Beam kills leave a rune that bursts a second later.', [A('hold', .2, 1.2), dict(k='beam', mana=1, t=.3, d=1.1, to='t0'), R(1.3, 't0', 'die'), dict(k='mark', t=1.35, at='t0', d=1.0), FX(2.3, 'burst', 'nova', 't0', .7), R(2.35, 'near:280@t0', 'stagger')]),
            ('surge', 'Mana Surge', 'Beam kills refill your magicka, and your next tap fires three bolts.', [A('hold', .2, 1.0), dict(k='beam', mana=1, t=.3, d=.9, to='t0'), R(1.1, 't0', 'die'), dict(k='motes', t=1.15, **{'from': 't0'}, to='me', d=.6), A('cast', 1.9), BOLT(1.95, 't4', .35, False, SMALL()), BOLT(1.95, 't2', .4, False, SMALL()), BOLT(1.95, 't1', .45, False, SMALL())])]),
    ]),
    'spellsword': dict(el='fire', els=['fire', 'frost', 'shock'], why='The Spellsword fuses blade and spell: the other hand\'s element rides every move.', moves=[
        ('light', 'Light attack', [
            ('spellthrust', 'Spellthrust', 'Each thrust carries the spell in your other hand: fire burns, frost slows, shock arcs to one more.', [A('swing', .2), FX(.32, 'impact', 'core', 't0', .7), R(.34, 't0'), ARC(.5, 't0', 't4', .3), R(.55, 't4')]),
            ('riposte', 'Riposte', 'Block a blow just in time and the counter-thrust strikes three times as hard in a burst of your element.', [R(.1, 't0', 'flinch'), A('swing', .45), FX(.58, 'impact', 'spark', 't0', 1.3), R(.6, 't0', 'die')]),
            ('flurry', 'Flurry', 'Every 3rd thrust is a quick double thrust.', [A('swing', .2), FX(.32, 'impact', 'core', 't0', .5), A('swing', .6), FX(.72, 'impact', 'core', 't0', .5), A('swing', .8), FX(.92, 'impact', 'core', 't0', .6), R(.95, 't0', 'die')])]),
        ('heavy', 'Heavy attack', [
            ('pillar', 'Spell Pillar', 'A pillar of your element pours down on the one you strike (your kit pick).', [A('slam', .2), FX(.42, 'above', 'pillar', 't0', 1), R(.6, 'near:300@t0'), R(1.5, 't0', 'die')]),
            ('lunge', 'Spell Lunge', 'You dash 6 m through the dead, leaving a trail of your element.', [dict(k='dash', t=.15, d=.35, to=[140, -60]), A('swing', .2), FX(.4, 'ground', 'wave', 't0', .8), R(.35, 't0'), R(.42, 't1')]),
            ('lance', 'Spell Lance', 'A long lance of your element pierces everything in a line.', [A('slam', .2), dict(k='beam', t=.4, d=.45, to='t1'), R(.5, 'line', 'die')])]),
        ('special', 'Special', [
            ('resonance', 'Resonance', 'Each spell cast with your other hand charges the blade: your next heavy is twice as strong.', [A('cast', .2), FX(.35, 'impact', 'drain', 'me', .6), A('slam', .9), FX(1.1, 'burst', 'big', 't0', 1.2), R(1.15, 'near:360@t0', 'die')]),
            ('wake', 'Elemental Wake', 'Kills leave a patch of your element on the ground that burns, freezes or shocks the dead that cross it.', [A('swing', .2), R(.34, 't0', 'die'), FX(.5, 'ground', 'mist', 't0', .7), R(1.0, 'near:240@t0')]),
            ('focus', 'Duelist\'s Focus', 'Go 5 s without being hit and your next thrust is a sure critical.', [FX(.1, 'impact', 'drain', 'me', .5), A('swing', .9), FX(1.02, 'impact', 'spark', 't0', 1.4), R(1.05, 't0', 'die')])]),
    ]),
}
# the wonders (Nov 27: "the wonders aren't in the weapon & effects"): each its model, its look, and what its description says it does
def JAV(t, to, d, curve, frm, size=1.0): return dict(BOLT(t, to, d, curve, None, frm, size), model='javelin', len=150)
# the wonders (Nov 27: "the wonders aren't in the weapon & effects"; "it needs to look and have the more customizable options like the
# class weapons tab"): each its model, its five looks, how strong its look is, and each of its moves with three designs - the first
# what it does today - played as its description says
WONDERS = [
    dict(id='soulcaller', key='soulcaller', cls='summoner', name='Soulcaller\'s Staff', el='spirit', look='o2', who='Summoner\'s wonder (The Ghost Pride)',
         text='Hold to cast: each of the dead the beam kills gives a soul. Every 20 souls: a spectral sabre cat for 45 s (max 3).',
         slots=[('hold', 'Hold: the soul beam', [
             ('soulbeam', 'Soul Beam (today)', 'A beam of soul-light from the staff\'s head; each of the dead it kills gives you a soul.',
              [A('hold', .2, 2.3), dict(k='beam', t=.3, d=2.2, to='t0'), R(.4, 't0', 'drain'), R(1.6, 't0', 'die'), dict(k='motes', t=1.65, **{'from': 't0'}, to='me', d=.6)]),
             ('soulchain', 'Soul Chain', 'The beam leaps from the one it holds to two more; every kill on the chain gives a soul.',
              [A('hold', .2, 2.3), dict(k='beam', t=.3, d=2.2, to='t0', fork=['t1', 't4']), R(.4, 't0', 'drain'), R(.5, 't1', 'drain'), R(.5, 't4', 'drain'), R(1.7, 't1', 'die'), dict(k='motes', t=1.75, **{'from': 't1'}, to='me', d=.6), R(2.1, 't0', 'die')]),
             ('harvest', 'Soul Harvest', 'The beam tears souls out of every one of the dead near the one it holds, streaming back to you.',
              [A('hold', .2, 2.3), dict(k='beam', t=.3, d=2.2, to='t0'), R(.5, 'near:300@t0', 'drain'), dict(k='motes', t=.8, **{'from': 't0'}, to='me', d=1.4), dict(k='motes', t=1.0, **{'from': 't4'}, to='me', d=1.2), R(2.2, 'near:300@t0', 'die')])]),
           ('souls', 'Every 20 souls', [
             ('cat', 'Spectral sabre cat (today)', 'A spectral sabre cat leaps out of the light and hunts for 45 s (up to 3).',
              [FX(.2, 'burst', 'nova', 'me', .6), dict(k='ghost', t=.4, d=2.4, at=[-80, 60], face='t0', swings=[.8, 1.4, 2.0]), R(1.3, 't0', 'die'), R(1.9, 't4', 'die')]),
             ('pack', 'Spectral wolf pack', 'Two spectral wolves instead of one cat: faster, weaker, they bring the dead down together.',
              [FX(.2, 'burst', 'nova', 'me', .6), dict(k='ghost', t=.4, d=2.2, at=[-60, 40], face='t0', swings=[.7, 1.2]), dict(k='ghost', t=.55, d=2.2, at=[-90, -80], face='t4', swings=[.8, 1.3]), R(1.2, 't0', 'die'), R(1.4, 't4', 'die')]),
             ('storm', 'Soul storm', 'The souls burst out of you as a storm of spirits that circle you and strike the dead that come close.',
              [FX(.2, 'burst', 'big', 'me', .5), dict(k='orbit', t=.3, d=2.3, n=5), BOLT(.9, 't4', .3, False, SMALL(), 'me', .5), BOLT(1.3, 't0', .3, False, SMALL(), 'me', .5), BOLT(1.7, 't2', .35, False, SMALL(), 'me', .5), R(1.2, 't4', 'die'), R(1.6, 't0', 'die'), R(2.05, 't2', 'die')])])]),
    dict(id='javelin', key='javelin', cls='destruction', name='Wyrmspine Javelin', el='fire', els=['fire', 'frost', 'shock'], look='o5', who='Destruction\'s wonder (The Eye of Magnus)',
         text='Hold attack to charge, let go to throw: it bursts in your other hand\'s element, bigger the longer you held. A kill sends it on to the next enemy; then it flies back to you.',
         slots=[('throw', 'Hold, then throw', [
             ('bounce', 'Bursting throw (today)', 'It bursts in your other hand\'s element where it lands; a kill sends it on to the next of the dead.',
              [A('hold', .2, .9), FX(.3, 'impact', 'drain', 'me', .5), A('throw', 1.1), JAV(1.15, 't0', .35, False, 'hand'), FX(1.5, 'burst', 'big', 't0', .7), R(1.55, 't0', 'die'), JAV(1.7, 't1', .4, True, 't0', .8), FX(2.1, 'impact', 'spark', 't1', .8), R(2.1, 't1', 'die')]),
             ('pierce', 'Piercing throw', 'It flies straight through every one of the dead in a line, a trail of its element behind it.',
              [A('hold', .2, .9), FX(.3, 'impact', 'drain', 'me', .5), A('throw', 1.1), JAV(1.15, 't1', .5, False, 'hand'), R(1.35, 'line', 'die'), FX(1.65, 'ground', 'wave', 't1', .7)]),
             ('impale', 'Impaling throw', 'It pins the first of the dead to the ground and the element erupts round it a moment later.',
              [A('hold', .2, .9), FX(.3, 'impact', 'drain', 'me', .5), A('throw', 1.1), JAV(1.15, 't0', .35, False, 'hand'), R(1.5, 't0', 'freeze'), dict(k='mark', t=1.5, at='t0', d=.9), FX(2.3, 'burst', 'spikes', 't0', .8), R(2.35, 'near:300@t0', 'die')])]),
           ('back', 'Its return', [
             ('fly', 'Flies back (today)', 'It flies back to your hand.',
              [JAV(.3, 't0', .01, False, 't0', .6), JAV(.4, 'me', .6, True, 't0', .8), A('cast', 1.0)]),
             ('cleave', 'Cleaving return', 'On its way back it cuts through the dead between it and you.',
              [JAV(.4, 'me', .6, False, 't1', .8), R(.6, 'line', 'die'), A('cast', 1.0)]),
             ('storm', 'Storm return', 'Lightning follows it back, striking the dead it passes.',
              [JAV(.4, 'me', .7, True, 't0', .8), ARC(.5, 't0', 't4', .3), ARC(.7, 't4', 't2', .3), R(.55, 't4', 'shock'), R(.75, 't2', 'shock'), A('cast', 1.1)])])]),
    dict(id='mutagen', key='flaskweap_philosopher', cls='alchemist', name='Mutagen Staff', el='poison', look='o1', who='Alchemist\'s wonder (The Elixir of Aetherius)',
         text='Hold block: throws a flask every 0.5 s, cycling the six elements. Power attack: throws the next flask. And its normal staff attacks.',
         slots=[('block', 'Hold block: the flasks', [
             ('six', 'Six elements (today)', 'A flask every 0.5 s, each the next of the six elements.',
              [A('throw', .2), BOLT(.25, 't0', .5, True, SMALL('burst', 'big', .5), 'hand', .5, 'fire'), A('throw', .7), BOLT(.75, 't4', .5, True, SMALL('burst', 'nova', .5), 'hand', .5, 'frost'), A('throw', 1.2), BOLT(1.25, 't2', .5, True, SMALL('impact', 'spark', .7), 'hand', .5, 'shock'), A('throw', 1.7), BOLT(1.75, 't1', .5, True, SMALL('ground', 'rune', .5), 'hand', .5, 'poison'),
               R(.75, 't0', 'burn'), R(1.25, 't4', 'freeze'), R(1.75, 't2', 'shock'), R(2.25, 't1', 'poison')]),
             ('volley', 'Volley', 'Every second, three flasks at once in a fan, the same element, cycling.',
              [A('throw', .2), BOLT(.25, 't0', .5, True, SMALL('burst', 'big', .45), 'hand', .5, 'fire'), BOLT(.25, 't4', .5, True, SMALL('burst', 'big', .45), 'hand', .5, 'fire'), BOLT(.25, 't2', .55, True, SMALL('burst', 'big', .45), 'hand', .5, 'fire'), R(.75, 't0', 'burn'), R(.75, 't4', 'burn'), R(.8, 't2', 'burn'),
               A('throw', 1.2), BOLT(1.25, 't0', .5, True, SMALL('burst', 'nova', .45), 'hand', .5, 'frost'), BOLT(1.25, 't1', .5, True, SMALL('burst', 'nova', .45), 'hand', .5, 'frost'), BOLT(1.25, 't3', .55, True, SMALL('burst', 'nova', .45), 'hand', .5, 'frost'), R(1.75, 't0', 'freeze'), R(1.75, 't1', 'freeze'), R(1.8, 't3', 'freeze')]),
             ('mortar', 'Mortar', 'One big flask every 1.5 s, lobbed high, bursting wide where it lands.',
              [A('throw', .2), BOLT(.25, 't0', .8, True, SMALL('burst', 'big', 1.0), 'hand', .8, 'poison'), R(1.1, 'near:320@t0', 'poison'), R(1.9, 't0', 'die')])]),
           ('strike', 'Its staff strike', [
             ('strike', 'Staff strike (today)', 'The staff\'s normal strike: the dead stagger.', [A('swing', .2), FX(.32, 'impact', 'core', 't0', .6), R(.34, 't0', 'stagger')]),
             ('mutate', 'Mutating strike', 'Each strike marks the dead with a mutation that bursts a second later in the last flask\'s element.',
              [A('swing', .2), FX(.32, 'impact', 'core', 't0', .5), dict(k='mark', t=.35, at='t0', d=1.0), FX(1.35, 'burst', 'big', 't0', .6), R(1.4, 'near:240@t0', 'poison')]),
             ('splash', 'Acid splash', 'Each strike splashes acid off the staff\'s head over the dead in front of you.',
              [A('swing', .2), FX(.32, 'impact', 'spark', 't0', .9), R(.36, 'near:220@t0', 'poison')])])]),
    dict(id='slayer', key='cls_dragonslayer_slayer', cls='spellsword', name='Dragon Slayer', el='holy', look='o3', who='Spellsword\'s wonder (The Slayer\'s Oath)',
         text='Light: a golden fireball, 2x damage to dragons. Heavy: 5 s of golden fireballs on the dead within 20 m.',
         slots=[('light', 'Light attack', [
             ('fireball', 'Golden fireball (today)', 'Each swing looses a golden fireball; twice the damage to dragons.', [A('swing', .2), BOLT(.3, 't0', .35, False, SMALL('burst', 'big', .6), 'hand', .8), R(.66, 't0', 'die')]),
             ('wave', 'Golden wave', 'Each swing sends a crescent of golden fire along the ground through the dead in front of you.', [A('swing', .2), FX(.35, 'ground', 'wave', 'front', .9), R(.5, 'line', 'burn'), R(.9, 't0', 'die')]),
             ('brand', 'Dragonbane brand', 'Hits brand the dead in gold; the third brand on one bursts in golden fire.', [A('swing', .2), FX(.32, 'impact', 'core', 't0', .5), A('swing', .6), FX(.72, 'impact', 'core', 't0', .5), A('swing', 1.0), dict(k='mark', t=1.1, at='t0', d=.6), FX(1.15, 'burst', 'big', 't0', .8), R(1.2, 'near:260@t0', 'die')])]),
           ('heavy', 'Heavy attack', [
             ('rain', 'Golden rain (today)', '5 s of golden fireballs falling on the dead within 20 m.', [A('slam', .2), dict(k='rain', t=.4, n=14, d=2.2, at='t0', R=260, size=.7, hit=SMALL('burst', 'big', .45)), R(1.0, 'near:300@t0', 'burn'), R(2.3, 'near:300@t0', 'die')]),
             ('sovngarde', 'Light of Sovngarde', 'A pillar of golden light falls where you strike and the dead in it burn away.', [A('slam', .2), FX(.42, 'above', 'pillar', 't0', 1.1), R(.6, 'near:300@t0', 'burn'), R(1.6, 'near:300@t0', 'die')]),
             ('dragon', 'Spectral dragon', 'A spectral dragon\'s breath of golden fire sweeps across the dead in front of you.', [A('slam', .2), dict(k='beam', t=.4, d=1.4, to='t1'), R(.6, 'line', 'burn'), R(1.6, 'line', 'die'), FX(.5, 'ground', 'mist', 't0', .9)])])]),
    dict(id='bane', key='fang', cls='summoner', name='Alduin\'s Bane', el='fire', look='o2', who='The last weapon (the Hall of the Awakened)',
         text='Light: Ash of Helgen - the fire pulled into the one struck until it pops, embers on the ground, the fire spreading to those beside it. Heavy: a pillar of white wind 3 m before you that hurls the dead near it away.',
         slots=[('light', 'Light attack', [
             ('meteor', 'Meteor on each hit', 'A large meteor falls on each of the dead you hit.', [A('swing', .2), R(.34, 't0', 'flinch'), FX(.4, 'above', 'sky', 't0', 1), R(1.0, 'near:300@t0', 'die')]),
             ('shout', 'World-Eater\'s breath', 'Each swing breathes a short cone of Alduin\'s fire through the dead in front of you.', [A('swing', .2), dict(k='beam', t=.3, d=.6, to='t0'), R(.4, 'near:200@t0', 'burn'), R(1.0, 't0', 'die')]),
             ('ash', 'Ash of Helgen (today)', 'Hits burn the dead to ash that falls and smoulders, spreading to those next to them.', [A('swing', .2), FX(.32, 'impact', 'drain', 't0', .7), R(.4, 't0', 'burn'), FX(.9, 'ground', 'mist', 't0', .8), R(1.1, 'near:240@t0', 'burn'), R(1.6, 't0', 'die')])]),
           ('heavy', 'Heavy attack', [
             ('tornado', 'White tornado (today)', 'A pillar of white wind pours down before you and hurls the dead near it away; the rest stagger.', [A('slam', .2), FX(.4, 'above', 'pillar', 'front', 1.2, 'wind'), R(.7, 'near:420@front', 'fling'), R(1.6, 'all', 'stagger')]),
             ('firestorm', 'Firestorm', 'A ring of Alduin\'s fire bursts out round you and the ground burns.', [A('slam', .2), FX(.4, 'burst', 'nova', 'me', 1.3), FX(.6, 'ground', 'wave', 'me', 1.1), R(.6, 'near:420@me', 'burn'), R(1.5, 'near:420@me', 'die')]),
             ('timewound', 'Time-Wound', 'Time stops round the blade: the dead freeze where they stand, then shatter.', [A('slam', .2), FX(.4, 'burst', 'nova', 'front', 1.1, 'arcane'), R(.5, 'all', 'freeze'), R(1.7, 'all', 'shatter')])])]),
]

# ------------------------------------------------------------------------------------------------ the lore (Nov 26 late: "add one more tab in the workshop for
# the story for CoA, the easter eggs, and trials - all of the lore essentially"): read from the mod itself, so it is what the game says
def lore():
    import re
    gen = (ROOT / 'src' / 'Generated.h').read_text()
    trials = re.findall(r'TrialDef\{ "(\w+)", "([^"]+)", "([^"]+)", "[^"]*", "([^"]+)"', gen)
    vo = json.loads((ROOT / 'docs' / 'altarvo_lines.json').read_text())['lines']
    def asks(group):
        out = {}
        for l in vo:
            if l['group'] == group: out.setdefault(l['ask'], []).append(re.sub(r'\[[^\]]*\]\s*', '', l['text']))
        return [dict(q=q, a=a) for q, a in out.items()]
    eggs = json.loads((ROOT / 'sheets' / 'eggs.json').read_text())
    mn = json.loads((ROOT / 'docs' / 'workshop' / 'modnow.json').read_text())
    relics = next(items for t, items in mn['sections'] if 'Priest' in t)[:10]
    whisper = eggs.get('whisper', {})
    return [
        dict(id='curse', title='The Curse', lead='At Helgen, Alduin shouted SLEN TIID VO - his raising shout - into the earth, and every grave in Skyrim heard it. His voice now hangs like a mist over the road to Sovngarde: the dead cannot pass, and every night it throws them back down to Skyrim as red beams of light. The Echo Altars the old Tongues raised in every city went dark; wake them, city by city, and the dead sleep again. The questline ends right before Skyrim\'s own fight with Alduin.',
             qa=asks('The story so far')),
        dict(id='felldir', title='Felldir the Old', lead='The voice in the altar\'s centre stone - one of the three ancient Tongues who cast Alduin out of time. He speaks only from a freed altar, and his story opens as more cities are freed (his name is kept secret until six altars are free).',
             qa=asks('Who the voice is')),
        dict(id='trials', title='The Echo Altars and their four trials', lead='The same four trials in every city, each with the city\'s own places, one after another, all at night, the party sharing them. The last seal calls the city\'s Dragon Priest out at once; his fall frees the city.',
             items=[dict(t=n, s=sub, d=re.sub(r'\{n\}', 'a number of', d.replace('{city}', 'the city'))) for k, n, sub, d in trials]),
        dict(id='cities', title='The cities and their Dragon Priests', lead='Ten cities, each with its altar, its trials\' places and its Dragon Priest. Each priest drops his own relic to every player when he falls.',
             items=[dict(t=re.sub('<[^>]+>', '', r.split(':')[0]), d=re.sub('<[^>]+>', '', r.split(':', 1)[1]).strip()) for r in relics], qa=asks('This city')),
        dict(id='classes', title='Your class and its wonder', lead='Each class has its own place in the story and its own wonder, earned by a hidden quest that Felldir whispers.',
             qa=asks('Your class and wonder')),
        dict(id='eggs', title='The hidden quests (Easter eggs)', lead=whisper.get('asleep', '') + ' Each wonder\'s quest has five steps, whispered by Felldir one at a time; the reward waits in the great hall of High Hrothgar.',
             eggs=[dict(t=r['title'], cls=r['cls'], reward=r['rewardText'], open=r.get('open', ''), steps=[dict(t=st['title'], d=st['text'], hint=st.get('hint', '')) for st in r['steps']],
                        whispers=[re.sub(r'\[[^\]]*\]\s*', '', l['text']) for l in vo if l['group'] == 'Whispers: ' + r['title']]) for r in eggs['rows']],
             qa=asks('Whispers: cities') + asks('Whispers: signs') + asks('Whispers: reveals')),
        dict(id='end', title='The dragons and the end', lead='Every 10th night a dragon; the three word-dragons and the Echo of Alduin are the hardest fights of all (35x, 40x, 45x and 80x health, tougher waves, enrage phases). Alduin\'s Bane waits in the Hall of the Awakened: Ash of Helgen on every light hit, a white tornado on a heavy. The Curse of Alduin ends right before Skyrim\'s own battle with Alduin.',
             items=[dict(t='Dragons', d='A dragon comes every 10th night; its words teach the city\'s shout (Solitude\'s bards teach Tiny Terrors, Whiterun gives Dragon\'s Updraft).'),
                    dict(t='The three word-dragons', d='The last three dragons, each guarding a word: the hardest fights before the end.'),
                    dict(t='The Echo of Alduin', d='Alduin\'s own echo, the curse made flesh: 80 times the health of the dead, the last fight of the mod.'),
                    dict(t='Alduin\'s Bane', d='The last weapon, from the Hall of the Awakened.')]),
    ]


# ------------------------------------------------------------------------------------------------ his notes, kept with the build they went into
# (Noah, Nov 26 late: "clear my notes in the boxes for the build and save them in the build history so it's documented")
NOTE_LOG = json.loads((HERE / 'notes_log.json').read_text()) if (HERE / 'notes_log.json').exists() else []
PICK_LOG = json.loads((HERE / 'picks_log.json').read_text()) if (HERE / 'picks_log.json').exists() else []   # (picks.py: his picks and decisions, round by round)

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


# ------------------------------------------------------------------------------------------------ the element kits (Nov 26 night: "go ahead and make the element kits")
# each element: Skyrim's own 3D effects (Skyrim, Dawnguard, Dragonborn, Apocalypse, Mysticism - read from Noah's game by
# tools/realpreview.py) offered for each part of the kit, and Skyrim's own sounds for its cast, impact and burst
KIT_THEME = {'fire': 'fire', 'frost': 'frost', 'shock': 'shock', 'shadowfire': 'shadow', 'holy': 'sun', 'poison': 'poison', 'arcane': 'arcane',
             'blood': 'blood', 'spirit': 'spirit', 'earth': 'earth', 'wind': 'wind'}
KIT_ROLES = [('impact', 'Impact', 'One hit landing.', ['core', 'spark', 'drain']),
             ('burst', 'Burst', 'A heavy or an explosion.', ['big', 'nova', 'spikes']),
             ('ground', 'Ground', 'What lingers where it struck.', ['mist', 'rune', 'wave']),
             ('above', 'From above', 'A strike from the sky.', ['sky', 'pillar'])]
KIT_SOUNDS = {
    'fire': {'cast': ['WB_DestructionFire_Sound_Release_1', 'WB_DestructionFire_Sound_Release_Emberbolt', 'MAGFireboltFire2DSD'],
             'impact': ['MAGFireboltImpactSD', 'WB_DestructionFire_Sound_Impact_Fwoosh', 'WB_DestructionFire_Sound_Impact_Immolation'],
             'burst': ['MAGDestructionFireballExplosion', 'MAGDestructionFireStormExplosion', 'WB_DestructionFire_Sound_Explosion_FlashFireball']},
    'frost': {'cast': ['MAGIcicleFireSD', 'WB_DestructionFrost_Release_1_Sound_Tiny', 'MAGFrostBiteFireLPMSD'],
              'impact': ['MAGIcicleImpactBreakSD', 'MAGFrostBiteImpactSD', 'WB_DestructionFrost_Sound_Impact_Crystalize'],
              'burst': ['FXExplosionIceSD', 'WB_DestructionFrost_Sound_Explosion_Shatter', 'WB_DestructionFrost_Sound_Explosion_Cryoblast']},
    'shock': {'cast': ['MAGShockFFFireSD', 'WB_DestructionShock_Sound_Release_JoyBuzzer_2', 'MAGShockConcentrationFireLPMSD'],
              'impact': ['MAGShockImpactSD', 'MAGRuneImpactSD', 'WB_DestructionShock_Sound_Explosion_Thunderball'],
              'burst': ['WB_DestructionShock_Sound_Explosion_LightningStrike_2C', 'WB_DestructionShock_Sound_Explosion_ShockNova', 'VOCShoutImpactStormCallNearSD']},
    'shadowfire': {'cast': ['WB_IllusionNightmare_Sound_Release', 'WB_IllusionShadow_Sound_Release_Buff', 'MAGIllusionFireLPM'],
                   'impact': ['WB_IllusionShadow_Sound_Explosion_Vanish', 'WB_IllusionNightmare_Sound_Explosion_EnergyBall', 'MAGNightCloakBatsHitLPM'],
                   'burst': ['WB_IllusionNightmare_Sound_Explosion_PaleMoon_Howl', 'WB_Illusion_Sound_Explosion_Subsonic', 'WB_AlterationChaos_Sound_Explosion_Plasma']},
    'holy': {'cast': ['MAGLightFireSD', 'MAGRestorationTurnUndeadFire2DSD', 'WB_AlterationAlt_Sound_Release_IgniteLight'],
             'impact': ['MAGAurielShieldBlastA', 'MAGRestorationFFFireSD', 'WB_RestorationHeal_Sound_Release_MassiveTwinkles'],
             'burst': ['FXExplosionAurielsSunNear', 'FXExplosionAurielBowHarkon', 'WB_Restoration_Sound_Explosion_SubsonicEnergy']},
    'poison': {'cast': ['MAG_PoisonReleaseSFX', 'TRPHarvestBloomReleasePoison', 'MAG_PoisonFireLoopSFX'],
               'impact': ['MAG_PoisonImpactSFX', 'WB_AlterationEarth_Sound_Impact', 'coa:flesh_hit_2'],
               'burst': ['MAG_PoisonExplosionSFX', 'MAG_PoisonRitualExplosionSFX', 'WB_ConjurationDetonate_Sound_Explosion_Blast']},
    'arcane': {'cast': ['WB_DestructionGold_Sound_Release_MagicMissile', 'MAGAlterationReleaseSD', 'WB_AlterationMetamagic_Sound_Release'],
               'impact': ['MAGAetherialShieldImpact', 'WB_DestructionCantrip_Sound_Explosion_ArcaneBurst', 'WB_AlterationTelekinesis_Sound_Explosion_GlassJaw'],
               'burst': ['WB_AlterationTelekinesis_Sound_Explosion_Bwoom', 'MAGPowerSerpentStoneExplosion', 'WB_Restoration_Sound_Explosion_LowSwish']},
    'blood': {'cast': ['WB_RestorationBlood_Sound_Release_Big', 'MAGAlterationVampireDrainRelease', 'WB_RestorationBlood_Sound_Release_OrcishHealing'],
              'impact': ['MAGAlterationVampireDrainOnHit', 'coa:flesh_hit_1', 'WPNImpactBladeVsFleshSD'],
              'burst': ['WB_RestorationNecro_Sound_Explosion_Undead', 'WB_ConjurationDetonate_Sound_Explosion_Blast', 'WB_RestorationBlood_Sound_Release_Big']},
    'spirit': {'cast': ['WB_ConjurationBanish_Sound_Release_Hiss', 'MAGConjureFireSD', 'NPCWispConsciousLP'],
               'impact': ['WB_ConjurationGhosts_Sound_Impact_Wail', 'MAGConjureImpactSD', 'WB_ConjurationSpirit_Sound_Explosion_Whoom'],
               'burst': ['WB_ConjurationCommand_Sound_Explosion_EtherealCall', 'QSTAlduinDeathSoulsASD', 'WB_RestorationNecro_Sound_Explosion_Undead']},
    'earth': {'cast': ['WB_AlterationEarth_Sound_Release', 'WB_AlterationBaleful_Sound_Release_ArmorCrush', 'WPNSwing2Hand'],
              'impact': ['WB_AlterationEarth_Sound_Impact', 'WB_AlterationBaleful_Sound_Impact_Rumble', 'WPNImpactCrGiantClubVsFlesh'],
              'burst': ['WB_AlterationEarth_Sound_Explosion_TalonsOfNirn', 'WB_ConjurationDetonate_Sound_Explosion_Blast', 'MAGPowerSerpentStoneImpact']},
    'wind': {'cast': ['VOCShoutFXSprintFireSD', 'MAGWhirlwindLP', 'coa:sp_cold_whoosh'],
             'impact': ['VOCShoutImpactPushSD', 'WB_DestructionFrost_Sound_Explosion_HighWinds', 'WB_Illusion_Sound_Explosion_Subsonic'],
             'burst': ['VOCShoutImpactPushImpactDragonSD', 'WB_AlterationTelekinesis_Sound_Explosion_Bwoom', 'WB_DestructionFrost_Sound_Explosion_HighWinds']},
}


# (Nov 26 late: "make the previews as good or close to what they would look like in the actual game ... 4k textures and hyper
# realistic VFX made in 3D not flat or layered textures") - the Curse's own effects (vfx.js), each part three designs
EL_WORDS = {   # noun, what erupts, what lingers, what falls
    'fire': ('flame', 'flame geysers', 'flames', 'a meteor'), 'frost': ('ice', 'ice spikes', 'frost mist', 'an ice comet'),
    'shock': ('lightning', 'lightning forks', 'static', 'a lightning strike'), 'shadowfire': ('violet-black flame', 'shadowfire geysers', 'shadowfire', 'a falling shadow-star'),
    'holy': ('sunlight', 'pillars of light', 'sun-motes', 'a sunbeam'), 'poison': ('acid', 'acid geysers', 'poison fog', 'a falling acid globe'),
    'arcane': ('magicka', 'arcane spires', 'magicka haze', 'a beam of magicka'), 'blood': ('blood', 'blood geysers', 'blood mist', 'a falling blood orb'),
    'spirit': ('soul light', 'rising spirits', 'ghost mist', 'a beam of soul light'), 'earth': ('stone and dust', 'stone spikes', 'dust', 'a falling boulder'),
    'wind': ('wind', 'whirlwinds', 'a dust storm', 'a falling gust'),
}
KIT_DESIGNS = {
    'impact': [('core', 'Flare', 'A compact burst of {n} where it lands, sparks thrown out, a wisp left hanging.'),
               ('spark', 'Splash', '{N} spraying off the hit in the direction of the blow.'),
               ('drain', 'Drain', '{N} pulled into the one struck from all round, then a pop.')],
    'burst': [('big', 'Explosion', 'A full blast of {n} rolling up into smoke, debris thrown, the ground scorched.'),
              ('nova', 'Nova ring', 'A ring of {n} tearing outward along the ground from the heart of it.'),
              ('spikes', 'Eruption', '{E} bursting up from the ground in a ring round you.')],
    'ground': [('mist', 'Lingering', 'A low blanket of {m} rolling over the ground for a few seconds.'),
               ('rune', 'Rune circle', 'A circle of runes burned into the ground, {n} rising off it.'),
               ('wave', 'Spreading wave', 'A creeping ring of {m} running outward, leaving patches behind.')],
    'above': [('sky', 'From the sky', '{F} comes down on the spot and bursts.'),
              ('pillar', 'Pillar', 'A turning column of {n} pours down from the sky onto the spot.')],
}


def element_kits():
    snd = json.loads((ROOT / 'docs' / 'workshop' / 'sounds.json').read_text())['sounds']
    out = []
    for eid, name, col, about in ELEMENTS + EXTRA_ELEMENTS:
        n, e, m, f = EL_WORDS[eid]
        fill = lambda t: t.format(n=n, N=n[0].upper() + n[1:], E=e[0].upper() + e[1:], m=m, F=f[0].upper() + f[1:])
        parts = [dict(id=rid, name=rn, about=ra, opts=[dict(id=i, label=l, about=fill(a)) for i, l, a in KIT_DESIGNS[rid]]) for rid, rn, ra, roles in KIT_ROLES]
        sounds = [dict(id=k, name={'cast': 'Cast', 'impact': 'Impact', 'burst': 'Burst'}[k],
                       opts=[dict(id=n_, name=n_.replace('_', ' '), mp3=snd[n_]['mp3'][0], src=snd[n_].get('src', '')) for n_ in v if n_ in snd])
                  for k, v in KIT_SOUNDS[eid].items()]
        out.append(dict(id=eid, name=name, col=col, about=about, theme=KIT_THEME[eid], parts=parts, sounds=sounds))
    return out

# each class weapon's light and heavy, made from its element's kit on the weapon's own page
CLASS_ABIL = {
    'alchemist': dict(els=['shadowfire'], light=dict(act='swing', roles=['impact'], what='A scythe swing: shadowfire where the blade bites.'),
                      heavy=dict(act='slam', roles=['burst', 'ground', 'above'], what='The scythe brought down: shadowfire bursting round the blow.')),
    'summoner': dict(els=['holy', 'spirit'], light=dict(act='swing', roles=['impact'], what='A quarterstaff strike: radiant light on the hit.'),
                     heavy=dict(act='slam', roles=['burst', 'ground', 'above'], what='Both heads driven down: radiance bursting from the ground.')),
    'destruction': dict(els=['arcane'], light=dict(act='cast', cast=True, roles=['impact'], what='A glintstone bolt from the wand, bursting on the one it hits.'),
                        heavy=dict(act='cast', cast=True, roles=['burst', 'ground', 'above'], what='A charged cast: the wand\'s magicka unleashed where you aim.')),
    'spellsword': dict(els=['fire', 'frost', 'shock'], light=dict(act='swing', roles=['impact'], what='A rapier thrust carrying the spell in your other hand.'),
                       heavy=dict(act='slam', roles=['burst', 'ground', 'above'], what='A spell-charged heavy: your other hand\'s element bursting from the blow.')),
}

EXTRA_ELEMENTS = [
    ('spirit', 'Spirit', '#dfe8ff', 'Ghost light, souls and the dead raised: pale, cold, whispering.'),
    ('wind', 'Wind and Thu\'um', '#8fcbe8', 'The Voice: swirling sky-blue gusts, racing streaks, dust and leaves torn up - shockwaves that throw the dead. (Remade: your note.)'),
]

# ------------------------------------------------------------------------------------------------ the builds (history)
def builds():
    out = []
    log = subprocess.run(['git', '-C', str(ROOT), 'log', '--since=2026-11-24 00:00 -0500', '--format=%h|%ad|%s', '--date=format:%Y-%m-%d %H:%M'], capture_output=True, text=True).stdout
    for line in log.splitlines():
        sha, when, subj = line.split('|', 2)
        if 'skip ci' in subj or subj.startswith('Workshop') or subj.startswith('The mod now'):
            continue
        out.append(dict(sha=sha, when=when, what=subj.replace('\n', ' ')[:400]))
    return out

INSTALLS = {   # (each build installed on Noah's PC, and when)
    '3edd0b7': '2026-11-27 03:20',
    '3938d82': '2026-11-27 01:10',
    'f696bbb': '2026-11-26 20:00', '15cfd57': '2026-11-26 17:20', 'a4584e6': '2026-11-26 15:01', '5865977': '2026-11-26 01:30',
}


def class_looks():
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    import weaponlooks
    for c in CLASSES:
        v = weaponlooks.W[c['lk']]
        c['looks'] = [dict(id=f'o{i + 1}', name=x['name'], desc=x['desc']) for i, x in enumerate(v[1])]


def wonder_looks():
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    import weaponlooks
    lk = {'soulcaller': 'soulcaller', 'javelin': 'javelin', 'mutagen': 'flaskweap_philosopher', 'slayer': 'slayer', 'bane': 'fang'}
    for w in WONDERS:
        w['looks'] = [dict(id=f'o{i + 1}', name=x['name'], desc=x['desc']) for i, x in enumerate(weaponlooks.W[lk[w['id']]][1])]


def main():
    class_looks()
    wonder_looks()
    ids = [f['id'] for f in FEATURES]
    assert len(ids) == len(set(ids)), 'duplicate feature ids'
    sys_ids = {s[0] for s in SYSTEMS}
    for f in FEATURES:
        assert f['sys'] in sys_ids, f['id']
        if f['st'] == 'ready':
            assert f['how'] and f['look'], 'a ready feature needs how and look: ' + f['id']
    data = dict(installed=INSTALLED, systems=[dict(id=a, name=b, about=c) for a, b, c in SYSTEMS], features=FEATURES,
                decisions=[dict(d, opts=[dict(id=a, name=b, about=c) for a, b, c in d['opts']]) for d in DECISIONS],
                decided=DECIDED,
                builds=builds(), installs=INSTALLS, bible=BIBLE,
                elements=[dict(id=a, name=b, col=c, about=d) for a, b, c, d in ELEMENTS], kit=[dict(name=a, about=b) for a, b in KIT_PARTS],
                classes=CLASSES, tablet=TABLET, tabnew=TABLET_NEW, kits=element_kits(), abil=CLASS_ABIL, tabnew2=TABLET_NEW2, rarity=RARITY_DEFAULT, odds=ODDS_DEFAULT, rolls=ROLLS, lore=lore(), notelog=NOTE_LOG, picklog=PICK_LOG, moves=CLASS_MOVES, wonders=WONDERS)
    for k in ('modstack', 'nexus', 'modnow'):
        data[k] = json.loads((ROOT / 'docs' / 'workshop' / f'{k}.json').read_text())
    (HERE / 'data.json').write_text(json.dumps(data, ensure_ascii=False, indent=1))
    print('data.json:', len(FEATURES), 'features,', sum(f['st'] == 'ready' for f in FEATURES), 'ready to test,', len(DECISIONS), 'decisions,', len(data['builds']), 'builds')


if __name__ == '__main__':
    main()
