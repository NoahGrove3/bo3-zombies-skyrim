"""The workshop doc's other tabs, as data (Noah, 2026-11-25: "Treat this document as the master document for our mod, where we
can tinker, build, preview, test" - hit feedback, the dead's deaths, the Echo Altar's trials, big nights, the issues and the
master test list). Each option's first is what the game does now; 'rec' is my recommendation.

    python tools/workshopdata.py      -> docs/workshop/{feel,deaths,trials,perf,issues,tests_master}.json
"""
import json, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
W = ROOT / 'docs/workshop'
CURSE = json.load(open(ROOT / 'sheets/curse.json'))

# ---------------------------------------------------------------- hit feel: hit-stop and camera shake by weapon weight
P = lambda sl, sh, k, kk: {'stopL': sl, 'stopH': sh, 'shakeH': k, 'shakeK': kk}
FEEL = {'classes': [
    {'id': 'light', 'name': 'Daggers, claws and rapiers', 'who': 'Sacrifice Knife, the three claws, Frostmourn Rapier, the Spellbound Rapier', 'item': 'cinderclaws', 'key': 'cinderclaws_hit',
     'opts': [['a1', 'As it is now', 'No hit-stop, no shake: the blade passes through.', P(0, 0, 0, 0)], ['a2', 'Quick bite', 'A tiny catch on every hit, a little more on a heavy.', P(25, 60, 20, 10)],
              ['a3', 'Subtle', 'Barely there: felt more than seen.', P(12, 35, 10, 0)], ['a4', 'Brutal', 'Every stab catches hard.', P(45, 110, 45, 20)]], 'rec': 'a2',
     'why': 'Fast weapons hit often: a catch of 25 ms keeps them quick but lets each hit land.'},
    {'id': 'medium', 'name': 'Swords and maces', 'who': 'Mistblade, Windcutter, Storm Blade, Grief Blade, Gravebell Mace', 'item': 'otar', 'key': 'otar_hit',
     'opts': [['a1', 'As it is now', 'No hit-stop, no shake.', P(0, 0, 0, 0)], ['a2', 'Solid', 'A short catch on each hit; a heavy stops a beat and shakes a little.', P(35, 85, 30, 10)],
              ['a3', 'Subtle', 'Half of that.', P(18, 45, 15, 0)], ['a4', 'Brutal', 'Big catches and a hard shake on heavies.', P(60, 140, 60, 20)]], 'rec': 'a2',
     'why': 'The middle weight: you should feel it bite, but never feel slowed down.'},
    {'id': 'heavy', 'name': 'Greatswords, axes, hammers, scythes and halberds', 'who': 'Keyhammer, Glacier Greatsword, Sealbreaker, Arcane Maul, every scythe, Riverstone Halberd, Wyrmspine Pike, Eclipse Edge, Dragon Slayer, Alduin\'s Bane', 'item': 'zahkriisos', 'key': 'zahkriisos_heavy',
     'opts': [['a1', 'As it is now', 'No hit-stop, no shake.', P(0, 0, 0, 0)], ['a2', 'Weighty', 'Each blow stops a beat; a heavy stops longer and shakes the screen.', P(50, 110, 45, 15)],
              ['a3', 'Subtle', 'Half of that.', P(25, 60, 22, 0)], ['a4', 'Brutal', 'A crunch on every blow.', P(80, 180, 75, 25)]], 'rec': 'a2',
     'why': 'Big weapons should feel big: the stop sells the weight, the shake sells the power attack.'},
    {'id': 'magic', 'name': 'Wands, staffs and flasks', 'who': 'Carian Wand, Radiant Quarterstaff (its bolts), Soulcaller\'s and Mutagen Staffs, the Wandering Star, the flasks', 'item': 'wand', 'key': 'wand_heavy',
     'opts': [['a1', 'As it is now', 'No hit-stop, no shake.', P(0, 0, 0, 0)], ['a2', 'Impact only', 'No stop on casts; a small shake when a great bolt lands.', P(0, 0, 25, 10)],
              ['a3', 'Subtle', 'A smaller shake.', P(0, 0, 12, 0)], ['a4', 'Punchy', 'A short stop and a shake on each great bolt.', P(20, 60, 45, 15)]], 'rec': 'a2',
     'why': 'Spells hit far from you: stopping the screen on them feels wrong, but a shake on the big ones lands.'},
], 'marks': [   # hit markers and kill feedback (src/Events.cpp, src/Hud.cpp): a few things to keep, change or turn off
    {'id': 'mark', 'name': 'The hit marker', 'now': 'A white mark at the crosshair on a hit, gold on a headshot, red and bigger on a kill, with a bright tick.',
     'opts': [['keep', 'Keep it as it is'], ['quiet', 'Keep the mark, a softer tick'], ['nosound', 'The mark only, no tick'], ['off', 'Off']], 'rec': 'keep', 'snd': 'coa:sp_bright_0', 'pitch': 140},
    {'id': 'head', 'name': 'A headshot', 'now': 'A brighter tick, a white flash at the crosshair; a headshot kill bursts the head.',
     'opts': [['keep', 'Keep it as it is'], ['nofx', 'Keep the tick, no burst'], ['off', 'Off']], 'rec': 'keep', 'snd': 'coa:sp_bright_2', 'pitch': 130},
    {'id': 'kill', 'name': 'A kill', 'now': 'The body gives way (our sound) where it falls.',
     'opts': [['keep', 'Keep it as it is'], ['louder', 'Louder, so you hear it in a crowd'], ['element', 'Its death\'s own sound (The dead tab)'], ['off', 'Off']], 'rec': 'element', 'snd': 'coa:flesh_death', 'pitch': 100},
    {'id': 'last', 'name': 'The last of the night', 'now': 'Nothing special: the night ends when the last one falls.',
     'opts': [['keep', 'Keep it as it is'], ['slow', 'A moment of slow motion on the last kill'], ['flash', 'A flash and a low boom on the last kill']], 'rec': 'slow'},
]}

# ---------------------------------------------------------------- the dead's deaths, by what kills them
# a reaction: [when, kind(, colour)]; a part: [theme, role, when, scale, on the ground]
D = lambda i, n, d, rx, parts, snd, note='': {'id': i, 'name': n, 'desc': d, 'rx': rx, 'parts': parts, 'snd': snd, 'game': note}
DEATHS = [
    {'id': 'fire', 'name': 'Killed by fire', 'who': 'Cinderclaws, the Wyrmspine Pike and Javelin, the Dragon Slayer, Alduin\'s Bane, the Spellbound Rapier in fire, the Fire Flask, the Destruction fire spells', 'rec': 'a2',
     'why': 'Burning to ash leaves no bodies to cost frames on a big night, and you see what did it.',
     'opts': [D('a1', 'As it is now', 'They burn and fall, charred and smoking (the Frozen Electrocuted Combusted mod\'s burned body).', [[0.05, 'burn'], [0.45, 'die']], [['fire', 'core', 0.05, 0.8, 0]], ['coa:flesh_death']),
              D('a2', 'Burn to ash', 'They burn, fall, and crumble into a pile of glowing ash (Skyrim\'s own ash pile, as its fire perks leave).', [[0.05, 'burn'], [0.4, 'die'], [1.15, 'shrink']], [['fire', 'core', 0.05, 0.8, 0], ['fire', 'mist', 1.1, 0.9, 1]], ['WB_DestructionFire_Sound_Impact_Immolation', 'coa:flesh_death'], 'ash pile + body removed'),
              D('a3', 'Flare out', 'A burst of flame and they\'re gone.', [[0.05, 'burn'], [0.35, 'shrink']], [['fire', 'big', 0.3, 0.9, 0]], ['WB_DestructionFire_Sound_Explosion_FlashFireball']),
              D('a4', 'Embers on the wind', 'They fall and blow away as embers.', [[0.05, 'burn'], [0.4, 'die'], [1.0, 'shrink']], [['fire', 'core', 0.05, 0.7, 0], ['fire', 'spikes', 0.95, 0.9, 1]], ['WB_DestructionWizardfire_Sound_Explosion_Emberblast'])]},
    {'id': 'frost', 'name': 'Killed by frost', 'who': 'Glacier Greatsword, Rimewick Quarterstaff, Frostmourn Rapier, Eclipse Edge, the Spellbound Rapier in frost, the Frost Flask, the frost spells', 'rec': 'a2',
     'why': 'A frozen one shattering is the most satisfying death in the game - and the shards clear the body.',
     'opts': [D('a1', 'As it is now', 'They freeze and fall as a frozen body (Frozen Electrocuted Combusted).', [[0.05, 'freeze'], [0.6, 'die']], [['frost', 'core', 0.05, 0.8, 0]], ['MAGIcicleImpactBreakSD', 'coa:flesh_death']),
              D('a2', 'Freeze and shatter', 'Frozen solid, then they shatter into ice.', [[0.05, 'freeze'], [0.6, 'shatter']], [['frost', 'core', 0.05, 0.8, 0], ['frost', 'spikes', 0.6, 1.0, 0]], ['WB_DestructionFrost_Sound_Explosion_Shatter'], 'shards + body removed'),
              D('a3', 'Frozen statue', 'They stay standing, frozen, then topple.', [[0.05, 'freeze'], [1.1, 'knock']], [['frost', 'core', 0.05, 0.8, 0]], ['WB_DestructionFrost_Sound_Impact_Crystalize']),
              D('a4', 'Ice burst', 'They burst at once into a cloud of ice.', [[0.05, 'freeze'], [0.2, 'shatter']], [['frost', 'big', 0.2, 0.9, 0]], ['WB_DestructionFrost_Sound_Explosion_Cryoblast'])]},
    {'id': 'shock', 'name': 'Killed by lightning', 'who': 'Storm Blade, Stormtalons, the Spellbound Rapier in shock, the Shock Flask, the shock spells', 'rec': 'a2',
     'why': 'Skyrim\'s own disintegration: what a lightning death should look like, and no body left.',
     'opts': [D('a1', 'As it is now', 'They jolt and fall, blackened (Frozen Electrocuted Combusted).', [[0.05, 'shock'], [0.7, 'die']], [['shock', 'core', 0.05, 0.8, 0]], ['MAGShockImpactSD', 'coa:flesh_death']),
              D('a2', 'Disintegrate', 'They jolt, fall, and fall apart into ash with a crackle (Skyrim\'s own disintegration).', [[0.05, 'shock'], [0.5, 'die'], [0.95, 'shrink']], [['shock', 'core', 0.05, 0.8, 0], ['shock', 'mist', 0.9, 0.9, 1]], ['WB_DestructionShock_Sound_Explosion_LightningStrike_2C'], 'ash pile + body removed'),
              D('a3', 'Thrown by the jolt', 'The last jolt throws them.', [[0.05, 'shock'], [0.35, 'fling']], [['shock', 'big', 0.35, 0.8, 0]], ['WB_DestructionShock_Sound_Explosion_Thunderball']),
              D('a4', 'The last spark leaps on', 'As they fall, a spark leaps to the next one of the dead.', [[0.05, 'shock'], [0.5, 'die']], [['shock', 'core', 0.05, 0.8, 0], ['shock', 'spark', 0.55, 0.8, 0]], ['WB_DestructionShock_Sound_Explosion_ShockNova'], 'chains 1 more (a little damage)')]},
    {'id': 'sun', 'name': 'Killed by sun and holy light', 'who': 'Radiant Quarterstaff, Gravebell Mace, the Wandering Star, Eclipse Edge, Holy Water, the sun spells', 'rec': 'a2',
     'why': 'Undead burned away by holy light: a clean, readable death.',
     'opts': [D('a1', 'As it is now', 'They fall as any death.', [[0.05, 'flinch'], [0.4, 'die']], [['sun', 'spark', 0.05, 0.7, 0]], ['coa:flesh_death']),
              D('a2', 'Turned to light', 'They burn white and fade up into light.', [[0.05, 'burn', [1, 0.9, 0.6]], [0.35, 'die'], [0.85, 'shrink']], [['sun', 'core', 0.05, 0.7, 0], ['sun', 'pillar', 0.35, 0.5, 1]], ['MAGRestorationTurnUndeadFire2DSD'], 'body removed'),
              D('a3', 'Smitten', 'A flash of light hurls them.', [[0.05, 'burn', [1, 0.9, 0.6]], [0.1, 'fling']], [['sun', 'big', 0.1, 0.8, 0]], ['FXExplosionAurielsSunNear'])]},
    {'id': 'poison', 'name': 'Killed by poison and acid', 'who': 'Sacrifice Knife, the Acid Flask', 'rec': 'a2',
     'why': 'Melting into the ground fits acid, and clears the body.',
     'opts': [D('a1', 'As it is now', 'They fall as any death.', [[0.05, 'poison'], [0.5, 'die']], [['poison', 'spark', 0.05, 0.7, 0]], ['coa:flesh_death']),
              D('a2', 'Melt', 'They sag and melt into a hissing pool.', [[0.05, 'poison'], [0.4, 'die'], [1.1, 'shrink']], [['poison', 'core', 0.05, 0.7, 0], ['poison', 'mist', 1.0, 0.8, 1]], ['MAG_PoisonImpactSFX', 'coa:flesh_death'], 'body removed'),
              D('a3', 'Burst into a cloud', 'They pop into a cloud of poison that hurts the dead near them.', [[0.05, 'poison'], [0.35, 'shrink']], [['poison', 'big', 0.3, 0.8, 0]], ['MAG_PoisonExplosionSFX'], 'a small poison cloud')]},
    {'id': 'blood', 'name': 'Killed by blood magic', 'who': 'Grief Blade, Talons of Woe, Wyrmtooth and Bloodmoon Scythes', 'rec': 'a2',
     'why': 'A drained body shrivelling sells the drain; a burst is louder but messier.',
     'opts': [D('a1', 'As it is now', 'They fall as any death.', [[0.05, 'bleed'], [0.45, 'die']], [['blood', 'spark', 0.05, 0.7, 0]], ['coa:flesh_death']),
              D('a2', 'Drained husk', 'Their blood pulled out of them, they shrivel and fall.', [[0.05, 'drain'], [0.25, 'lift', [0.9, 0.1, 0.1]], [1.4, 'die']], [['blood', 'drain', 0.1, 0.8, 0], ['blood', 'mist', 1.4, 0.6, 1]], ['MAGAlterationVampireDrainOnHit', 'coa:flesh_death']),
              D('a3', 'Blood burst', 'They burst in a spray of blood.', [[0.05, 'bleed'], [0.2, 'shrink']], [['blood', 'big', 0.2, 0.7, 0]], ['coa:flesh_hit_1', 'WB_RestorationBlood_Sound_Release_Big'], 'body removed')]},
    {'id': 'shadow', 'name': 'Killed by shadow, spirit and arcane', 'who': 'Keyhammer, Mistblade, Nightcaller\'s Scythe, Arcane Maul, Silver Reaping Scythe, Bonereaper, Shadowflame Scythe, Carian Wand, Soulcaller\'s Staff, the Voidglass Flask', 'rec': 'a3',
     'why': 'The souls Alduin holds going free is the story of the mod: every kill a small soul rising.',
     'opts': [D('a1', 'As it is now', 'They fall as any death.', [[0.05, 'flinch'], [0.45, 'die']], [['shadow', 'spark', 0.05, 0.7, 0]], ['coa:flesh_death']),
              D('a2', 'Fade into shadow', 'They fall and fade into dark smoke.', [[0.05, 'flinch'], [0.35, 'die'], [0.95, 'shrink']], [['shadow', 'mist', 0.9, 0.8, 1]], ['WB_IllusionShadow_Sound_Explosion_Vanish'], 'body removed'),
              D('a3', 'Soul torn out', 'They fall, and their soul rises out of them.', [[0.05, 'flinch'], [0.35, 'die']], [['spirit', 'pillar', 0.45, 0.6, 1], ['spirit', 'core', 0.4, 0.8, 0]], ['QSTAlduinDeathSoulsASD', 'coa:flesh_death'])]},
    {'id': 'plain', 'name': 'Killed by a weapon\'s edge', 'who': 'Every melee kill with no element of its own', 'rec': 'a2',
     'why': 'A heavy that throws the body is the cheapest big feel-good in the game.',
     'opts': [D('a1', 'As it is now', 'They fall where they stand (Skyrim\'s ragdoll).', [[0.05, 'flinch'], [0.3, 'die']], [], ['coa:flesh_death']),
              D('a2', 'Heavies throw them', 'A power attack\'s kill throws the body back.', [[0.05, 'knock'], [0.12, 'fling']], [['earth', 'mist', 0.1, 0.6, 1]], ['coa:flesh_death', 'WPNImpactBlunt2HandVsFlesh']),
              D('a3', 'Crumble to dust', 'The Thriceborn are old dead: they fall and crumble to dust.', [[0.05, 'flinch'], [0.35, 'die'], [1.0, 'shrink']], [['earth', 'core', 0.95, 0.9, 1]], ['WB_RestorationUndead_Sound_Explosion_DustToDust'], 'body removed')]},
]

# ---------------------------------------------------------------- the trials (sheets/curse.json), with what each looks and sounds like
EV = lambda i, n, now, opts, rec='a1': {'id': i, 'name': n, 'now': now, 'opts': opts, 'rec': rec}
SO = lambda i, name, refs, desc='': {'id': i, 'name': name, 'snd': refs, 'desc': desc}
T = {t['id']: t for t in CURSE['trials']}
TRIALS = {'radius': {'light': CURSE['lightRadius'], 'whisper': CURSE['whisperRadius'], 'echo': CURSE['echoRadius'], 'hold': CURSE['holdRadius']},
          'diffs': ['Novice', 'Apprentice', 'Adept', 'Expert', 'Master', 'Legendary'],
          'common': [EV('start', 'A trial begins', 'Our rite-start swell.', [SO('a1', 'As it is now', ['coa:rite_start']), SO('a2', "Meridia's beam", ['QSTBeamMeridiaStatueRelease'], 'Skyrim: Meridia\'s statue waking'), SO('a3', 'Ethereal call', ['WB_ConjurationCommand_Sound_Explosion_EtherealCall']), SO('a4', 'Holy chord', ['MAGRestorationTurnUndeadFire2DSD'])], 'a2'),
                     EV('step', 'A step done', 'Our rite-step chime.', [SO('a1', 'As it is now', ['coa:rite_step']), SO('a2', 'Twinkles', ['WB_RestorationHeal_Sound_Release_MassiveTwinkles']), SO('a3', 'Light flare', ['WB_AlterationAlt_Sound_Release_IgniteLight'])]),
                     EV('freed', 'The city freed', 'The curse lifted: our swell, then the freed-city music.', [SO('a1', 'As it is now', ['coa:shout_learned']), SO('a2', "Auriel's sun", ['FXExplosionAurielsSunNear'], 'and the music after'), SO('a3', 'Souls set free', ['QSTAlduinDeathSoulsASD'])])],
          'trials': [
              {'id': 'restless', 'title': T['restless']['title'], 'goal': T['restless']['goal'], 'text': T['restless']['text'], 'n': T['restless']['n'], 'seconds': T['restless']['seconds'], 'radius': 'light',
               'look': {'ring': '#9fd0ff', 'soul': '#bfe6ff'}, 'shows': 'The altar\'s light on the ground round it; the dead slain inside it give up their souls, which rise as blue-white spirit flames into the stone.',
               'events': [EV('soul', 'A soul taken by the stone', 'A whisper and a thunk (ours, six takes).', [SO('a1', 'As it is now', ['coa:echo_tone_0']), SO('a2', 'Souls', ['QSTAlduinDeathSoulsASD'], 'and our thunk after'), SO('a3', 'Ghost wail', ['WB_ConjurationGhosts_Sound_Impact_Wail']), SO('a4', 'Spirit whoom', ['WB_ConjurationSpirit_Sound_Explosion_Whoom'])]),
                          EV('rise', 'A soul rising', 'A soft rising tone (ours).', [SO('a1', 'As it is now', ['coa:wisp_rise']), SO('a2', 'Ethereal call', ['WB_ConjurationCommand_Sound_Explosion_EtherealCall']), SO('a3', 'Wisp', ['NPCWispConsciousLP'])]),
                          EV('hum', 'Near the altar', 'The altar\'s hum (ours) while you stand close.', [SO('a1', 'As it is now', ['coa:shrine_hum']), SO('a2', 'Circle of Protection', ['MAGRestorationCircleOfProtection2DLPSD']), SO('a3', 'Soul trap', ['MAGMysticismSoulTrapActiveLPSD'])])]},
              {'id': 'whisper', 'title': T['whisper']['title'], 'goal': T['whisper']['goal'], 'text': T['whisper']['text'], 'n': T['whisper']['n'], 'seconds': T['whisper']['seconds'], 'radius': 'whisper',
               'look': {'ring': '#c8a8ff', 'soul': '#e0d0ff'}, 'shows': 'Felldir speaks the riddle from the altar; at the place it names, a ring marks where to hold, and the dead come.',
               'events': [EV('found', 'The place found', 'A step chime (ours).', [SO('a1', 'As it is now', ['coa:rite_step']), SO('a2', 'Ethereal call', ['WB_ConjurationCommand_Sound_Explosion_EtherealCall']), SO('a3', 'Ghost wail', ['WB_ConjurationGhosts_Sound_Impact_Wail'])], 'a2')]},
              {'id': 'echo', 'title': T['echo']['title'], 'goal': T['echo']['goal'], 'text': T['echo']['text'], 'n': T['echo']['n'], 'seconds': T['echo']['seconds'], 'radius': 'hold',
               'look': {'ring': '#ffd88a', 'soul': '#fff0c0'}, 'shows': 'The ring of light closes in on the stone over the trial\'s seconds while you stand inside it; step out and it slips back.',
               'events': [EV('sing', 'The light wakes', 'The altar\'s song (ours): every one of the dead in the city hears it.', [SO('a1', 'As it is now', ['coa:echo_sing']), SO('a2', 'Holy chord', ['MAGRestorationTurnUndeadFire2DSD']), SO('a3', "Auriel's shield", ['MAGAurielShieldBlastA'])]),
                          EV('inside', 'Standing inside', 'The rite\'s hum (ours), looping.', [SO('a1', 'As it is now', ['coa:rite_hum']), SO('a2', "Stendarr's Aura", ['MAGDestructionStendarrsAura2DLP']), SO('a3', 'Circle of Protection', ['MAGRestorationCircleOfProtection2DLPSD'])]),
                          EV('slip', 'Stepped out: it slips back', 'Our rite-reset falter.', [SO('a1', 'As it is now', ['coa:rite_reset']), SO('a2', 'Banish hiss', ['WB_ConjurationBanish_Sound_Release_Hiss']), SO('a3', 'Subsonic', ['WB_Illusion_Sound_Explosion_Subsonic'])])]},
              {'id': 'seals', 'title': T['seals']['title'], 'goal': T['seals']['goal'], 'text': T['seals']['text'], 'n': T['seals']['n'], 'seconds': T['seals']['seconds'], 'radius': 'echo',
               'look': {'ring': '#ff8a6a', 'soul': '#ffc0a0'}, 'shows': 'Three seals of dragon script at the city\'s great places, each guarded by an elite; strike each once its guard falls. The last calls the Dragon Priest.',
               'events': [EV('break', 'A seal broken', 'The Shatter shout\'s sound (ours).', [SO('a1', 'As it is now', ['coa:sp_bones']), SO('a2', 'Shatter', ['WB_DestructionFrost_Sound_Explosion_Shatter']), SO('a3', 'Glass jaw', ['WB_AlterationTelekinesis_Sound_Explosion_GlassJaw']), SO('a4', 'Unrelenting Force', ['VOCShoutImpactPushSD'])], 'a3')]}]}

# ---------------------------------------------------------------- big nights: what the night of Nov 24 measured (its CurseOfAlduin.log), and the knobs
PERF = {'measured': None, 'knobs': [
    {'id': 'hitches', 'name': 'The hitches first', 'now': 'On your Nov 24 night our own code cost 0.6 ms a frame on average - small - but a few frames took 40-85 ms (Classes, Gun, Director, Shops): a stutter you can feel.',
     'opts': [['fix', 'Find and fix the hitches before anything else'], ['later', 'Later']], 'rec': 'fix', 'why': 'A steady 80 fps with a stutter every minute feels worse than 70 without one.'},
    {'id': 'alive', 'name': 'The dead alive at once', 'now': '64 (and up to half again for a moment in a big wave).', 'opts': [['48', '48'], ['64', '64 (now)'], ['80', '80']], 'rec': '64', 'why': 'You peaked at 29 bodies near you on Nov 24: the cap isn\'t what costs.'},
    {'id': 'ai', 'name': 'The dead thinking at full speed', 'now': '24 near you, 6 more per player in co-op; the rest move simply.', 'opts': [['18', '18'], ['24', '24 (now)'], ['32', '32']], 'rec': '24', 'why': 'Skyrim\'s AI is its biggest cost; 24 keeps a crowd alive without the frame rate falling.'},
    {'id': 'corpses', 'name': 'Bodies left on the ground', 'now': '18, the oldest crumbling as new ones fall.', 'opts': [['8', '8'], ['12', '12'], ['18', '18 (now)'], ['24', '24']], 'rec': '12', 'why': 'Bodies cost physics; with ash, shatter and melt deaths (The dead tab) far fewer are left anyway.'},
    {'id': 'fx', 'name': 'Our effects at once', 'now': '32 bursts at a time; any more wait.', 'opts': [['16', '16'], ['24', '24'], ['32', '32 (now)'], ['48', '48']], 'rec': '32', 'why': 'Enough for every heavy in a crowd; more and the glow piles up into white.'},
    {'id': 'detail', 'name': 'Effects on a big night', 'now': 'Every effect at full detail, however many are going.', 'opts': [['full', 'Full detail always (now)'], ['auto', 'Fewer particles past 30 of the dead near you'], ['light', 'Always lighter']], 'rec': 'auto', 'why': 'You won\'t see the difference in a crowd; your frame rate will.'},
    {'id': 'lights', 'name': 'Light cast by effects', 'now': 'Each effect lights the ground and the dead (Skyrim\'s own light on its effects).', 'opts': [['all', 'Every effect (now)'], ['big', 'Only the big ones (heavies, spells) on a big night'], ['none', 'None']], 'rec': 'big', 'why': 'Skyrim draws only a few lights at once; dozens of small ones flicker in and out.'},
    {'id': 'voices', 'name': 'The horde\'s sounds', 'now': 'A budget of the dead\'s sounds at once; your own sounds never wait.', 'opts': [['keep', 'Keep (now)'], ['less', 'Fewer of the dead\'s groans in a crowd']], 'rec': 'keep', 'why': 'It already works: your hits are always heard.'},
]}

# ---------------------------------------------------------------- the issues: known problems, mine and in game
ISSUES = [
    ['bolts', 'The bolts in flight look pale in the previews', 'The fire bolts (Dragon Slayer, Spellbound Rapier, Shadowflame Scythe) look washed out in the doc while flying; the game draws them as Skyrim does. I tried three fixes; not solved yet.', 'open', 'doc'],
    ['hitch', 'Stutters of 40-85 ms on a night', 'Your Nov 24 log: Classes 85 ms, Gun 75 ms, Director 59 ms, Shops 42 ms at worst. Rare, but felt.', 'open', 'game'],
    ['mem', 'Little memory left late in a night', 'At the end of your Nov 24 night Windows had 768 MB free of 32 GB (Skyrim at 12 GB). To watch: the vanishing-dead hunt found this before.', 'watch', 'game'],
    ['generic', 'Most abilities use Skyrim\'s generic blast sounds', 'The relics play a few shared blasts (the Firestorm\'s roar for many); the Tablet weapons\' abilities play none of their own. The Sounds tab is the fix.', 'doing', 'game'],
    ['swings', 'Light and heavy swings sound the same', 'Each weapon plays Skyrim\'s swing for its type on both. The Sounds tab gives the heavy its own.', 'doing', 'game'],
    ['feel', 'No hit-stop or shake on melee hits', 'Hits pass through; shakes only on big events. The Hit feel tab is the fix.', 'doing', 'game'],
    ['deaths', 'One death for every kill', 'Fire, frost and shock kills use the FEC mod\'s bodies; everything else falls the same. The dead tab is the fix.', 'doing', 'game'],
    ['dpscythe', 'The Dragon Priest scythe\'s blade', 'Now coming from the head of the staff, as you asked; needs your OK in the Scythes tab.', 'check', 'game'],
    ['notplayed', 'Nothing since Nov 24 evening played yet', 'The last build your game ran was 6a0f8a0 (Nov 24, 7 pm). Everything in the Test list is still to see in game.', 'open', 'test'],
]

# ---------------------------------------------------------------- the master test list: everything not yet seen in game (since 6a0f8a0)
TESTS = [
    ['When this doc is final (the next install)', [
        {'id': 'looks', 't': 'Weapon looks in the game', 'how': 'Draw each relic, Tablet weapon, wonder and class weapon at each tier.', 'look': 'Each wears the look and colour you picked here, matching its preview: light on its own edges and faces, coming to a point at the tip.'},
        {'id': 'abilfx', 't': 'Your ability picks in the game', 'how': 'Use each weapon\'s hits, heavies and specials on a crowd.', 'look': 'Each plays the option you picked, in your colour, at your size, brightness, speed, linger and density, lighting the ground as set.'},
        {'id': 'abilarea', 't': 'Area of effect', 'how': 'Use an area ability you set wider or narrower than 100%.', 'look': 'It reaches the dead as far as it looks: wider reaches further, narrower less.'},
        {'id': 'sounds', 't': 'Your sound picks', 'how': 'Draw, swing light and heavy, and use each ability.', 'look': 'Each plays the sound you picked, at your volume and pitch, with its layer; a hundred hits don\'t sound the same.'},
        {'id': 'feel', 't': 'Hit feel', 'how': 'Hit the dead with a dagger, a sword and a greatsword; power-attack with each.', 'look': 'A short catch on each hit, longer on a heavy, a shake on heavies, as set per weight.'},
        {'id': 'deaths', 't': 'The deaths', 'how': 'Kill the dead with fire, frost, lightning, sun, poison, blood and shadow weapons, and with a plain heavy.', 'look': 'Each dies as you picked: ash, shatter, disintegrate, light, melt, husk, soul - with its sound.'},
        {'id': 'trialsfix', 't': 'The trials as edited', 'how': 'Run each of the four trials in a city.', 'look': 'Your words, numbers, ring colour and sounds.'},
        {'id': 'perf', 't': 'A big night', 'how': 'Play a night past round 15 with a crowd.', 'look': 'No stutters; the frame rate steady with 30+ of the dead near you.'}]],
    ['In builds since the last you played (Nov 24, 6a0f8a0)', [
        {'id': 'scygame', 't': 'Our own scythes', 'how': 'Draw and swing each of the nine scythes.', 'look': 'Each its own scythe, 25% larger, the blade flush at the haft on Wyrmtooth, Silver Reaping and Bloodmoon; on your back when put away.'},
        {'id': 'scydp', 't': 'The Dragon Priest scythe', 'how': 'Draw the Shadowflame Scythe at Tier III and Ascended.', 'look': 'The blade coming from the head of the staff.'},
        {'id': 'realfx', 't': 'Abilities from real parts', 'how': 'Use a few relics\' hits and heavies.', 'look': 'Each its own effect made of Skyrim\'s, the DLCs\', Apocalypse\'s and Mysticism\'s effects - no flat sprites.'},
        {'id': 'chains', 'how': 'Power-attack a crowd with the Jailor\'s Keyhammer.', 't': 'Vahlok\'s chains', 'look': 'Real chain links catch the dead and haul them in.'},
        {'id': 'banewind', 't': "Alduin's Bane heavy", 'how': "Power-attack a crowd with Alduin's Bane.", 'look': 'A white wind blast round you; the dead within 10 m hurled far.'},
        {'id': 'sunwhite', 't': 'The Radiant Quarterstaff heavy in white', 'how': 'As the Summoner at Tier II and III, power-attack the dead.', 'look': 'A white bolt from the sky, a white burst, Auriel\'s sunbeam in white, white motes and ring.'},
        {'id': 'portal', 't': "Summons out of Skyrim's portal", 'how': "Hold the Soulcaller's Staff until its wolves come; cast any conjuration.", 'look': "Each comes out of Skyrim's own conjuring portal."},
        {'id': 'spellsown', 't': 'The class spells their own', 'how': 'Cast a few Destruction spells and Spellsword touches.', 'look': 'Each plays its own mod\'s impact, nothing of ours over it.'},
        {'id': 'tips', 't': 'Looks come to a point', 'how': 'Draw any blade.', 'look': 'No hard cut-offs: the light comes to a point at the tip and ends in the weapon\'s shape; no floating motes; no old pillars of colour on the relics.'},
        {'id': 'staffheads', 't': 'Staffs glow on their heads', 'how': 'Draw the Radiant Quarterstaff at each tier and the Rimewick Quarterstaff.', 'look': 'Only the two heads glow.'},
        {'id': 'clawsboth', 't': 'Claws in both hands', 'how': 'Equip Stormtalons, Talons of Woe and Cinderclaws.', 'look': 'The same look on both hands, lined up with the blades.'},
        {'id': 'javhand', 't': 'The Wyrmspine Javelin', 'how': 'Tap to jab, hold and let go to throw.', 'look': 'Held in the hand, a human overhand throw along your aim, a trail you can follow, its bounce.'},
        {'id': 'holdlight', 't': 'Hold the Light', 'how': 'Run the third trial in a city at night.', 'look': 'The ring of real light closes on the stone while you stand in it; step out and it slips back.'},
        {'id': 'souls', 't': 'Souls in Lay the Restless to Rest', 'how': 'Kill the dead in the altar\'s light.', 'look': 'Each soul a white-fire spirit flame rising into the stone, with a whisper and a thunk.'},
        {'id': 'voicesout', 't': 'Class voices out', 'how': 'Play a night as any class.', 'look': 'No class voice lines; Felldir\'s altar voice still there.'},
        {'id': 'ground', 't': 'Floating trees and rocks', 'how': 'Travel round a few cities.', 'look': 'Nothing floating over nothing.'},
        {'id': 'newchar', 't': 'A new character', 'how': 'Start a new game.', 'look': 'One smooth start with no sound under the loading screen; menu music from launch to the menu.'}]],
]

if __name__ == '__main__':
    for name, obj in (('feel', FEEL), ('deaths', DEATHS), ('trials', TRIALS), ('perf', PERF), ('issues', ISSUES), ('tests_master', TESTS)):
        (W / f'{name}.json').write_text(json.dumps(obj, indent=1, ensure_ascii=False))
    print('wrote', ', '.join(['feel', 'deaths', 'trials', 'perf', 'issues', 'tests_master']))
