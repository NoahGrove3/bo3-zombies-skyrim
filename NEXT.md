# Undying Nights — state and next steps (2026-10-07)

## Working (verified in game)
- Zombies visible (skinconv now writes per-vertex SSE weights); torso fix (continuous trunk remap) built, needs a close look.
- Wands in hand with HUD (name, ability line, infinity), tomes/grimoires on magicka, 19 signature cast sounds.
- Wand bolts: spells load their effects since the ESP group-order fix (MGEF before ENCH/SPEL). `bolt:` log shows real projectiles
  launching from the wand tip; 10 of 12 bolts fly on Skyrim's own spell projectiles. Still to confirm visually.
- One round per night, tally counter, 30-min day cycle (60/40), dawn despawn, night truce for NPCs, sprint OK on fresh character.
- Station stand with sloped walkable skirt, map markers, clutter cleared from the footprint.

## Built 2026-10-08 21:20 (2c644e9, NOT installed yet: Skyrim was running. Install _un_builds/UN-2c644e9-core.zip; pack + Melty zip refreshed)
- STR auto-join: Relay.cpp (UDP, no PCH) + Net tunnel on Steam channel 8; friend's STR -> 127.0.0.1:10578; host forwards to its
  server (uPort from STServer.ini), starts SkyrimTogetherServer.exe if the port is free; lobby data str_pw.
  STR UI main.js patched once (backup main.js.before-undying-nights) to poll UI/undying_connect.json and call skyrimtogether.connect.
- Main menu: full picture; Skyrim's button words luma-keyed back (D3DCompile ps_4_0 via d3dcompiler_47), own cursor, lightning bolts;
  picture hidden whenever any other menu is open (pop-ups). Lore book given once the character is made.

## Built 2026-10-08 20:50 (0f17fe6, installed via UN-0f17fe6-core.zip, pack + Melty zip refreshed, NOT yet seen in game)
- Net.cpp: Steam flat API (steam_api64 v009/v017/v021/v010, NetworkingMessages v002), friends-only lobby, invite overlay,
  GameLobbyJoinRequested (333) callback, +connect_lobby, friends-in-parties list; channel 7; host State 4 Hz; client curse max-merge.
- Coop::Client() also true for a Steam party member. Director::NetFollow drives the client's night from the host.
- Main menu: assets/ui/menu_bg.png (cover.py MENU=1, bottom-right open for Skyrim's buttons) + drifting fog, embers,
  moon pulse, lightning; party panel bottom-left (also in the Journal menu); clicks via InputWatch -> Hud::OnClick.
- "Character Selection" on the main menu is Skyrim Together's own (string in SkyrimTogether.exe); the menu art covers it.
- Still to do: STR players still connect to the host's STR server themselves; two-PC test.

## PLAN for when Noah is back home (saved 2026-10-08 19:00 ET)
1. Re-link the PC; install UN-f408882.zip (boss/elite styles); refresh Party Pack + Melty zip.
2. Co-op option 2: keep Skyrim Together for players; our own Steam networking (steam_api64 flat API: ISteamNetworkingMessages,
   friends in Skyrim found via ISteamFriends) carrying host-authoritative zombies-game state: night/round/countdown, event
   night (weather, mist, boss), power-up activations (Nuke etc. for all), Coffer town, curse globals/freed cities, boss HP.
   Test with a friend (two PCs).
3. Custom animated main menu in the cover-art style (blood moon, shrine, embers, fog drift). Options: replace the menu
   background/logo meshes, or draw our own layer behind the Scaleform buttons (our Present hook already runs on the menu).
4. Lobby in the menu: Host / Join buttons, Steam invites (ISteamMatchmaking lobby + invite overlay), so no Party.txt.
5. Remove the "choose character" button at launch: Noah says it's left over from the BO3 crew picker (Dempsey, Takeo...).
   Remove the crew picker everywhere it can still show (Intro.cpp crew screen, Hud "CHOOSE YOUR HERO", any menu entry),
   keeping saves working (crew -1 = none).

## Built 2026-10-08 22:36 (f408882, NOT installed: PC link dropped. Install UN-f408882.zip, refresh pack + Melty zip)
- Boss / Gloomstalker: hidden weapons wyrm_claws (2H anim, speed 0.7, reach 1.9) / shadow_claws (dagger anim, 1.45) via CNTO + equip; no sword.
- Hazards (Director): telegraphed ground bursts. Grimmvar: Grave Slam (power-attack windup), Grave Eruption line, Ash Fall when enraged. Gloomstalker: Umbral Pounce after lunge. Reaper: Chain Hook (420-1100).
- New expl templates giantslam (crGiantSlamExplosion), dragonfire.

## Built 2026-10-08 22:05 (0556f96 + UN-staffs-v12 re-textured staffs + brewmachine.nif, installed, Melty zip + pack refreshed, NOT yet seen in game)
- Staffs: WEAP DNAM anim type 4 (one-handed mace grip) instead of 8 (staff: open-hand cast in 1st person); our jab/charge/thrust poses; no native swing.
- Unlimited fps: Present SyncInterval 0, fMaxTime:HAVOK = frame time (1/300..1/30) every frame; movement.fps_cap 0.
- Coffer staffs: own tap/charge blasts (new expl templates: poisongas, tentacle, daedraball, paralyze, icewraith, constellation, skybolt, turnundead; DLC ids as runtime 02/04). Arcane Lance = burst chain along the line.
- Fog Night: FXMistLow* ring round the player (Director::TickMist), no HUD tint for fog.
- staffs.py: seam-aware decimation (UV in the cluster key), cylindrical UVs for skymat parts, budget 40000.
- HUD: weapon dock (active card + other slots, icons from tools/weaponicons.py -> assets/ui, copied by CI), power-up pills, points count-up.
- Walled cities' map markers by id (they live in WhiterunWorld etc.).

## Built 2026-10-08 21:05 (f5a81e1, installed, Melty zip + pack refreshed, NOT yet seen in game)
- Station icons hidden until the town's own map marker is discovered (kCanTravelTo); skull moves the Coffer only to discovered towns (stays if none).
- Dais floor kStandLift 34 above ground (old stands lifted in place on adopt); ClearSite also clears the approach (front -420, sides +160).
- Coffer scale 1.6 (show models rise with its scale).

## Built 2026-10-08 20:48 (12e023b + brewmachine.nif rotated 180, installed, Melty zip + party pack refreshed, NOT yet seen in game)
- Aim ray skips the player's collision group and anything within 140 of the camera (light shot burst at the feet: the ray hit the player).
- Staff native swing: attack handler from PlayerControls (our input hook eats the press, so FireButton never saw one); tap held 0.35 s. Check log for "staff: native swing".
- charge_up / charge_ready / mana_empty / cast_* in the dark kit.
- Map icons for every town from the start (preview spot 2200 out, no fast travel until built); icons found in the worldspace persistent cell (no doubles); active Coffer town named on its icon.
- One Coffer at a time: other towns' chests sleep; the dragon skull moves it to a random other town.

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
