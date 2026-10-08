# Undying Nights — state and next steps (2026-10-07)

## Working (verified in game)
- Zombies visible (skinconv now writes per-vertex SSE weights); torso fix (continuous trunk remap) built, needs a close look.
- Wands in hand with HUD (name, ability line, infinity), tomes/grimoires on magicka, 19 signature cast sounds.
- Wand bolts: spells load their effects since the ESP group-order fix (MGEF before ENCH/SPEL). `bolt:` log shows real projectiles
  launching from the wand tip; 10 of 12 bolts fly on Skyrim's own spell projectiles. Still to confirm visually.
- One round per night, tally counter, 30-min day cycle (60/40), dawn despawn, night truce for NPCs, sprint OK on fresh character.
- Station stand with sloped walkable skirt, map markers, clutter cleared from the footprint.

## Built 2026-10-07 afternoon (9dfaa60, installed, NOT yet seen in game)
- Signature moves get their own motion: arm/wand poses per style (sheet weapons.chargeStyle: thrust, slam, sweep,
  twirl, raise, surge) applied after Skyrim's animation (PlayerCharacter::UpdateAnimation hook, upper arm bone);
  charge hum + ready ping, FOV kick on release. Log line "pose: arm animation live" confirms the hook moves bones.
  If the arm doesn't move in game, the bone write happens before Skyrim's own pose: move PoseArm to a later hook.
- One native cast per click: after 0.12 s held, Skyrim is told the button was released (AttackBlockHandler
  ProcessButton hook); tome hand spells are fire-and-forget. All formerly automatic weapons are semi (+40% damage).
- 2x zombies: spawnMultiplier 6, at-once floor 12, maxAlive 64, spawn delay halved.
- Undead faction (UN_UndeadFaction, allies to each other, enemy of PlayerFaction), aggression 1: no infighting.
- Standing corpses get knocked over (KnockExplosion) if not ragdolled 0.5 s after death; KillImpl only if not dead.
- Falkreath stand: GroundTop now needs 16/20 ground hits and hi-lo <= 110, sinks into slopes (top <= low + 40);
  adopted stands that fail it are rebuilt.
- Dev: `hold <mouse id> <secs>` (fire = 0) holds the button for charge tests.

## Built 2026-10-08 00:20 (69fdf39 + un_v10 assets, installed, NOT yet seen in game)
- Reforge upright (assets.py forge: rotate 90 0 90, ground splash Mesh.003 skipped, fit 230).
- Stations move OUT of towns: walled cities anchored on their main gate on the Tamriel side (Whiterun 01B1F1, Solitude
  01C386, Windhelm 055FCB, Markarth 01C389, Riften 042286); search 900-3600 from the anchor; obstacles gathered once
  (doors 900, buildings/walls/cliffs >450 + 550, clutter/trees +160), slope <= 30 for a perfect spot; in-town stands
  from older saves fail adoption (crowding) and SweepOldStands clears our pieces in worldspaces/places no town owns.
- Station = Nordic shrine (tools/stand.py): Whiterun textures by path, alcoves per machine with carved panels,
  timber lintels, canopy on posts, steps, knotwork trim; collision via new modelconv `collidebox2`. Dressing:
  WH fire braziers + fire FX + lights, urns, candle lanterns. Adopt overhead tolerance 440 (the wall is ~314 tall).
- Staffs: native press/hold swallowed; on release (shot or move) a synthetic 0.15 s tap plays Skyrim's staff cast.
  Charge pose = kStaffCharge (no spin); PoseWand removed.
- Event nights (sheets/events.json, 25%): Blood Moon (DLC1Eclipse, x1.5), Fog Night (SkyrimFog, Grimmvar 20 s in),
  Ash Storm (DLC02VolcanicAshStorm01, speed x1.25, points x2), Night of Iron (storm, champions x4, drops x2). HUD tint.
- Nuke (was Kaboom): kills all but the boss, +400, no spawns for 20 s, white flash + HUD timer.
- The Undying Dirge (sheets/curse.json, src/Curse.cpp): per city 4 night quests (tithe 30 kills / 3 ward-fires /
  Herald = Grimmvar in fog / hold the station 180 s at double pressure); GLOB UN_Curse_<town> = step*1000+progress;
  freed city = sanctuary (city location chain incl. interiors, or 6500 of its station). Lore scrolls on the HUD.
- VERIFY: stand look + collision, braziers' fire FX, Reforge upright, new stand spots outside each city, staff swing on
  release (no cast on press), event weather, ward-fire placement on open ground, Herald spawn, sanctuary crumble.

## Built 2026-10-07 20:50 (c2374f9 + un_v9_a/b/c assets, installed; graphics mods installed, launch via skse64_loader.exe)
- Enemies: Iron Reaper (draugr skeleton, autorig.py + rigwrite, invisible 2H 'Reaper Scythe' for swings; reap sweep
  every 6.5 s in reach, enrage <35%) and Voidbound Necromancer (BretonRace, skin covers head/hands/feet, spells from
  EncWarlock07TemplateNecro, holds range, raises 2 vanilla necro skeletons every 11 s, max 8). Weights 14/5/5.
  VERIFY: necromancer head hidden (no FaceGen), reaper swings with draugr 2H anims, skeleton minions hostile.
- Staffs kept: sidearm, aetherium, mammothlmg, triad. Remade from the 'magic staffs basic bundle': animunculi
  Hexbinder (Doom Mark), longshot Starforge Spire (Orbital Strike), centurion Plaguebringer (Pestilence), kagrenac
  Mindbreaker (Mass Frenzy: out of undead faction, StartCombat on nearest zombie, 12%/0.5 s tearing).
- Materials: tools/skymat.py (wood/iron/steel/bronze/gold/silver/leather/bone/crystal/vines, 1024 + spec-in-alpha).
- Bows charge-only (2.6 s); staffs 0.75 s delay, 2.0 s charge; no wall-buys (retired + removed from stands);
  chargeSpell none everywhere (vanilla area spells hurt NPCs); FUS RO DAH wind bow.
- Power-ups (PowerUps namespace in Director.cpp): 3.5% drop, max 4/night, one at a time, 26 s; Insta-Kill,
  Double Points, Kaboom, Fire Sale (Coffer 10 points), HUD timers.

## Built 2026-10-07 17:00 (2857ca4 + un_staffs_v7, installed)
- Release decides: full charge -> signature move only, shorter -> normal shot (OnButton; TryUnleash returns bool).
- Dirt/rock blasts (giantslam*, dragondust) deleted. drill + spider retired; kagrenac = Chronos Hourglass (Time Stop:
  freeze 4 s then 4x damage), longshot = Prismheart (Prism Sentinel: crystal beams 3 nearest for 5 s); models from
  tools/staffs_proc.py (procedural), sounds e_time/e_prism, u_time/u_prism.

## Built 2026-10-07 16:30 (e7788b1 + un_weapons_v6 assets, installed, not yet seen in game)
- Roster: 8 staffs (tools/staffs.py from the staff pack: heaven/void/heart/seed bases x 2 palettes + add-ons; NIF Prn
  WeaponStaff, grip at origin, head y=62) and 4 bows (tools/bows.py + tools/modelconv/bowconv.cpp: mesh skinned to the
  iron bow's Bow_* bones, template keeps its behavior graph). Bow ids reuse the old tome rows (flamerepeater fire,
  frostlancer ice, stormcaller lightning, embermortar wind); steampump/marksman/kagrenac/longshot + grimoires retired.
- Bows: native draw + arrow (AMMO/PROJ arrow_<id>, damage 0, gravity 0, speed 5200, element flight model + light),
  TryFire on release, quiver topped to 80 each second; charge = hold >= 1.6 s.
- Blasts are runtime clones of Skyrim explosions (BlastForm): EXPL records in the ESL crashed the game at load.
- Sounds: energy kit e_<element> (casts) and u_<element> (unleash_<id>) in tools/synth.py.
- Ragdolls: every kill gets KnockExplosion 2.5 immediately, re-knocked if still upright.
- Verify in game: bow string bends with the draw, staffs held right (grip/orientation), arrow flight models visible,
  Black Hole pull, unleash sounds, blasts appear.

## Built 2026-10-07 15:30 (8789a91, installed, not yet seen in game)
- Arm pose hook confirmed live in game ("pose: arm animation live").
- Spell blasts: 18 no-damage EXPL copies (tools/templates/expl, sheet weapons.tapBlast/chargeBlast) placed via a
  moved XMarker; tap blast lands when the bolt arrives; charged finale = big blasts at impact + up to 5 foes (ring
  around you for sweep moves) + weapons.chargeSpell (vanilla master spells) + weapons.chargeAura body FX while charging.
- Double bolts: the wand ENCH now uses the no-projectile wandcast effect (self), so only our bolt flies.
- Crash at dawn (spamming drill at 15:16:18, log ended at "dawn ... crumbled"): despawn now StopCombat + Disable,
  delete 2 s later; NPCs fighting our undead let go first.
- Untouchable zombies fighting a follower: undead saved in an earlier session were not in g_ours (truce made them
  essential + healed). SweepStrays vanishes any of our-plugin-based actors not on the books; the truce only spares
  friendlies/teammates, hostile monsters are targetable at night.
- Zombies carry a Draugr sword / iron sword / iron dagger.

## Open issues (in priority order)
1. (fix built, verify) Falkreath stand still floats: adoption keeps it ("alley already built"). Add a dev command that forces a town rebuild
   (Sweep + plan reset) and log GroundTop/low/coffer z on adoption to see why the 30-unit / slope checks pass.
2. Confirm bolts are visible (screenshot right after a real click); Nightshade + Bloodthirst use our own PROJ (no vanilla match).
3. Wand staff cast: ENCH is fire-and-forget aimed with the bolt effect; check the animation now that the enchant has effects.
4. Test loop: launch Skyrim ONLY when it isn't running (computer_open_application on a running game starts a second
   instance and truncates the log). Quit with test-channel `console qqq` in game, or Main menu > Quit by keyboard.
   steam_appid.txt (489830) in the game folder lets SkyrimSE.exe start directly.
5. Cmd file writes sometimes fail (ENOENT) while the game deletes it: write, then retry once.

## Roadmap Noah asked for
- Multiplayer (4-player co-op): build on Skyrim Together Reborn (STR) rather than custom netcode. Host-authoritative
  Director: only the host spawns undead and runs rounds; points/perks/slots per player synced through STR's script
  events or a small side channel; Coffer/Brew/Reforge purchases validated by the host.
- Apex-style movement polish: slide momentum curve, slide-hop, mantle onto ledges, wall-bounce, air strafe tuning.
- Creative content: special night events (blood moon, fog night), traps on the stand, Pack-a-Punch-style wand
  alt-fires at Reforge level 3, easter-egg quest per town, more elites, boss arena at Bleak Falls.
