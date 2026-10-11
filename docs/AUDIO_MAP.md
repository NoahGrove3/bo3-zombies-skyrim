# Undying Nights: audio inventory and assignment map

Generated from `sheets/sfx.json` (clips cut from the supplied library by `tools/sfx.py`) and `sheets/sounds.json` (every sound the mod plays, rebuilt by `tools/soundmap.py`). No Skyrim sound effects are referenced.

## 1. Supplied files (31 MP3 + the spec PDF)

| Alias | File | Clips cut from it |
|---|---|---|
| firebreath | ES_Creatures, Dragon, Dragon, Vocalizations, Fire Breathing 06 - Epidemic Sound.mp3 | boss_breath_wind, boss_breath |
| dgrowl | ES_Creatures, Dragon, Dragon, Vocalizations, Growls 01 - Epidemic Sound.mp3 | boss_growl |
| screech | ES_Creatures, Ethereal, Poltergeist, Screech, Aggressive, Attack, Horror 02 - Epidemic Sound.mp3 | en_wail_0, en_wail_1, en_hag_death |
| zf04 | ES_Creatures, Humanoid, Zombie, Female, Snarl, Groan 04 - Epidemic Sound.mp3 | en_runner_idle_0, en_runner_idle_1 |
| zf08 | ES_Creatures, Humanoid, Zombie, Female, Snarl, Groan 08 - Epidemic Sound.mp3 | en_runner_idle_2, en_runner_idle_3, en_runner_attack, en_runner_death |
| roar | ES_Creatures, Monster, Monster, Roar - Epidemic Sound.mp3 | en_roar, en_roar_hi |
| demon | ES_Demon, Evil Demonic Growl 04 - Epidemic Sound.mp3 | en_demon, en_demon_low |
| bighit | ES_Designed, Impact, Cinematic, Scary, Horror, Creepy, Unpleasant, Fast, Big Hit - Epidemic Sound.mp3 | boss_land |
| stinger | ES_Designed, Stinger, Cinematic, Hit, Impact, Big, Dark, Creepy, Horror 02 - Epidemic Sound.mp3 | boss_arrive, rite_start |
| ghosts | ES_Designed, Vocal, Horror, Ghosts, Airy Roars 06 - Epidemic Sound.mp3 | en_ghost_0, en_ghost_1, en_ghost_2, night_fog |
| menu | ES_Echoes of the Unmasked - DEX 1200.mp3 | mus_menu |
| rubble | ES_Explosions, Real, Heavy Blasts, Stone Rubble Debris - Epidemic Sound.mp3 | sp_bones, en_burst, boss_quake_0, boss_quake_1, boss_quake_2, night_ash |
| ignite | ES_Fire, Ignite, Large Ignition, Start Fire x2 - Epidemic Sound.mp3 | sp_ignite, sp_ignite_small |
| darkhit | ES_Hit, Big, Powerful, Dark, Suspense, Cinematic 03 - Epidemic Sound.mp3 | sp_void_hit, champ_arrive |
| portal | ES_Magic, Angelic, Portal, Open, Spell, Cast, Holy - Epidemic Sound.mp3 | sp_holy_loop, sp_holy_burst, coffer_charge |
| beam | ES_Magic, Elemental, Electric Magic Spell, Continuous, Lightning, Electrical Beam Emission - Epidemic Sound.mp3 | sp_beam_loop, sp_beam_loop2 |
| elcast | ES_Magic, Elemental, Electricity Magic, Spell, Cast, Long 01 - Epidemic Sound.mp3 | sp_shock_cast |
| icefast | ES_Magic, Elemental, Ice Magic Spell, Fast, Ice Blast, Cryomagic - Epidemic Sound.mp3 | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 |
| strike | ES_Magic, Elemental, Natural Magic, Lightning Strike, Electrical - Epidemic Sound.mp3 | sp_strike_0, sp_strike_1, sp_strike_2 |
| thunder | ES_Magic, Elemental, Thunderclap - Epidemic Sound.mp3 | sp_thunder |
| firewhoosh | ES_Magic, Elemental, Whoosh, Fire - Epidemic Sound.mp3 | sp_fire_0, sp_fire_1, sp_fire_2, sp_fire_3, sp_fire_4, sp_fire_5, sp_fire_6, sp_fire_7 |
| drone | ES_Magic, Evil, Dark, Magic, Death, Spell, Arcane, Drone, Tone - Epidemic Sound.mp3 | sp_drone_loop, sp_hex, rite_hum |
| wind | ES_Magic, Spell, Arial Summoning, Wind Magic, Cast 01 - Epidemic Sound.mp3 | sp_wind_cast, sp_wind_unleash |
| coldwhoosh | ES_Magic, Spell, Cold, Whoosh 06 - Epidemic Sound.mp3 | sp_cold_whoosh |
| crunch | ES_Magic, Spell, Crunchy Spell - Epidemic Sound.mp3 | sp_crunch, sp_crunch_low |
| bright | ES_Magic, Spell, Magic, Spell, Bright, Mixed, Delayed - Epidemic Sound.mp3 | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3, coffer_tick, coffer_take, rite_step |
| ethereal | ES_Magic, Spell, Nostalgic Ethereal Stinger - Epidemic Sound.mp3 | sp_starfall, coffer_land, shout_learned |
| epicloop | ES_Musical, Loop, Epic, Exciting, Thriller, Brass, Strings 02 - Epidemic Sound.mp3 | mus_boss |
| logo | ES_Musical, Loop, Logo, Orchestra, Epic, Dreamy, Suspense - Epidemic Sound.mp3 | mus_freed |
| spooky | ES_Swooshes, Whoosh, Spooky Tone - Epidemic Sound.mp3 | champ_telegraph, rite_reset |
| ui | ES_User Interface, Beep, Button, Short, Chord, Soft, Low - Epidemic Sound.mp3 | ui_open, ui_move, ui_close, ui_select |

## 2. Clips and what plays them

| Clip | From | Use (sheet) | Played by |
|---|---|---|---|
| mus_menu | menu | main menu music | music system |
| mus_boss | epicloop | Grimmvar fight music (looped while he lives) | music system |
| mus_freed | logo | a city freed (curse lifted) stinger | music system |
| sp_beam_loop | beam | shock charge hum (Storm Lash, Sky Sunder) | charge_frostlancer, charge_stormcaller, charge_skyforgearc |
| sp_beam_loop2 | beam | shock charge hum variation | charge_frostlancer, charge_stormcaller, charge_skyforgearc |
| sp_drone_loop | drone | dark charge hum (Hexfire, Void Orb); the ward/hold circle hum (pitched down) | charge_sidearm, charge_centurion, charge_spider, charge_drill, charge_steampump, charge_animunculi, charge_mammothlmg, charge_flamerepeater, charge_wabbajack, night_falls |
| sp_holy_loop | portal | holy charge hum (Sunfire, Starfall) | charge_triad, charge_marksman, charge_kagrenac, charge_longshot, charge_aetherium, charge_embermortar |
| sp_shock_cast | elcast | Storm Lash quick cast | cast_stormcaller, cast_skyforgearc |
| sp_strike_0 | strike | lightning impact 1 (Storm Lash, Sky Sunder, Starfall) | hit_longshot, hit_stormcaller, hit_skyforgearc |
| sp_strike_1 | strike | lightning impact 2 (Storm Lash, Sky Sunder, Starfall) | hit_longshot, hit_stormcaller, hit_skyforgearc |
| sp_strike_2 | strike | lightning impact 3 (Storm Lash, Sky Sunder, Starfall) | hit_longshot, hit_stormcaller, hit_skyforgearc |
| sp_thunder | thunder | Sky Sunder / Storm Lash unleash; Toll shout | boom_longshot, unleash_stormcaller, boom_stormcaller, unleash_skyforgearc, boom_skyforgearc, shout_toll, night_thunder |
| sp_ice_0 | icefast | ice blast 1 (Rimefang casts and impacts, Moonshard impact) | cast_triad, unleash_marksman, hit_marksman, boom_marksman, cast_frostlancer, hit_frostlancer, boom_frostlancer, shout_rime |
| sp_ice_1 | icefast | ice blast 2 (Rimefang casts and impacts, Moonshard impact) | cast_triad, unleash_marksman, hit_marksman, boom_marksman, cast_frostlancer, hit_frostlancer, boom_frostlancer, shout_rime |
| sp_ice_2 | icefast | ice blast 3 (Rimefang casts and impacts, Moonshard impact) | cast_triad, unleash_marksman, hit_marksman, boom_marksman, cast_frostlancer, hit_frostlancer, boom_frostlancer, shout_rime |
| sp_ice_3 | icefast | ice blast 4 (Rimefang casts and impacts, Moonshard impact) | cast_triad, unleash_marksman, hit_marksman, boom_marksman, cast_frostlancer, hit_frostlancer, boom_frostlancer, shout_rime |
| sp_ice_4 | icefast | ice blast 5 (Rimefang casts and impacts, Moonshard impact) | cast_triad, unleash_marksman, hit_marksman, boom_marksman, cast_frostlancer, hit_frostlancer, boom_frostlancer, shout_rime |
| sp_cold_whoosh | coldwhoosh | Moonshard / Rimefang cast whoosh | cast_marksman, unleash_frostlancer, hit_embermortar, charge_up, knife, claw_swing |
| sp_fire_0 | firewhoosh | fire whoosh 1 (Cinderstorm casts) | cast_drill, cast_animunculi, cast_aetherium, cast_flamerepeater |
| sp_fire_1 | firewhoosh | fire whoosh 2 (Cinderstorm casts) | cast_drill, cast_animunculi, cast_aetherium, cast_flamerepeater |
| sp_fire_2 | firewhoosh | fire whoosh 3 (Cinderstorm casts) | cast_drill, cast_animunculi, cast_aetherium, cast_flamerepeater |
| sp_fire_3 | firewhoosh | fire whoosh 4 (Cinderstorm casts) | cast_drill, cast_animunculi, cast_aetherium, cast_flamerepeater |
| sp_fire_4 | firewhoosh | fire whoosh 5 (Cinderstorm casts) | cast_drill, cast_animunculi, cast_aetherium, cast_flamerepeater |
| sp_fire_5 | firewhoosh | fire whoosh 6 (Cinderstorm casts) | cast_drill, cast_animunculi, cast_aetherium, cast_flamerepeater |
| sp_fire_6 | firewhoosh | fire whoosh 7 (Cinderstorm casts) | cast_drill, cast_animunculi, cast_aetherium, cast_flamerepeater |
| sp_fire_7 | firewhoosh | fire whoosh 8 (Cinderstorm casts) | cast_drill, cast_animunculi, cast_aetherium, cast_flamerepeater |
| sp_ignite | ignite | Cinderstorm unleash; Ember shout | unleash_flamerepeater, shout_ember, forge_upgrade, forge_ritual |
| sp_ignite_small | ignite | fire impact (Cinderstorm bolt lands) | hit_animunculi, hit_aetherium, hit_flamerepeater, ward_lit |
| sp_wind_cast | wind | Gale Burst quick cast | cast_embermortar |
| sp_wind_unleash | wind | Gale Burst unleash; Gale shout | unleash_embermortar, boom_embermortar, shout_gale |
| sp_crunch | crunch | Bone Spear / Bramble Spike cast | cast_centurion, cast_spider, cast_steampump, charge_fizzle |
| sp_crunch_low | crunch | Bone Spear / Bramble Spike impact | hit_centurion, hit_spider, hit_drill, hit_steampump, hit_mammothlmg, shout_mire, champ_chain |
| sp_holy_burst | portal | Sunfire unleash (the light breaks open) | unleash_aetherium, boom_aetherium |
| sp_starfall | ethereal | Starfall unleash (a star falls) | boom_kagrenac, unleash_longshot |
| sp_void_hit | darkhit | Void Orb unleash; Dread shout | boom_sidearm, boom_drill, unleash_mammothlmg, boom_mammothlmg, cast_heavy, unleash_hearth, unleash_gildergreen, unleash_lorkhan, unleash_arkay, unleash_mirestaff, unleash_magnus, unleash_wolfqueen, unleash_ysgramor, unleash_nightingale, unleash_nightmare, shout_dread |
| sp_hex | drone | Hexfire unleash (the doom mark) | unleash_animunculi, boom_animunculi, vo_necro_attack, vo_lich_attack |
| sp_bones | rubble | Bone Spear unleash (the ground breaks into bone) | unleash_spider, boom_spider, unleash_steampump, shout_shatter |
| sp_bright_0 | bright | arcane impact 1 (Grave Spark, Wabbajack, Mind Spike) | cast_sidearm, hit_sidearm, hit_triad, cast_kagrenac, hit_kagrenac, cast_longshot, cast_wabbajack, hit_wabbajack, hitmarker, cast_light, cast_rapid, cast_scatter, cast_thuumcannon, cast_apprentice, reload_mag, reload_shell, dry_click, shot_pistol, shot_smg, shot_ar, shot_lmg, shot_shotgun, shot_sniper, shot_ray, crossbow_fire, crossbow_reload, bow_nock, cast_hearth, cast_gildergreen, cast_lorkhan, cast_arkay, cast_mirestaff, cast_magnus, cast_wolfqueen, cast_ysgramor, cast_nightingale, cast_nightmare, brew_trollshide, brew_quicksilver, brew_twinstrike, brew_hearthfire, brew_windrunner, brew_hawkeye, brew_mammoth, brew_frostbite, brew_stormcrown |
| sp_bright_1 | bright | arcane impact 2 (Grave Spark, Wabbajack, Mind Spike) | cast_sidearm, hit_sidearm, hit_triad, cast_kagrenac, hit_kagrenac, cast_longshot, cast_wabbajack, hit_wabbajack, charge_ready, cast_light, cast_rapid, cast_scatter, cast_thuumcannon, cast_apprentice, reload_mag, reload_shell, dry_click, shot_pistol, shot_smg, shot_ar, shot_lmg, shot_shotgun, shot_sniper, shot_ray, crossbow_fire, crossbow_reload, bow_nock, cast_hearth, cast_gildergreen, cast_lorkhan, cast_arkay, cast_mirestaff, cast_magnus, cast_wolfqueen, cast_ysgramor, cast_nightingale, cast_nightmare, drink, brew_trollshide, brew_quicksilver, brew_twinstrike, brew_hearthfire, brew_windrunner, brew_hawkeye, brew_mammoth, brew_frostbite, brew_stormcrown |
| sp_bright_2 | bright | arcane impact 3 (Grave Spark, Wabbajack, Mind Spike) | cast_sidearm, hit_sidearm, hit_triad, cast_kagrenac, hit_kagrenac, cast_longshot, cast_wabbajack, hit_wabbajack, headshot, cast_light, cast_rapid, cast_scatter, cast_thuumcannon, cast_apprentice, reload_mag, reload_shell, dry_click, shot_pistol, shot_smg, shot_ar, shot_lmg, shot_shotgun, shot_sniper, shot_ray, crossbow_fire, crossbow_reload, bow_nock, cast_hearth, cast_gildergreen, cast_lorkhan, cast_arkay, cast_mirestaff, cast_magnus, cast_wolfqueen, cast_ysgramor, cast_nightingale, cast_nightmare, brew_trollshide, brew_quicksilver, brew_twinstrike, brew_hearthfire, brew_windrunner, brew_hawkeye, brew_mammoth, brew_frostbite, brew_stormcrown |
| sp_bright_3 | bright | arcane impact 4 (Grave Spark, Wabbajack, Mind Spike) | cast_sidearm, hit_sidearm, hit_triad, cast_kagrenac, hit_kagrenac, cast_longshot, cast_wabbajack, hit_wabbajack, cast_light, cast_rapid, cast_scatter, cast_thuumcannon, cast_apprentice, reload_mag, reload_shell, dry_click, shot_pistol, shot_smg, shot_ar, shot_lmg, shot_shotgun, shot_sniper, shot_ray, crossbow_fire, crossbow_reload, bow_nock, cast_hearth, cast_gildergreen, cast_lorkhan, cast_arkay, cast_mirestaff, cast_magnus, cast_wolfqueen, cast_ysgramor, cast_nightingale, cast_nightmare, powerup_spawn, brew_trollshide, brew_quicksilver, brew_twinstrike, brew_hearthfire, brew_windrunner, brew_hawkeye, brew_mammoth, brew_frostbite, brew_stormcrown |
| en_runner_idle_0 | zf04 | Runner (hooded woman) snarl 1 | vo_runner_idle, vo_runner_hurt |
| en_runner_idle_1 | zf04 | Runner (hooded woman) snarl 2 | vo_runner_idle, vo_runner_hurt |
| en_runner_idle_2 | zf08 | Runner (hooded woman) snarl 3 | vo_runner_idle, vo_runner_hurt |
| en_runner_idle_3 | zf08 | Runner (hooded woman) snarl 4 | vo_runner_idle, vo_runner_hurt |
| en_runner_attack | zf08 | Runner lunge snarl | vo_runner_attack, grab |
| en_runner_death | zf08 | Runner death groan | vo_runner_death |
| en_wail_0 | screech | Wailer scream 1 (calls the horde) | shout_wail, vo_wailer_attack, vo_wailer_hurt, wailer_scream |
| en_wail_1 | screech | Wailer scream 2 (calls the horde) | shout_wail, vo_wailer_attack, vo_wailer_hurt, wailer_scream |
| en_ghost_0 | ghosts | ghostly breath 1 (Wailer and Gloomstalker idle, Fog Night) | unleash_centurion, unleash_triad, unleash_dread, shout_shroud, vo_shambler_idle, vo_wailer_idle, vo_stalker_idle, vo_stalker_hurt, vo_necro_idle, vo_lich_idle, rise, champ_summon |
| en_ghost_1 | ghosts | ghostly breath 2 (Wailer and Gloomstalker idle, Fog Night) | unleash_centurion, unleash_triad, unleash_dread, shout_shroud, vo_shambler_idle, vo_wailer_idle, vo_stalker_idle, vo_stalker_hurt, vo_necro_idle, vo_lich_idle, rise, champ_summon |
| en_ghost_2 | ghosts | ghostly breath 3 (Wailer and Gloomstalker idle, Fog Night) | unleash_centurion, unleash_triad, unleash_dread, shout_shroud, vo_shambler_idle, vo_wailer_idle, vo_stalker_idle, vo_stalker_hurt, vo_necro_idle, vo_lich_idle, rise, champ_summon |
| en_hag_death | screech | Wailer death (a screech that dies away) | vo_wailer_death, vo_stalker_death |
| en_roar | roar | Bloater / bone knight roar (aggro) | vo_shambler_attack, vo_bloater_idle, vo_bloater_hurt, vo_reaper_idle, vo_knight_idle, champ_enrage |
| en_roar_hi | roar | abomination attack bellow | vo_bloater_attack |
| en_demon | demon | Lich King / Corrupted King growl; necromancer death | unleash_drill, boom_wabbajack, vo_shambler_hurt, vo_reaper_hurt, vo_necro_hurt, vo_necro_death, vo_knight_hurt, vo_king_idle, vo_king_hurt, vo_lich_hurt |
| en_demon_low | demon | champion death | vo_shambler_death, vo_bloater_death, vo_reaper_death, vo_knight_death, vo_king_death, vo_lich_death |
| en_burst | rubble | Bloater bursts (with the grave-rot) | boom_centurion, bloater_burst |
| boss_growl | dgrowl | Grimmvar growl (idle / arrival / enraged) | shout_roar, shout_dragon, boss_growl, boss_claw_wind, boss_leap_wind, boss_death |
| boss_breath_wind | firebreath | Grimmvar breath telegraph (the in-breath and first roar) | boss_breath_wind |
| boss_breath | firebreath | Grimmvar fire breath | boss_breath |
| boss_land | bighit | Grimmvar leap landing / heavy slam impact | vo_knight_attack, vo_king_attack, boss_claw, boss_land |
| boss_quake_0 | rubble | eruption / ground burst 1 | boom_steampump, boom_flamerepeater, shout_quake, boss_quake, nuke |
| boss_quake_1 | rubble | eruption / ground burst 2 | boom_steampump, boom_flamerepeater, shout_quake, boss_quake, nuke |
| boss_quake_2 | rubble | eruption / ground burst 3 | boom_steampump, boom_flamerepeater, shout_quake, boss_quake, nuke |
| boss_arrive | stinger | Grimmvar arrives; Blood Moon begins | boss_arrive, hound_night, event_bloodmoon |
| champ_arrive | darkhit | a champion takes the field; Night of Iron begins | champ_arrive, night_start, round_start, event_champions |
| champ_telegraph | spooky | champion ability wind-up (chain hook, scream, summon) | unleash_sidearm, boom_triad, unleash_kagrenac, cast_mammothlmg, unleash_wabbajack, vo_stalker_attack, vo_reaper_attack, boss_quake_wind, champ_telegraph |
| ui_open | ui | shrine menu open | coffer_open |
| ui_move | ui | choose a brew | ui_move |
| ui_close | ui | shrine menu close; main menu back | mana_empty, coffer_cheese, coffer_skull, ui_close |
| ui_select | ui | main menu select | ui_select |
| coffer_charge | portal | the shrine gathers power (roll begins) | coffer_charge |
| coffer_tick | bright | a spell passes over the crown (cycling tick) | coffer_spin |
| coffer_land | ethereal | the shrine lands on a spell | night_end, round_end, coffer_land |
| coffer_take | bright | take the spell from the crown | powerup_grab, coffer_take, buy |
| rite_hum | drone | ward / hold circle hum (loop) | rite_hum |
| rite_reset | spooky | the dead broke the circle (timer reset) | rite_reset, rite_fail |
| rite_step | bright | rite step done | quest_step |
| rite_start | stinger | rite begins | quest_start |
| night_fog | ghosts | Fog Night begins | event_fog |
| night_ash | rubble | Ash Storm begins (distant blasts) | event_ash |
| shout_learned | ethereal | a Thu'um learned | curse_lifted, shout_learned |

## 3. Sounds (SNDR) and their clips

| Sound | 2D/3D | Loop | Pitch | Clips | Use |
|---|---|---|---|---|---|
| cast_sidearm | 2d |  | +15% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | sidearm: quick cast |
| charge_sidearm | 2d | yes | +30% | sp_drone_loop | sidearm: charge hum (loops while both hands gather) |
| unleash_sidearm | 2d |  | +20% | champ_telegraph | sidearm: signature release |
| hit_sidearm | 3d |  | +5% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | sidearm: bolt impact where it lands |
| boom_sidearm | 3d |  | +25% | sp_void_hit | sidearm: gathered burst at the target |
| cast_centurion | 2d |  | +10% | sp_crunch | centurion: quick cast |
| charge_centurion | 2d | yes | +10% | sp_drone_loop | centurion: charge hum (loops while both hands gather) |
| unleash_centurion | 2d |  | +10% | en_ghost_0, en_ghost_1, en_ghost_2 | centurion: signature release |
| hit_centurion | 3d |  | +15% | sp_crunch_low | centurion: bolt impact where it lands |
| boom_centurion | 3d |  | +10% | en_burst | centurion: gathered burst at the target |
| cast_triad | 2d |  | +25% | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 | triad: quick cast |
| charge_triad | 2d | yes | +10% | sp_holy_loop | triad: charge hum (loops while both hands gather) |
| unleash_triad | 2d |  | +0% | en_ghost_0, en_ghost_1, en_ghost_2 | triad: signature release |
| hit_triad | 3d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | triad: bolt impact where it lands |
| boom_triad | 3d |  | -10% | champ_telegraph | triad: gathered burst at the target |
| cast_spider | 2d |  | +0% | sp_crunch | spider: quick cast |
| charge_spider | 2d | yes | -10% | sp_drone_loop | spider: charge hum (loops while both hands gather) |
| unleash_spider | 2d |  | +15% | sp_bones | spider: signature release |
| hit_spider | 3d |  | +0% | sp_crunch_low | spider: bolt impact where it lands |
| boom_spider | 3d |  | +10% | sp_bones | spider: gathered burst at the target |
| cast_drill | 2d |  | -25% | sp_fire_0, sp_fire_1, sp_fire_2, sp_fire_3, sp_fire_4, sp_fire_5, sp_fire_6, sp_fire_7 | drill: quick cast |
| charge_drill | 2d | yes | -20% | sp_drone_loop | drill: charge hum (loops while both hands gather) |
| unleash_drill | 2d |  | +10% | en_demon | drill: signature release |
| hit_drill | 3d |  | -15% | sp_crunch_low | drill: bolt impact where it lands |
| boom_drill | 3d |  | -10% | sp_void_hit | drill: gathered burst at the target |
| cast_steampump | 2d |  | -15% | sp_crunch | steampump: quick cast |
| charge_steampump | 2d | yes | -25% | sp_drone_loop | steampump: charge hum (loops while both hands gather) |
| unleash_steampump | 2d |  | +0% | sp_bones | steampump: signature release |
| hit_steampump | 3d |  | -25% | sp_crunch_low | steampump: bolt impact where it lands |
| boom_steampump | 3d |  | +15% | boss_quake_0, boss_quake_1, boss_quake_2 | steampump: gathered burst at the target |
| cast_marksman | 2d |  | +10% | sp_cold_whoosh | marksman: quick cast |
| charge_marksman | 2d | yes | -15% | sp_holy_loop | marksman: charge hum (loops while both hands gather) |
| unleash_marksman | 2d |  | -10% | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 | marksman: signature release |
| hit_marksman | 3d |  | +10% | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 | marksman: bolt impact where it lands |
| boom_marksman | 3d |  | -20% | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 | marksman: gathered burst at the target |
| cast_animunculi | 2d |  | +10% | sp_fire_0, sp_fire_1, sp_fire_2, sp_fire_3, sp_fire_4, sp_fire_5, sp_fire_6, sp_fire_7 | animunculi: quick cast |
| charge_animunculi | 2d | yes | +0% | sp_drone_loop | animunculi: charge hum (loops while both hands gather) |
| unleash_animunculi | 2d |  | +0% | sp_hex | animunculi: signature release |
| hit_animunculi | 3d |  | +15% | sp_ignite_small | animunculi: bolt impact where it lands |
| boom_animunculi | 3d |  | +10% | sp_hex | animunculi: gathered burst at the target |
| cast_kagrenac | 2d |  | +25% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | kagrenac: quick cast |
| charge_kagrenac | 2d | yes | +25% | sp_holy_loop | kagrenac: charge hum (loops while both hands gather) |
| unleash_kagrenac | 2d |  | +30% | champ_telegraph | kagrenac: signature release |
| hit_kagrenac | 3d |  | +20% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | kagrenac: bolt impact where it lands |
| boom_kagrenac | 3d |  | +20% | sp_starfall | kagrenac: gathered burst at the target |
| cast_mammothlmg | 2d |  | -20% | champ_telegraph | mammothlmg: quick cast |
| charge_mammothlmg | 2d | yes | -35% | sp_drone_loop | mammothlmg: charge hum (loops while both hands gather) |
| unleash_mammothlmg | 2d |  | +0% | sp_void_hit | mammothlmg: signature release |
| hit_mammothlmg | 3d |  | -30% | sp_crunch_low | mammothlmg: bolt impact where it lands |
| boom_mammothlmg | 3d |  | -20% | sp_void_hit | mammothlmg: gathered burst at the target |
| cast_longshot | 2d |  | -5% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | longshot: quick cast |
| charge_longshot | 2d | yes | +0% | sp_holy_loop | longshot: charge hum (loops while both hands gather) |
| unleash_longshot | 2d |  | +0% | sp_starfall | longshot: signature release |
| hit_longshot | 3d |  | +15% | sp_strike_0, sp_strike_1, sp_strike_2 | longshot: bolt impact where it lands |
| boom_longshot | 3d |  | +10% | sp_thunder | longshot: gathered burst at the target |
| cast_aetherium | 2d |  | +20% | sp_fire_0, sp_fire_1, sp_fire_2, sp_fire_3, sp_fire_4, sp_fire_5, sp_fire_6, sp_fire_7 | aetherium: quick cast |
| charge_aetherium | 2d | yes | +5% | sp_holy_loop | aetherium: charge hum (loops while both hands gather) |
| unleash_aetherium | 2d |  | +0% | sp_holy_burst | aetherium: signature release |
| hit_aetherium | 3d |  | +25% | sp_ignite_small | aetherium: bolt impact where it lands |
| boom_aetherium | 3d |  | +10% | sp_holy_burst | aetherium: gathered burst at the target |
| cast_flamerepeater | 2d |  | +0% | sp_fire_0, sp_fire_1, sp_fire_2, sp_fire_3, sp_fire_4, sp_fire_5, sp_fire_6, sp_fire_7 | flamerepeater: quick cast |
| charge_flamerepeater | 2d | yes | +15% | sp_drone_loop | flamerepeater: charge hum (loops while both hands gather) |
| unleash_flamerepeater | 2d |  | +0% | sp_ignite | flamerepeater: signature release |
| hit_flamerepeater | 3d |  | +0% | sp_ignite_small | flamerepeater: bolt impact where it lands |
| boom_flamerepeater | 3d |  | +10% | boss_quake_0, boss_quake_1, boss_quake_2 | flamerepeater: gathered burst at the target |
| cast_frostlancer | 2d |  | +0% | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 | frostlancer: quick cast |
| charge_frostlancer | 2d | yes | -30% | sp_beam_loop, sp_beam_loop2 | frostlancer: charge hum (loops while both hands gather) |
| unleash_frostlancer | 2d |  | -10% | sp_cold_whoosh | frostlancer: signature release |
| hit_frostlancer | 3d |  | -5% | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 | frostlancer: bolt impact where it lands |
| boom_frostlancer | 3d |  | -25% | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 | frostlancer: gathered burst at the target |
| cast_stormcaller | 2d |  | +0% | sp_shock_cast | stormcaller: quick cast |
| charge_stormcaller | 2d | yes | +0% | sp_beam_loop, sp_beam_loop2 | stormcaller: charge hum (loops while both hands gather) |
| unleash_stormcaller | 2d |  | +0% | sp_thunder | stormcaller: signature release |
| hit_stormcaller | 3d |  | +0% | sp_strike_0, sp_strike_1, sp_strike_2 | stormcaller: bolt impact where it lands |
| boom_stormcaller | 3d |  | -10% | sp_thunder | stormcaller: gathered burst at the target |
| cast_embermortar | 2d |  | +0% | sp_wind_cast | embermortar: quick cast |
| charge_embermortar | 2d | yes | -25% | sp_holy_loop | embermortar: charge hum (loops while both hands gather) |
| unleash_embermortar | 2d |  | +0% | sp_wind_unleash | embermortar: signature release |
| hit_embermortar | 3d |  | -20% | sp_cold_whoosh | embermortar: bolt impact where it lands |
| boom_embermortar | 3d |  | -15% | sp_wind_unleash | embermortar: gathered burst at the target |
| cast_wabbajack | 2d |  | -15% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | wabbajack: quick cast |
| charge_wabbajack | 2d | yes | +35% | sp_drone_loop | wabbajack: charge hum (loops while both hands gather) |
| unleash_wabbajack | 2d |  | +40% | champ_telegraph | wabbajack: signature release |
| hit_wabbajack | 3d |  | -20% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | wabbajack: bolt impact where it lands |
| boom_wabbajack | 3d |  | +30% | en_demon | wabbajack: gathered burst at the target |
| cast_skyforgearc | 2d |  | -10% | sp_shock_cast | skyforgearc: quick cast |
| charge_skyforgearc | 2d | yes | -10% | sp_beam_loop, sp_beam_loop2 | skyforgearc: charge hum (loops while both hands gather) |
| unleash_skyforgearc | 2d |  | -15% | sp_thunder | skyforgearc: signature release |
| hit_skyforgearc | 3d |  | -10% | sp_strike_0, sp_strike_1, sp_strike_2 | skyforgearc: bolt impact where it lands |
| boom_skyforgearc | 3d |  | -25% | sp_thunder | skyforgearc: gathered burst at the target |
| charge_ready | 2d |  | -15% | sp_bright_1 | full charge reached (both hands glow) |
| charge_up | 2d |  | +25% | sp_cold_whoosh | charge begins (hands come up) |
| charge_fizzle | 2d |  | +20% | sp_crunch | early release: the gathered power gutters out |
| mana_empty | 2d |  | -30% | ui_close | not enough magicka |
| cast_heavy | 2d |  | -10% | sp_void_hit | heavy cast (Black Hole collapse) |
| hitmarker | 2d |  | +40% | sp_bright_0 | a spell hit an undead |
| headshot | 2d |  | +30% | sp_bright_2 | a head hit |
| knife | 2d |  | -25% | sp_cold_whoosh | quick knife swing |
| cast_light | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| cast_rapid | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| cast_scatter | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| cast_thuumcannon | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| cast_apprentice | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| reload_mag | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| reload_shell | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| dry_click | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| shot_pistol | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| shot_smg | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| shot_ar | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| shot_lmg | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| shot_shotgun | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| shot_sniper | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| shot_ray | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| crossbow_fire | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| crossbow_reload | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| bow_nock | 2d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired weapon sound, kept so old references stay quiet and in theme) |
| cast_hearth | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_hearth | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_gildergreen | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_gildergreen | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_lorkhan | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_lorkhan | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_arkay | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_arkay | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_mirestaff | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_mirestaff | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_magnus | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_magnus | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_wolfqueen | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_wolfqueen | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_ysgramor | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_ysgramor | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_nightingale | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_nightingale | 2d |  | +0% | sp_void_hit | (retired relic) |
| cast_nightmare | 2d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | (retired relic) |
| unleash_nightmare | 2d |  | +0% | sp_void_hit | (retired relic) |
| unleash_dread | 2d |  | -20% | en_ghost_0, en_ghost_1, en_ghost_2 | (layer, retired) |
| shout_ember | 2d |  | -10% | sp_ignite | Thu'um: ember |
| shout_gale | 2d |  | -10% | sp_wind_unleash | Thu'um: gale |
| shout_wail | 2d |  | -15% | en_wail_0, en_wail_1 | Thu'um: wail |
| shout_rime | 2d |  | -30% | sp_ice_0, sp_ice_1, sp_ice_2, sp_ice_3, sp_ice_4 | Thu'um: rime |
| shout_quake | 2d |  | +0% | boss_quake_0, boss_quake_1, boss_quake_2 | Thu'um: quake |
| shout_shroud | 2d |  | -10% | en_ghost_0, en_ghost_1, en_ghost_2 | Thu'um: shroud |
| shout_toll | 2d |  | -20% | sp_thunder | Thu'um: toll |
| shout_mire | 2d |  | -20% | sp_crunch_low | Thu'um: mire |
| shout_dread | 2d |  | -15% | sp_void_hit | Thu'um: dread |
| shout_shatter | 2d |  | -10% | sp_bones | Thu'um: shatter |
| shout_roar | 2d |  | +10% | boss_growl | Thu'um: roar |
| shout_dragon | 2d |  | +0% | boss_growl | Thu'um: dragon |
| vo_shambler_idle | 3d |  | -45% | en_ghost_0, en_ghost_1, en_ghost_2 | shambler: idle |
| vo_shambler_attack | 3d |  | -25% | en_roar | shambler: attack |
| vo_shambler_hurt | 3d |  | -30% | en_demon | shambler: hurt |
| vo_shambler_death | 3d |  | -20% | en_demon_low | shambler: death |
| vo_runner_idle | 3d |  | +0% | en_runner_idle_0, en_runner_idle_1, en_runner_idle_2, en_runner_idle_3 | runner: idle |
| vo_runner_attack | 3d |  | +0% | en_runner_attack | runner: attack |
| vo_runner_hurt | 3d |  | +15% | en_runner_idle_0, en_runner_idle_1, en_runner_idle_2, en_runner_idle_3 | runner: hurt |
| vo_runner_death | 3d |  | +0% | en_runner_death | runner: death |
| vo_wailer_idle | 3d |  | +0% | en_ghost_0, en_ghost_1, en_ghost_2 | wailer: idle |
| vo_wailer_attack | 3d |  | +20% | en_wail_0, en_wail_1 | wailer: attack |
| vo_wailer_hurt | 3d |  | +35% | en_wail_0, en_wail_1 | wailer: hurt |
| vo_wailer_death | 3d |  | +0% | en_hag_death | wailer: death |
| vo_bloater_idle | 3d |  | -40% | en_roar | bloater: idle |
| vo_bloater_attack | 3d |  | +0% | en_roar_hi | bloater: attack |
| vo_bloater_hurt | 3d |  | -20% | en_roar | bloater: hurt |
| vo_bloater_death | 3d |  | -30% | en_demon_low | bloater: death |
| vo_stalker_idle | 3d |  | +15% | en_ghost_0, en_ghost_1, en_ghost_2 | stalker: idle |
| vo_stalker_attack | 3d |  | +20% | champ_telegraph | stalker: attack |
| vo_stalker_hurt | 3d |  | +30% | en_ghost_0, en_ghost_1, en_ghost_2 | stalker: hurt |
| vo_stalker_death | 3d |  | -20% | en_hag_death | stalker: death |
| vo_reaper_idle | 3d |  | -55% | en_roar | reaper: idle |
| vo_reaper_attack | 3d |  | -10% | champ_telegraph | reaper: attack |
| vo_reaper_hurt | 3d |  | -45% | en_demon | reaper: hurt |
| vo_reaper_death | 3d |  | -40% | en_demon_low | reaper: death |
| vo_necro_idle | 3d |  | -20% | en_ghost_0, en_ghost_1, en_ghost_2 | necro: idle |
| vo_necro_attack | 3d |  | +10% | sp_hex | necro: attack |
| vo_necro_hurt | 3d |  | +0% | en_demon | necro: hurt |
| vo_necro_death | 3d |  | -10% | en_demon | necro: death |
| vo_knight_idle | 3d |  | -50% | en_roar | knight: idle |
| vo_knight_attack | 3d |  | +20% | boss_land | knight: attack |
| vo_knight_hurt | 3d |  | -35% | en_demon | knight: hurt |
| vo_knight_death | 3d |  | -35% | en_demon_low | knight: death |
| vo_king_idle | 3d |  | -40% | en_demon | king: idle |
| vo_king_attack | 3d |  | +10% | boss_land | king: attack |
| vo_king_hurt | 3d |  | -25% | en_demon | king: hurt |
| vo_king_death | 3d |  | -15% | en_demon_low | king: death |
| vo_lich_idle | 3d |  | -30% | en_ghost_0, en_ghost_1, en_ghost_2 | lich: idle |
| vo_lich_attack | 3d |  | -10% | sp_hex | lich: attack |
| vo_lich_hurt | 3d |  | +15% | en_demon | lich: hurt |
| vo_lich_death | 3d |  | +0% | en_demon_low | lich: death |
| rise | 3d |  | -50% | en_ghost_0, en_ghost_1, en_ghost_2 | the dead climb out of the earth |
| claw_swing | 3d |  | -40% | sp_cold_whoosh | an undead swings (claws, scythe) |
| wailer_scream | 3d |  | +0% | en_wail_0, en_wail_1 | Wailer screams: the horde comes faster |
| bloater_burst | 3d |  | +0% | en_burst | Bloater bursts |
| grab | 3d |  | -10% | en_runner_attack | (retired grab) |
| boss_growl | 3d |  | +0% | boss_growl | Grimmvar growls (arrival, enraged) |
| boss_breath_wind | 3d |  | +0% | boss_breath_wind | Grimmvar breath telegraph (the in-breath) |
| boss_breath | 3d |  | +0% | boss_breath | Grimmvar fire breath |
| boss_claw_wind | 3d |  | +15% | boss_growl | Grimmvar raises his claws (telegraph) |
| boss_claw | 3d |  | +25% | boss_land | Grimmvar claw strike lands |
| boss_leap_wind | 3d |  | -10% | boss_growl | Grimmvar crouches to leap (telegraph) |
| boss_land | 3d |  | +0% | boss_land | Grimmvar lands from his leap |
| boss_quake | 3d |  | +0% | boss_quake_0, boss_quake_1, boss_quake_2 | eruption: the ground bursts |
| boss_quake_wind | 3d |  | -30% | champ_telegraph | eruption telegraph (the ground rumbles) |
| boss_death | 3d |  | -20% | boss_growl | Grimmvar falls |
| boss_arrive | 2d |  | +0% | boss_arrive | Grimmvar rises (stinger) |
| champ_arrive | 2d |  | +0% | champ_arrive | a champion takes the field |
| champ_telegraph | 3d |  | +0% | champ_telegraph | champion ability wind-up (chain, summon, scream) |
| champ_chain | 3d |  | -10% | sp_crunch_low | the Reaper chain bites |
| champ_summon | 3d |  | -25% | en_ghost_0, en_ghost_1, en_ghost_2 | a necromancer / lich raises the dead |
| champ_enrage | 3d |  | -30% | en_roar | a champion is enraged |
| hound_night | 2d |  | -5% | boss_arrive | special night begins (generic) |
| night_falls | 2d |  | -40% | sp_drone_loop | dusk: the night is coming (warning) |
| night_start | 2d |  | -15% | champ_arrive | the night begins: the dead rise |
| night_end | 2d |  | -10% | coffer_land | dawn: the night survived |
| round_start | 2d |  | -15% | champ_arrive | (round start) |
| round_end | 2d |  | -10% | coffer_land | (round end) |
| night_thunder | 2d |  | -30% | sp_thunder | distant thunder (night ambience) |
| event_bloodmoon | 2d |  | -10% | boss_arrive | Blood Moon begins |
| event_fog | 2d |  | +0% | night_fog | Fog Night begins |
| event_ash | 2d |  | +0% | night_ash | Ash Storm begins |
| event_champions | 2d |  | +0% | champ_arrive | Night of Iron begins |
| nuke | 2d |  | -15% | boss_quake_0, boss_quake_1, boss_quake_2 | Nuke power-up |
| powerup_spawn | 3d |  | -20% | sp_bright_3 | a power-up drops |
| powerup_grab | 2d |  | +0% | coffer_take | a power-up taken |
| coffer_open | 3d |  | +0% | ui_open | shrine menu opens |
| coffer_charge | 3d |  | +0% | coffer_charge | the shrine gathers power (roll begins) |
| coffer_spin | 3d |  | +0% | coffer_tick | a spell passes over the crown (cycling tick, rate-limited) |
| coffer_land | 3d |  | +0% | coffer_land | the shrine lands on a spell |
| coffer_take | 2d |  | +0% | coffer_take | you take the spell |
| coffer_cheese | 3d |  | -20% | ui_close | (retired) |
| coffer_skull | 3d |  | -20% | ui_close | (retired) |
| buy | 2d |  | -10% | coffer_take | points spent |
| drink | 2d |  | -25% | sp_bright_1 | a brew drunk |
| forge_upgrade | 3d |  | +10% | sp_ignite | reforge |
| forge_ritual | 2d |  | -10% | sp_ignite | (reforge ritual) |
| brew_trollshide | 3d |  | -30% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: trollshide |
| brew_quicksilver | 3d |  | -20% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: quicksilver |
| brew_twinstrike | 3d |  | -10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: twinstrike |
| brew_hearthfire | 3d |  | +0% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: hearthfire |
| brew_windrunner | 3d |  | +10% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: windrunner |
| brew_hawkeye | 3d |  | +20% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: hawkeye |
| brew_mammoth | 3d |  | -25% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: mammoth |
| brew_frostbite | 3d |  | -5% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: frostbite |
| brew_stormcrown | 3d |  | +15% | sp_bright_0, sp_bright_1, sp_bright_2, sp_bright_3 | brew bought: stormcrown |
| ui_move | 2d |  | +0% | ui_move | menu: choose |
| ui_select | 2d |  | +0% | ui_select | menu: select |
| ui_close | 2d |  | +0% | ui_close | menu: close / back |
| quest_start | 2d |  | +0% | rite_start | a rite begins |
| quest_step | 2d |  | +0% | rite_step | a rite step done |
| rite_hum | 3d | yes | +0% | rite_hum | the ward / hold circle hums (loop while active) |
| rite_reset | 2d |  | +0% | rite_reset | the dead broke the circle: the timer resets |
| rite_fail | 2d |  | -20% | rite_reset | a rite failed |
| ward_lit | 3d |  | -10% | sp_ignite_small | a ward-fire kindled |
| curse_lifted | 2d |  | -10% | shout_learned | a city freed (with the music stinger) |
| shout_learned | 2d |  | +0% | shout_learned | a Thu'um learned |

## 4. Gaps and notes
- No footstep recordings were supplied: undead footsteps stay the engine's own (they are part of the creature skeleton's movement, not a sound the mod plays).
- Blood/impact splashes on the player from undead claws were Skyrim impact sets; they are removed (the claw swing is ours).
- Several spells share source material (the library has one fire, one ice, one shock family); each spell differs by take, engine pitch and its own release/burst combination.
- Music: `mus_menu` (main menu, via sMainMenuMusic), `mus_boss` (Grimmvar, MUSC UN_MUSBoss, loops), `mus_freed` (a city freed, MUSC UN_MUSFreed).
